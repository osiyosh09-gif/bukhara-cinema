// Phase 15 (part 3, 15c-1) acceptance tests: VST 3 audio plug-ins. A small
// effect built from the Steinberg SDK (tests/vst3/BcTestGain.cpp) is the
// plug-in: "Gain" multiplies the sound by 2 x its value, "Invert" flips it.
#include "app/AboutDialog.h"
#include "app/EffectsPanel.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "app/Vst3Plugins.h"
#include "core/AudioFx.h"
#include "core/MediaCache.h"
#include "core/Vst3Host.h"
#include "engine/TimelinePlayer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListWidget>
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

constexpr int kRate = 48000;
const QString kUid = QStringLiteral("6B1C3A104E2D47A19F2B5C8D3E7A1B42");
const QString kName = QStringLiteral("BC Test Gain");

QString bundleDir()
{
    return QStringLiteral(BC_TEST_VST3_DIR);
}

QString bundlePath()
{
    return QDir(bundleDir()).filePath(QStringLiteral("BcTestGain.vst3"));
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

std::vector<float> sine(double amplitude, int frames = kRate / 2)
{
    std::vector<float> s(size_t(frames) * 2);
    for (int i = 0; i < frames; ++i)
        s[size_t(i) * 2] = s[size_t(i) * 2 + 1] = float(amplitude * std::sin(2 * M_PI * 440 * i / kRate));
    return s;
}

double peak(const std::vector<float> &s)
{
    float p = 0;
    for (float v : s)
        p = std::max(p, std::abs(v));
    return p;
}

bool copyDir(const QString &from, const QString &to)
{
    QDir src(from);
    if (!QDir().mkpath(to))
        return false;
    for (const QFileInfo &fi : src.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
        const QString target = QDir(to).filePath(fi.fileName());
        if (fi.isDir() ? !copyDir(fi.filePath(), target) : !QFile::copy(fi.filePath(), target))
            return false;
    }
    return true;
}

ClipEffect gainEffect(double gain)
{
    return ClipEffect::makePlugin(bundlePath(), kUid, kName, {{ClipEffect::pluginParamKey(0), gain}});
}

} // namespace

class TestPhase15c : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;
    QString m_copy; // a second copy of the bundle, with moduleinfo.json

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
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase15c"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        // Only the test plug-in's folder besides the standard ones.
        QSettings().setValue(QStringLiteral("vst3/folders"), QStringList{bundleDir()});
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        QVERIFY2(QFileInfo(bundlePath()).isDir(), qPrintable(bundlePath()));
        m_video = m_dir.filePath(QStringLiteral("tone.mp4"));
        QVERIFY2(makeVideo(m_video), "ffmpeg could not create test media");
    }

    void host()
    {
        QVERIFY(vst3::available());
        const std::vector<std::string> found = vst3::findBundles(bundleDir().toStdString());
        QCOMPARE(int(found.size()), 1);
        QCOMPARE(QDir::cleanPath(QString::fromStdString(found[0])), QDir::cleanPath(bundlePath()));

        // No moduleinfo.json: nothing without loading.
        bool loaded = true;
        std::string error;
        QVERIFY(vst3::describeBundle(bundlePath().toStdString(), false, &loaded, &error).empty());
        QVERIFY(!loaded);
        const std::vector<vst3::PluginInfo> infos = vst3::describeBundle(bundlePath().toStdString(), true, &loaded, &error);
        QVERIFY2(infos.size() == 1, error.c_str());
        QVERIFY(loaded);
        QCOMPARE(QString::fromStdString(infos[0].uid), kUid);
        QCOMPARE(QString::fromStdString(infos[0].name), kName);
        QVERIFY(QString::fromStdString(infos[0].subCategories).startsWith(QStringLiteral("Fx")));

        std::unique_ptr<vst3::Plugin> p = vst3::Plugin::create(bundlePath().toStdString(), kUid.toStdString(), &error);
        QVERIFY2(p, error.c_str());
        const std::vector<vst3::ParamInfo> params = p->parameters();
        QCOMPARE(int(params.size()), 2);
        QCOMPARE(QString::fromStdString(params[0].title), QStringLiteral("Gain"));
        QCOMPARE(params[0].defaultValue, 0.5);
        QCOMPARE(params[1].stepCount, 1);
        QVERIFY(!p->valueText(0, 0.25).empty());

        for (int channels : {2, 1}) {
            QVERIFY(p->prepare(kRate, channels, 512));
            p->setParameter(0, 0.25); // x0.5
            std::vector<float> s(size_t(3000) * channels, 0.5f);
            p->process(s.data(), 3000, channels); // more than one block
            QVERIFY(std::abs(s[10] - 0.25f) < 1e-4);
            QVERIFY(std::abs(s.back() - 0.25f) < 1e-4);
            p->setParameter(1, 1); // invert
            std::fill(s.begin(), s.end(), 0.5f);
            p->process(s.data(), 3000, channels);
            QVERIFY(std::abs(s[10] + 0.25f) < 1e-4);
            p->setParameter(1, 0);
        }
        // A wrong class id or bundle: no instance, an error.
        error.clear();
        QVERIFY(!vst3::Plugin::create(bundlePath().toStdString(), std::string(32, '0'), &error));
        QVERIFY(!error.empty());
        QVERIFY(!vst3::Plugin::create(m_dir.filePath(QStringLiteral("none.vst3")).toStdString(), kUid.toStdString(), &error));
    }

    void moduleInfo()
    {
        std::string error;
        const std::string json = vst3::moduleInfoJson(bundlePath().toStdString(), &error);
        QVERIFY2(!json.empty(), error.c_str());
        // A copy that carries moduleinfo.json is described without loading it.
        m_copy = m_dir.filePath(QStringLiteral("copy/BcTestGain.vst3"));
        QVERIFY(copyDir(bundlePath(), m_copy));
        QVERIFY(QDir().mkpath(m_copy + QStringLiteral("/Contents/Resources")));
        QFile f(m_copy + QStringLiteral("/Contents/Resources/moduleinfo.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray::fromStdString(json));
        f.close();
        bool loaded = true;
        const std::vector<vst3::PluginInfo> infos = vst3::describeBundle(m_copy.toStdString(), false, &loaded, &error);
        QCOMPARE(int(infos.size()), 1);
        QVERIFY(!loaded);
        QCOMPARE(QString::fromStdString(infos[0].uid), kUid);
        QCOMPARE(QString::fromStdString(infos[0].name), kName);
    }

    void scanner()
    {
        QVERIFY(Vst3Plugins::available());
        QVERIFY(Vst3Plugins::extraFolders().contains(bundleDir()));
        QList<Vst3PluginEntry> list = Vst3Plugins::plugins();
        auto count = [](const QList<Vst3PluginEntry> &l) {
            int n = 0;
            for (const Vst3PluginEntry &e : l)
                n += e.uid == kUid;
            return n;
        };
        QCOMPARE(count(list), 1);
        // The list is kept in the settings: a new run does not load the bundle again.
        const QByteArray cache = QSettings().value(QStringLiteral("vst3/cache")).toByteArray();
        QVERIFY(cache.contains(kUid.toLatin1()));

        // The copy's folder added: two places of the same plug-in.
        const QString copyFolder = QFileInfo(m_copy).absolutePath();
        QSettings().setValue(QStringLiteral("vst3/folders"), QStringList{bundleDir(), copyFolder});
        QCOMPARE(count(list = (Vst3Plugins::rescan(), Vst3Plugins::plugins())), 2);

        // A bundle that was being read when the program stopped is skipped from then on.
        QSettings().setValue(QStringLiteral("vst3/scanning"), m_copy);
        Vst3Plugins::rescan();
        QCOMPARE(count(Vst3Plugins::plugins()), 1);
        QVERIFY(Vst3Plugins::badBundles().contains(QFileInfo(m_copy).canonicalFilePath()));
        QVERIFY(!QSettings().contains(QStringLiteral("vst3/scanning")));
        QSettings().setValue(QStringLiteral("vst3/folders"), QStringList{bundleDir()});
        Vst3Plugins::rescan();

        // The instance for the interface is kept.
        vst3::Plugin *ui = Vst3Plugins::uiInstance(bundlePath(), kUid);
        QVERIFY(ui);
        QCOMPARE(Vst3Plugins::uiInstance(bundlePath(), kUid), ui);
        QString error;
        QVERIFY(!Vst3Plugins::uiInstance(m_dir.filePath(QStringLiteral("none.vst3")), kUid, &error));
        QVERIFY(!error.isEmpty());
    }

    void model()
    {
        ClipEffect e = gainEffect(0.25);
        QVERIFY(ClipEffect::isAudio(e.kind));
        QVERIFY(!ClipEffect::kinds(true).contains(ClipEffect::Kind::Plugin)); // listed by name instead
        QVERIFY(!e.isNeutral());
        QCOMPARE(e.value(QStringLiteral("p0")), 0.25);
        QVERIFY(e.setValue(QStringLiteral("p1"), 3)); // clamped to 0..1
        QCOMPARE(e.value(QStringLiteral("p1")), 1.0);
        QVERIFY(!e.setValue(QStringLiteral("gain"), 1)); // not a plug-in key
        quint32 id = 0;
        QVERIFY(ClipEffect::pluginParamId(QStringLiteral("p17"), &id));
        QCOMPARE(id, 17u);

        const ClipEffects back = effectsFromJson(effectsToJson({e}));
        QCOMPARE(back.size(), 1);
        QVERIFY(back[0] == e);
        QCOMPARE(back[0].pluginPath, bundlePath());
        QCOMPARE(back[0].pluginId, kUid);
        QCOMPARE(back[0].pluginName, kName);
        QCOMPARE(effectsFromEngineString(effectsToEngineString({e})).size(), 1);
        QCOMPARE(Project::kFormatVersion, 14);
    }

    void chain()
    {
        AudioChain chain;
        chain.configure({gainEffect(0.25)}, kRate, 2);
        QVERIFY(chain.isActive());
        std::vector<float> s = sine(0.5);
        chain.process(s.data(), int(s.size() / 2));
        QVERIFY2(std::abs(peak(s) - 0.25) < 0.005, qPrintable(QString::number(peak(s))));

        // A new value reaches the same instance.
        chain.configure({gainEffect(1.0)}, kRate, 2);
        s = sine(0.25);
        chain.process(s.data(), int(s.size() / 2));
        QVERIFY(std::abs(peak(s) - 0.5) < 0.005);

        // A plug-in that is not there: the sound passes unchanged.
        ClipEffect missing = gainEffect(0.25);
        missing.pluginPath = m_dir.filePath(QStringLiteral("gone.vst3"));
        AudioChain other;
        other.configure({missing}, kRate, 2);
        s = sine(0.5);
        other.process(s.data(), int(s.size() / 2));
        QVERIFY(std::abs(peak(s) - 0.5) < 0.005);
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
        const int e = ctl.addClipPlugin(audio, bundlePath(), kUid, kName, &error);
        QVERIFY2(e == 0, qPrintable(error));
        // Defaults: unchanged.
        QVERIFY(std::abs(level() / full - 1) < 0.05);
        QVERIFY(ctl.setClipEffectValue(audio, e, ClipEffect::pluginParamKey(0), 0.25));
        const double half = level() / full;
        QVERIFY2(std::abs(half - 0.5) < 0.05, qPrintable(QString::number(half)));
        QVERIFY(ctl.setClipEffectEnabled(audio, e, false));
        QVERIFY(std::abs(level() / full - 1) < 0.05);
        QVERIFY(ctl.setClipEffectEnabled(audio, e, true));

        // The export sound (frames one after another, as the exporter and the
        // mix measurement get them): half of the plug-in at its neutral value.
        auto rendered = [&] {
            std::vector<float> mix;
            player.setProject(ctl.project());
            const bool ok = player.renderAudio(0, 50, [&](const float *s, int frames) {
                mix.insert(mix.end(), s, s + size_t(frames) * 2);
                return true;
            });
            if (!ok || mix.size() < size_t(kRate))
                return -1.0;
            // The first frames after the seek to 0 depend on where the AAC
            // decoder was before; from frame 10 on the tone is steady.
            mix.erase(mix.begin(), mix.begin() + 10 * 1920 * 2);
            return peak(mix);
        };
        const double half2 = rendered();
        QVERIFY(ctl.setClipEffectValue(audio, e, ClipEffect::pluginParamKey(0), 0.5));
        const double unity = rendered();
        QVERIFY(ctl.setClipEffectEnabled(audio, e, false));
        const double without = rendered();
        qInfo("rendered peaks: x0.5 %f, x1 %f, without %f", half2, unity, without);
        QVERIFY2(unity > 0.02 && std::abs(half2 / unity - 0.5) < 0.02,
                 qPrintable(QStringLiteral("%1 %2").arg(half2).arg(unity)));
        QVERIFY(std::abs(unity / without - 1) < 0.02); // the neutral value changes nothing
    }

    void controller()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        loadVideo(ctl);
        const Timeline &tl = ctl.timeline();
        const QUuid video = tl.tracks[tl.indexOf(Kind::Video, 0)].clips[0].id;
        const QUuid audio = audioClip(ctl);
        QString error;
        QCOMPARE(ctl.addClipPlugin(video, bundlePath(), kUid, kName, &error), -1);
        QVERIFY(!error.isEmpty());
        QCOMPARE(ctl.addClipPlugin(audio, m_dir.filePath(QStringLiteral("none.vst3")), kUid, kName, &error), -1);
        QVERIFY(ctl.clipEffects(audio).isEmpty());

        const int before = ctl.undoStack()->count();
        QCOMPARE(ctl.addClipPlugin(audio, bundlePath(), kUid, QString(), &error), 0);
        QCOMPARE(ctl.undoStack()->count(), before + 1);
        const ClipEffect e = ctl.clipEffects(audio)[0];
        QCOMPARE(e.kind, ClipEffect::Kind::Plugin);
        QCOMPARE(e.pluginName, kName); // taken from the plug-in when not given
        QCOMPARE(e.value(QStringLiteral("p0")), 0.5);
        QVERIFY(e.params.contains(QStringLiteral("p1")));

        // Saved in the project and read back.
        const QString path = m_dir.filePath(QStringLiteral("plugins.bcproj"));
        QVERIFY(ctl.setClipEffectValue(audio, 0, QStringLiteral("p0"), 0.3));
        QVERIFY2(ctl.saveAs(path, &error), qPrintable(error));
        ProjectController again;
        QVERIFY2(again.open(path, &error), qPrintable(error));
        const ClipEffects loaded = again.clipEffects(audioClip(again));
        QCOMPARE(loaded.size(), 1);
        QVERIFY(loaded[0] == ctl.clipEffects(audio)[0]);
        QCOMPARE(loaded[0].value(QStringLiteral("p0")), 0.3);

        ctl.undoStack()->undo(); // the value
        ctl.undoStack()->undo(); // the plug-in
        QVERIFY(ctl.clipEffects(audio).isEmpty());
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
        auto *kind = panel->findChild<QComboBox *>(QStringLiteral("effectAddKind"));
        auto *find = panel->findChild<QToolButton *>(QStringLiteral("effectFindPlugins"));

        view->setSelection({audio});
        QCOMPARE(panel->clip(), audio);
        QVERIFY(find->isVisibleTo(panel));
        int at = -1;
        for (int i = 0; i < kind->count(); ++i)
            if (kind->itemText(i).contains(kName))
                at = i;
        QVERIFY2(at > ClipEffect::kinds(true).size(), "the plug-in follows the built-in sound effects");
        QVERIFY(kind->itemText(at).startsWith(QStringLiteral("VST 3: ")));
        find->click(); // looks again; the plug-in stays listed
        QVERIFY(kind->findText(kind->itemText(at)) >= 0);
        kind->setCurrentIndex(kind->findText(kind->itemText(at)));
        panel->findChild<QToolButton *>(QStringLiteral("effectAdd"))->click();
        QCOMPARE(ctl->clipEffects(audio).size(), 1);
        auto *list = panel->findChild<QListWidget *>(QStringLiteral("effectList"));
        QVERIFY(list->item(0)->text().contains(kName));

        // The plug-in's own parameters: a slider for Gain, a switch for Invert.
        auto *gain = panel->findChild<QSlider *>(QStringLiteral("effectParam_p0"));
        QTRY_VERIFY(gain && gain->isVisible());
        QCOMPARE(gain->value(), gain->maximum() / 2);
        auto *text = panel->findChild<QLabel *>(QStringLiteral("effectParam_p0_text"));
        QVERIFY(text && !text->text().isEmpty());
        const QString oldText = text->text();
        gain->setValue(gain->maximum() / 4);
        QCOMPARE(ctl->clipEffects(audio)[0].value(QStringLiteral("p0")), 0.25);
        QVERIFY(text->text() != oldText);
        auto *invert = panel->findChild<QCheckBox *>(QStringLiteral("effectParam_p1"));
        QVERIFY(invert);
        invert->setChecked(true);
        QCOMPARE(ctl->clipEffects(audio)[0].value(QStringLiteral("p1")), 1.0);
        // Undo turns the switch back.
        ctl->undoStack()->undo();
        QVERIFY(!invert->isChecked());

        // A project whose plug-in is not on this computer: a note, no sliders.
        QString error;
        const QString path = m_dir.filePath(QStringLiteral("missing.bcproj"));
        QVERIFY(ctl->saveAs(path, &error));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray data = f.readAll();
        f.close();
        const QByteArray from = ctl->clipEffects(audio)[0].pluginPath.toUtf8();
        QVERIFY(data.contains(from));
        data.replace(from, QDir::fromNativeSeparators(m_dir.filePath(QStringLiteral("elsewhere/BcTestGain.vst3"))).toUtf8());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(data);
        f.close();
        QVERIFY2(ctl->open(path, &error), qPrintable(error));
        const QUuid moved = audioClip(*ctl);
        view->setSelection({moved});
        list->setCurrentRow(0);
        QTRY_VERIFY(panel->findChild<QLabel *>(QStringLiteral("effectPluginMissing")));
        QVERIFY(!panel->findChild<QSlider *>(QStringLiteral("effectParam_p0")));
        QCOMPARE(ctl->clipEffects(moved)[0].pluginName, kName);
    }

    void about()
    {
        AboutDialog::initResources();
        QVERIFY(!QImage(QStringLiteral(":/vst/vst-compatible.png")).isNull());
        const QString html = AboutDialog::licensesHtml();
        QVERIFY(html.contains(QStringLiteral("VST 3 SDK")));
        QVERIFY(html.contains(QStringLiteral("MIT")));
        QVERIFY(html.contains(QStringLiteral("LGPL")));
        QVERIFY(AboutDialog::vstTrademark().contains(QStringLiteral("Steinberg")));
        AboutDialog d(AboutDialog::Page::About);
        auto *logo = d.findChild<QLabel *>(QStringLiteral("aboutVstLogo"));
        QVERIFY(logo);
        QVERIFY(logo->text().contains(QStringLiteral(":/vst/vst-compatible.png")));
        QVERIFY(logo->text().contains(QStringLiteral("VST®")));
    }
};

QTEST_MAIN(TestPhase15c)
#include "tst_phase15c.moc"
