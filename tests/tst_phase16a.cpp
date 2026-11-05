// Phase 16a acceptance tests: automatic multicamera edit (who is speaking,
// or a rhythm): the planner, the cache of envelopes, the controller and the dialog.
#include "app/AutoMulticamDialog.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/AutoMulticam.h"
#include "core/MediaCache.h"

#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

#include <cmath>
#include <functional>
#include <vector>

using namespace bc;
using Kind = Timeline::Kind;
using AutoMulticam::Cut;
using AutoMulticam::Mode;
using AutoMulticam::Settings;

namespace {

constexpr double kFps = 25;
constexpr int kRate = AutoMulticam::kRate;

// Level of a camera: `loud` (RMS) where `speaks(t)`, a quiet room otherwise.
std::vector<float> camera(double seconds, const std::function<bool(double)> &speaks, float loud = 0.3f,
                          float room = 0.01f)
{
    std::vector<float> env(static_cast<size_t>(seconds * kRate));
    for (size_t i = 0; i < env.size(); ++i) {
        // A little wobble, as real sound has.
        const float wobble = 1.0f + 0.2f * float(std::sin(double(i) * 0.37));
        env[i] = (speaks(double(i) / kRate) ? loud : room) * wobble;
    }
    return env;
}

// Speaker n is heard loudly by camera n and softly (-14 dB) by the others.
// who(t) < 0: nobody speaks.
QList<std::vector<float>> room(double seconds, const std::function<int(double)> &who, int cameras)
{
    QList<std::vector<float>> out;
    for (int a = 0; a < cameras; ++a) {
        std::vector<float> env(static_cast<size_t>(seconds * kRate));
        for (size_t i = 0; i < env.size(); ++i) {
            // People breathe: 0.3 s of quiet every 2 s.
            const double t = double(i) / kRate;
            const int w = std::fmod(t, 2.0) < 1.7 ? who(t) : -1;
            const float wobble = 1.0f + 0.2f * float(std::sin(double(i) * 0.37 + a));
            env[i] = (w < 0 ? 0.01f : w == a ? 0.3f : 0.06f) * wobble;
        }
        out << env;
    }
    return out;
}

int angleAt(const QList<Cut> &cuts, qint64 frame)
{
    int angle = cuts.isEmpty() ? -1 : cuts.first().angle;
    for (const Cut &c : cuts)
        if (c.frame <= frame)
            angle = c.angle;
    return angle;
}

bool makeCamera(const QString &path, const QString &color, const QString &volume)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-f"),
             QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("color=c=%1:size=320x240:rate=25:duration=10").arg(color), QStringLiteral("-f"),
             QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("sine=frequency=300:sample_rate=48000:duration=10,volume='%1':eval=frame").arg(volume),
             QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
             QStringLiteral("-shortest"), path});
    return p.waitForFinished(120000) && p.exitCode() == 0 && QFile::exists(path);
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

} // namespace

class TestPhase16a : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QStringList m_cams;

    // Two cameras: camera 1 hears its speaker for the first 5 s, camera 2 after.
    QUuid setup(ProjectController &ctl)
    {
        ctl.importFiles(m_cams);
        waitIdle(ctl);
        QList<QUuid> ids(2);
        for (const MediaItem &m : ctl.project().items)
            for (int i = 0; i < 2; ++i)
                if (m.path == m_cams[i])
                    ids[i] = m.id;
        ctl.setPlayhead(0);
        return ctl.createMulticam(ids, {0, 0});
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase16a"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_cams << m_dir.filePath(QStringLiteral("cam1.mov")) << m_dir.filePath(QStringLiteral("cam2.mov"));
        QVERIFY(makeCamera(m_cams[0], QStringLiteral("red"), QStringLiteral("if(lt(t,5),1,0.15)")));
        QVERIFY(makeCamera(m_cams[1], QStringLiteral("blue"), QStringLiteral("if(lt(t,5),0.15,1)")));
    }

    void speaker()
    {
        // Three people take turns: 0-10 s, 10-20 s, 20-30 s.
        const auto levels = room(30, [](double t) { return t < 10 ? 0 : t < 20 ? 1 : 2; }, 3);
        Settings s;
        s.maxShot = 0;
        const AutoMulticam::Plan p = AutoMulticam::plan(levels, {}, kFps, 0, 30 * 25, s);
        QCOMPARE(p.cuts.size(), 3);
        QCOMPARE(p.cuts[0], (Cut{0, 0}));
        QCOMPARE(p.cuts[1].angle, 1);
        QCOMPARE(p.cuts[2].angle, 2);
        // The cut comes a moment before the new speaker (at most 0.4 s early, at most 0.1 s late).
        QVERIFY2(p.cuts[1].frame >= 240 && p.cuts[1].frame <= 252, qPrintable(QString::number(p.cuts[1].frame)));
        QVERIFY2(p.cuts[2].frame >= 490 && p.cuts[2].frame <= 502, qPrintable(QString::number(p.cuts[2].frame)));
        QVERIFY(p.separation > 0.9);
        QCOMPARE(p.share.size(), 3);
        QVERIFY(std::abs(p.share[0] - 1.0 / 3) < 0.03);

        // Part of the range only: frames are those of the range.
        const AutoMulticam::Plan part = AutoMulticam::plan(levels, {}, kFps, 200, 400, s);
        QCOMPARE(part.cuts.first(), (Cut{200, 0}));
        QCOMPARE(part.cuts.size(), 2);
        QCOMPARE(part.cuts.last().angle, 1);
    }

    void shortest()
    {
        // Speakers change every 1.2 s: no shot may be shorter than 2 s.
        const auto levels = room(30, [](double t) { return int(t / 1.2) % 2; }, 2);
        Settings s;
        s.minShot = 2;
        s.maxShot = 0;
        const AutoMulticam::Plan p = AutoMulticam::plan(levels, {}, kFps, 0, 750, s);
        QVERIFY(p.cuts.size() > 1);
        for (int i = 1; i < p.cuts.size(); ++i)
            QVERIFY2(p.cuts[i].frame - p.cuts[i - 1].frame >= 50,
                     qPrintable(QStringLiteral("%1 -> %2").arg(p.cuts[i - 1].frame).arg(p.cuts[i].frame)));
    }

    void wideAndLongest()
    {
        // One person talks for 30 s, then 5 s of silence; camera 2 is the wide shot.
        const auto levels = room(35, [](double t) { return t < 30 ? 0 : -1; }, 3);
        Settings s;
        s.wideAngle = 2;
        s.maxShot = 8;
        const AutoMulticam::Plan p = AutoMulticam::plan(levels, {}, kFps, 0, 35 * 25, s);
        // The long talk is broken by the wide shot; no shot is longer than 8 s (+ the hold).
        bool wideDuringTalk = false;
        for (int i = 0; i < p.cuts.size(); ++i) {
            const qint64 end = i + 1 < p.cuts.size() ? p.cuts[i + 1].frame : 35 * 25;
            if (p.cuts[i].frame < 30 * 25 - 50)
                QVERIFY2(end - p.cuts[i].frame <= 8 * 25 + 30, qPrintable(QString::number(end - p.cuts[i].frame)));
            if (p.cuts[i].angle == 2 && p.cuts[i].frame < 28 * 25)
                wideDuringTalk = true;
        }
        QVERIFY(wideDuringTalk);
        // Silence at the end: the wide shot.
        QCOMPARE(angleAt(p.cuts, 34 * 25), 2);
        QCOMPARE(angleAt(p.cuts, 1 * 25), 0);
    }

    void picture()
    {
        // Camera 2 speaks all the time but has no picture from 10 s to 20 s.
        const auto levels = room(30, [](double) { return 1; }, 2);
        std::vector<bool> noPicture(size_t(30 * kRate), true);
        for (int i = 10 * kRate; i < 20 * kRate; ++i)
            noPicture[size_t(i)] = false;
        Settings s;
        s.maxShot = 0;
        const AutoMulticam::Plan p = AutoMulticam::plan(levels, QList<std::vector<bool>>{std::vector<bool>(), noPicture}, kFps, 0, 750, s);
        for (qint64 f = 251; f < 500; f += 10)
            QCOMPARE(angleAt(p.cuts, f), 0);
        QCOMPARE(angleAt(p.cuts, 600), 1);
    }

    void sameSound()
    {
        // Every camera hears the same: who speaks cannot be told.
        const std::vector<float> one = camera(20, [](double t) { return int(t) % 3 != 0; });
        const AutoMulticam::Plan p = AutoMulticam::plan(QList<std::vector<float>>{one, one, one}, {}, kFps, 0, 500, Settings());
        QVERIFY(p.separation < 0.1);
    }

    void rhythm()
    {
        Settings s;
        s.mode = Mode::Rhythm;
        s.minShot = 2;
        s.maxShot = 4;
        s.seed = 7;
        const QList<std::vector<float>> none{std::vector<float>(), std::vector<float>(), std::vector<float>()};
        const AutoMulticam::Plan p = AutoMulticam::plan(none, {}, kFps, 0, 60 * 25, s);
        QVERIFY(p.cuts.size() >= 60 / 4);
        for (int i = 1; i < p.cuts.size(); ++i) {
            QVERIFY(p.cuts[i].angle != p.cuts[i - 1].angle);
            const qint64 len = p.cuts[i].frame - p.cuts[i - 1].frame;
            QVERIFY2(len >= 49 && len <= 101, qPrintable(QString::number(len)));
        }
        for (double share : p.share)
            QVERIFY(share > 0.15); // every camera gets its turn
        // The same seed, the same edit.
        QVERIFY(AutoMulticam::plan(none, {}, kFps, 0, 60 * 25, s).cuts == p.cuts);
    }

    void envelopeFile()
    {
        const std::vector<float> env = camera(3, [](double t) { return t > 1; });
        const QString file = m_dir.filePath(QStringLiteral("e.bcen"));
        QVERIFY(AutoMulticam::save(file, env));
        std::vector<float> back;
        QVERIFY(AutoMulticam::load(file, back));
        QVERIFY(back == env);
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("junk");
        f.close();
        QVERIFY(!AutoMulticam::load(file, back));
    }

    void controller()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        const QUuid nest = setup(ctl);
        QVERIFY(!nest.isNull());
        const QList<QUuid> clips = ctl.autoMulticamClips({});
        QCOMPARE(clips.size(), 1); // the one at the playhead
        QCOMPARE(ctl.multicamClipAngles(clips[0]).size(), 2);
        QVERIFY(!ctl.multicamSoundKnown(clips));
        QSignalSpy ready(&ctl, &ProjectController::multicamSoundReady);
        ctl.analyzeMulticamSound(clips);
        QVERIFY(ready.wait(60000));
        QVERIFY(ctl.multicamSoundKnown(clips));

        Settings s;
        s.maxShot = 0;
        const AutoMulticam::Plan p = ctl.autoMulticamPlan(clips[0], s);
        QCOMPARE(p.cuts.size(), 2);
        QCOMPARE(p.cuts[0], (Cut{0, 0}));
        QCOMPARE(p.cuts[1].angle, 1);
        QVERIFY2(std::abs(p.cuts[1].frame - 125) <= 12, qPrintable(QString::number(p.cuts[1].frame)));

        const int before = ctl.undoStack()->count();
        QCOMPARE(ctl.applyAutoMulticam(clips, s), 1);
        QCOMPARE(ctl.undoStack()->count(), before + 1);
        const Timeline &tl = ctl.timeline();
        const QList<TimelineClip> video = tl.tracks[tl.indexOf(Kind::Video, 0)].clips;
        QCOMPARE(video.size(), 2);
        QCOMPARE(video[0].angle, 0);
        QCOMPARE(video[1].angle, 1);
        QCOMPARE(video[1].start, p.cuts[1].frame);
        // The sound was cut with it and keeps its camera.
        const QList<TimelineClip> audio = tl.tracks[tl.indexOf(Kind::Audio, 0)].clips;
        QCOMPARE(audio.size(), 2);
        QCOMPARE(audio[0].angle, audio[1].angle);
        ctl.undoStack()->undo();
        QCOMPARE(ctl.timeline().tracks[tl.indexOf(Kind::Video, 0)].clips.size(), 1);

        // Read once: a second controller finds the envelopes in the cache.
        ProjectController again;
        setup(again);
        const QList<QUuid> clips2 = again.autoMulticamClips({});
        QSignalSpy ready2(&again, &ProjectController::multicamSoundReady);
        again.analyzeMulticamSound(clips2);
        QVERIFY(ready2.wait(10000));
        QVERIFY(QDir(MediaCache::folder(MediaCache::Kind::Envelope))
                    .entryList({QStringLiteral("*.bcen")}, QDir::Files).size() >= 2);
    }

    void dialog()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        if (!ctl->canPlay())
            QSKIP("video engine not available");
        QAction *act = w.findChild<QAction *>(QStringLiteral("actAutoMulticam"));
        QVERIFY(act);
        QVERIFY(!act->text().isEmpty());
        // Nothing to edit yet: the dialog says so.
        {
            AutoMulticamDialog empty(ctl, {}, &w);
            QVERIFY(!empty.findChild<QPushButton *>(QStringLiteral("autoApply"))->isEnabled());
        }
        setup(*ctl);
        AutoMulticamDialog dlg(ctl, {}, &w);
        auto *apply = dlg.findChild<QPushButton *>(QStringLiteral("autoApply"));
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 60000);
        QVERIFY(dlg.findChild<QLabel *>(QStringLiteral("autoResult"))->text().contains(QStringLiteral("1")));
        QCOMPARE(dlg.findChild<QComboBox *>(QStringLiteral("autoWide"))->count(), 3); // None + 2 cameras
        apply->click();
        const Timeline &tl = ctl->timeline();
        QCOMPARE(tl.tracks[tl.indexOf(Kind::Video, 0)].clips.size(), 2);
        // In a rhythm: more cuts.
        ctl->undoStack()->undo();
        AutoMulticamDialog rhythm(ctl, {}, &w);
        rhythm.findChild<QComboBox *>(QStringLiteral("autoMode"))->setCurrentIndex(1);
        rhythm.findChild<QDoubleSpinBox *>(QStringLiteral("autoMinShot"))->setValue(2);
        rhythm.findChild<QDoubleSpinBox *>(QStringLiteral("autoMaxShot"))->setValue(3);
        QTRY_VERIFY(rhythm.findChild<QPushButton *>(QStringLiteral("autoApply"))->isEnabled());
        QVERIFY(rhythm.apply() >= 2);
    }
};

QTEST_MAIN(TestPhase16a)
#include "tst_phase16a.moc"
