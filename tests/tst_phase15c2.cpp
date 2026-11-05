// Phase 15 (part 3, 15c-2) acceptance tests: VST 3 plug-ins in the host
// program (BukharaPluginHost). The test plug-in (tests/vst3/BcTestGain.cpp)
// crashes or hangs on purpose when its hidden "Crash" (id 2) or "Hang" (id 3)
// parameter is set, and its window (Windows) turns "Gain" to 0.75.
#include "app/EffectsPanel.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "app/Vst3Plugins.h"
#include "core/MediaCache.h"
#include "core/Vst3Host.h"
#include "engine/TimelinePlayer.h"

#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QProcess>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUndoStack>
#include <QtTest>

#include <cmath>
#include <vector>

using namespace bc;
using Kind = Timeline::Kind;

namespace {

const std::string kUid = "6B1C3A104E2D47A19F2B5C8D3E7A1B42";
const QString kName = QStringLiteral("BC Test Gain");

QString bundlePath()
{
    return QDir(QStringLiteral(BC_TEST_VST3_DIR)).filePath(QStringLiteral("BcTestGain.vst3"));
}

std::string bundle()
{
    return bundlePath().toStdString();
}

bool makeVideo(const QString &path, int seconds = 4)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("testsrc=size=320x240:rate=25:duration=%1").arg(seconds),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("sine=frequency=440:sample_rate=48000:duration=%1").arg(seconds),
             QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-g"), QStringLiteral("50"),
             QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), path});
    return p.waitForFinished(60000) && p.exitCode() == 0 && QFile::exists(path);
}

void waitIdle(ProjectController &ctl)
{
    for (int i = 0; i < 100; ++i) {
        ctl.waitForBackgroundWork();
        bool probing = false;
        for (const MediaItem &m : ctl.project().items)
            probing |= ctl.isProbing(m.id);
        if (!probing)
            return;
        QTest::qWait(50);
    }
}

// Makes the plug-in crash in a host program of its own (as playback would).
void crashOnce()
{
    std::string error;
    std::unique_ptr<vst3::Plugin> p = vst3::Plugin::create(bundle(), kUid, &error);
    QVERIFY2(p, error.c_str());
    QVERIFY(p->prepare(48000, 2, 512));
    p->setParameter(2, 1);
    std::vector<float> s(1024, 0.5f);
    p->process(s.data(), 512, 2);
    QVERIFY(p->crashed());
}

} // namespace

class TestPhase15c2 : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;

    void loadVideo(ProjectController &ctl)
    {
        QCOMPARE(ctl.importFiles({m_video}), 1);
        waitIdle(ctl);
        QVERIFY(!ctl.placeMedia({ctl.project().items.first().id}, 0, 0).isEmpty());
    }

    QUuid audioClip(ProjectController &ctl)
    {
        const Timeline &tl = ctl.timeline();
        return tl.tracks[tl.indexOf(Kind::Audio, 0)].clips[0].id;
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase15c2"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QSettings().setValue(QStringLiteral("vst3/folders"), QStringList{QStringLiteral(BC_TEST_VST3_DIR)});
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        QVERIFY2(QFileInfo(QStringLiteral(BC_PLUGIN_HOST)).isExecutable(), BC_PLUGIN_HOST);
        Vst3Plugins::useHostProgram(QFileInfo(QStringLiteral(BC_PLUGIN_HOST)).absolutePath());
        QVERIFY(!vst3::hostProgram().empty());
        vst3::setTimeouts(2000, 30000);
        m_video = m_dir.filePath(QStringLiteral("tone.mp4"));
        QVERIFY2(makeVideo(m_video), "ffmpeg could not create test media");
    }

    void remote()
    {
        std::string error;
        std::unique_ptr<vst3::Plugin> p = vst3::Plugin::create(bundle(), kUid, &error);
        QVERIFY2(p, error.c_str());
        QVERIFY(p->isRemote());
        QCOMPARE(QString::fromStdString(p->info().name), kName);
        QCOMPARE(int(p->parameters().size()), 4);
        QCOMPARE(p->parameter(0), 0.5);
        QVERIFY(!p->valueText(0, 0.25).empty());
        for (int channels : {2, 1}) {
            QVERIFY(p->prepare(48000, channels, 4096));
            p->setParameters({{0u, 0.25}});
            std::vector<float> s(size_t(1920) * channels, 0.5f);
            p->process(s.data(), 1920, channels);
            QVERIFY(std::abs(s.front() - 0.25f) < 1e-5 && std::abs(s.back() - 0.25f) < 1e-5);
            p->setParameter(1, 1); // invert
            std::fill(s.begin(), s.end(), 0.5f);
            p->process(s.data(), 1920, channels);
            QVERIFY(std::abs(s[3] + 0.25f) < 1e-5);
            p->setParameter(1, 0);
        }
        // Many blocks: quick enough for playback (a frame of sound is 40 ms).
        QElapsedTimer t;
        t.start();
        std::vector<float> s(3840, 0.5f);
        for (int i = 0; i < 100; ++i)
            p->process(s.data(), 1920, 2);
        QVERIFY2(t.elapsed() < 2000, qPrintable(QString::number(t.elapsed())));
        QVERIFY(!p->crashed());
        // Not there: an error, no host program left running, no crash noted.
        QVERIFY(!vst3::Plugin::create(m_dir.filePath(QStringLiteral("none.vst3")).toStdString(), kUid, &error));
        QVERIFY(!error.empty());
        QVERIFY(vst3::crashMessage(bundle(), kUid).empty());
    }

    void describeIsolated()
    {
        bool crashed = true;
        std::string error;
        const std::vector<vst3::PluginInfo> infos = vst3::describeBundleIsolated(bundle(), &crashed, &error);
        QVERIFY2(infos.size() == 1, error.c_str());
        QVERIFY(!crashed);
        QCOMPARE(QString::fromStdString(infos[0].name), kName);
        QCOMPARE(Vst3Plugins::rescan(), 1);
    }

    void crash()
    {
        std::string error;
        std::unique_ptr<vst3::Plugin> p = vst3::Plugin::create(bundle(), kUid, &error);
        QVERIFY2(p, error.c_str());
        QVERIFY(p->prepare(48000, 2, 512));
        p->setParameter(2, 1); // crash in the next block
        std::vector<float> s(1024, 0.5f);
        p->process(s.data(), 512, 2);
        // This program is still here; the sound passed unchanged.
        QVERIFY(p->crashed());
        QCOMPARE(s[7], 0.5f);
        p->process(s.data(), 512, 2); // nothing happens any more
        QCOMPARE(s[7], 0.5f);
        QVERIFY(!vst3::crashMessage(bundle(), kUid).empty());
        // It is not started again by itself...
        QVERIFY(!vst3::Plugin::create(bundle(), kUid, &error));
        QVERIFY(!error.empty());
        // ...until the user says so.
        vst3::forgetCrash(bundle(), kUid);
        QVERIFY(vst3::Plugin::create(bundle(), kUid, &error));
    }

    void hang()
    {
        std::string error;
        std::unique_ptr<vst3::Plugin> p = vst3::Plugin::create(bundle(), kUid, &error);
        QVERIFY2(p, error.c_str());
        QVERIFY(p->prepare(48000, 2, 512));
        p->setParameter(3, 1); // never answers again
        std::vector<float> s(1024, 0.5f);
        QElapsedTimer t;
        t.start();
        p->process(s.data(), 512, 2);
        QVERIFY(p->crashed());
        QVERIFY2(t.elapsed() < 10000, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(s[7], 0.5f);
        QVERIFY(QString::fromStdString(vst3::crashMessage(bundle(), kUid)).contains(QStringLiteral("hung")));
        p.reset(); // ends the hung host program
        vst3::forgetCrash(bundle(), kUid);
    }

    void engine()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        loadVideo(ctl);
        const QUuid audio = audioClip(ctl);
        TimelinePlayer player;
        auto level = [&] {
            player.setProject(ctl.project());
            double l = 0;
            for (qint64 f = 30; f <= 50; ++f)
                l = player.audioLevel(f);
            return l;
        };
        const double full = level();
        QVERIFY(full > 0.05);
        QString error;
        const int e = ctl.addClipPlugin(audio, bundlePath(), QString::fromStdString(kUid), kName, &error);
        QVERIFY2(e == 0, qPrintable(error));
        QVERIFY(ctl.setClipEffectValue(audio, e, ClipEffect::pluginParamKey(0), 0.25));
        const double half = level() / full;
        QVERIFY2(std::abs(half - 0.5) < 0.05, qPrintable(QString::number(half)));

        // The plug-in crashes during playback: playback goes on, the sound passes unchanged.
        QVERIFY(ctl.setClipEffectValue(audio, e, ClipEffect::pluginParamKey(2), 1));
        const double after = level() / full;
        QVERIFY2(std::abs(after - 1) < 0.05, qPrintable(QString::number(after)));
        QVERIFY(!Vst3Plugins::crashMessage(bundlePath(), QString::fromStdString(kUid)).isEmpty());

        // Started again (without the crash switch): it works again.
        QVERIFY(ctl.setClipEffectValue(audio, e, ClipEffect::pluginParamKey(2), 0));
        Vst3Plugins::restart(bundlePath(), QString::fromStdString(kUid));
        const double again = level() / full;
        QVERIFY2(std::abs(again - 0.5) < 0.05, qPrintable(QString::number(again)));
    }

    void panel()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        if (!ctl->canPlay())
            QSKIP("video engine not available");
        loadVideo(*ctl);
        const QUuid audio = audioClip(*ctl);
        auto *panel = w.findChild<EffectsPanel *>(QStringLiteral("effectsPanel"));
        w.findChild<QTabWidget *>(QStringLiteral("rightTabs"))->setCurrentWidget(panel);
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        view->setSelection({audio});
        QString error;
        QCOMPARE(ctl->addClipPlugin(audio, bundlePath(), QString::fromStdString(kUid), kName, &error), 0);
        QTRY_VERIFY(panel->findChild<QSlider *>(QStringLiteral("effectParam_p0")));
        auto *open = panel->findChild<QToolButton *>(QStringLiteral("effectPluginWindow"));
        QVERIFY(open);
#ifdef Q_OS_WIN
        // The window opens in the host program; turning its knob is one undo step.
        QVERIFY(Vst3Plugins::editorsAvailable());
        QVERIFY(open->isEnabled());
        const int steps = ctl->undoStack()->count();
        open->click();
        QCOMPARE(Vst3Plugins::openEditorCount(), 1);
        QVERIFY(Vst3Plugins::isEditorOpen(ctl, audio, 0));
        QTRY_COMPARE(ctl->clipEffects(audio)[0].value(QStringLiteral("p0")), 0.75);
        QCOMPARE(ctl->undoStack()->count(), steps + 1);
        QTRY_COMPARE(panel->findChild<QSlider *>(QStringLiteral("effectParam_p0"))->value(), 750);
        ctl->undoStack()->undo();
        QCOMPARE(ctl->clipEffects(audio)[0].value(QStringLiteral("p0")), 0.5);
        QVERIFY(Vst3Plugins::isEditorOpen(ctl, audio, 0)); // still open, now showing 0.5
        // Removing the effect closes its window.
        QVERIFY(ctl->removeClipEffect(audio, 0));
        QCOMPARE(Vst3Plugins::openEditorCount(), 0);
        ctl->undoStack()->undo();
        QTRY_VERIFY(panel->findChild<QSlider *>(QStringLiteral("effectParam_p0")));
#else
        QVERIFY(!Vst3Plugins::editorsAvailable());
        QVERIFY(!open->isEnabled());
#endif
        // The plug-in stops while its values are shown: the panel says so and offers a restart.
        crashOnce();
        QTRY_VERIFY_WITH_TIMEOUT(panel->findChild<QLabel *>(QStringLiteral("effectPluginCrashed")), 5000);
        QVERIFY(!panel->findChild<QSlider *>(QStringLiteral("effectParam_p0")));
        auto *restart = panel->findChild<QToolButton *>(QStringLiteral("effectPluginRestart"));
        QVERIFY(restart);
        restart->click();
        QTRY_VERIFY(panel->findChild<QSlider *>(QStringLiteral("effectParam_p0")));
        QVERIFY(!panel->findChild<QLabel *>(QStringLiteral("effectPluginCrashed")));
        QVERIFY(Vst3Plugins::crashMessage(bundlePath(), QString::fromStdString(kUid)).isEmpty());
        Vst3Plugins::closeAllEditors();
    }
};

QTEST_MAIN(TestPhase15c2)
#include "tst_phase15c2.moc"
