// Phase 10 (part 1) acceptance tests: audio mixer (track volume and balance,
// master volume, meters) in the model, the engine, the controller and the
// Audio panel.
#include "app/AudioMixer.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "core/MediaCache.h"
#include "engine/TimelinePlayer.h"

#include <QDoubleSpinBox>
#include <QProcess>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUndoStack>
#include <QtTest>

using namespace bc;
using Kind = Timeline::Kind;

namespace {

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

} // namespace

class TestPhase10a : public QObject
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

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase10a"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("tone.mp4"));
        QVERIFY2(makeVideo(m_video), "ffmpeg could not create test media");
    }

    void model()
    {
        Project p;
        p.timeline = Timeline::makeDefault();
        const int a2 = p.timeline.indexOf(Kind::Audio, 1);
        p.timeline.tracks[a2].gainDb = -6.5;
        p.timeline.tracks[a2].pan = -0.4;
        p.timeline.masterGainDb = 3;
        NestedSequence n;
        n.id = QUuid::createUuid();
        n.name = QStringLiteral("N");
        n.timeline = Timeline::makeDefault();
        n.timeline.masterGainDb = -2;
        p.nested << n;
        const QString path = m_dir.filePath(QStringLiteral("mix.bcproj"));
        QString error;
        QVERIFY2(p.save(path, &error), qPrintable(error));
        Project back;
        QVERIFY2(Project::load(path, back, &error), qPrintable(error));
        QCOMPARE(back.timeline, p.timeline);
        QCOMPARE(back.timeline.tracks[a2].pan, -0.4);
        QCOMPARE(back.nested.first().timeline.masterGainDb, -2.0);
        // Out of range values from a damaged file are limited.
        QJsonArray tracks = p.timeline.toJson();
        QJsonObject t = tracks[a2].toObject();
        t[QStringLiteral("gain_db")] = 99;
        t[QStringLiteral("pan")] = -5;
        tracks[a2] = t;
        const Timeline limited = Timeline::fromJson(tracks);
        QCOMPARE(limited.tracks[a2].gainDb, TimelineTrack::kMaxGainDb);
        QCOMPARE(limited.tracks[a2].pan, -1.0);
        // Neutral tracks write nothing new.
        QVERIFY(!Timeline::makeDefault().toJson()[0].toObject().contains(QStringLiteral("gain_db")));
    }

    void engine()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        loadVideo(ctl);
        const int a1 = ctl.timeline().indexOf(Kind::Audio, 0);
        const QUuid a1Id = ctl.timeline().tracks[a1].id;
        TimelinePlayer player;
        player.setProject(ctl.project());
        const double full = player.audioLevel(50);
        QVERIFY2(full > 0.05, qPrintable(QString::number(full)));
        // The meters measured the frame: track and master, both channels.
        const EngineServices::MeterLevel m = player.meterLevel(a1Id);
        QVERIFY(m.serial > 0);
        QVERIFY2(m.left > 0.05 && std::abs(m.left - m.right) < 0.02, qPrintable(QString::number(m.left)));
        QVERIFY(player.meterLevel({}).left > 0.05);
        QCOMPARE(player.meterLevel(QUuid::createUuid()).serial, 0);

        // Track fader: -20 dB = a tenth; it adds to the clip volume.
        QVERIFY(ctl.setTrackGain(a1, -20));
        player.setProject(ctl.project());
        QVERIFY(std::abs(player.audioLevel(50) / full - 0.1) < 0.03);
        QVERIFY(ctl.setClipAudio(ctl.timeline().tracks[a1].clips[0].id, -6, 0, 0));
        player.setProject(ctl.project());
        QVERIFY(std::abs(player.audioLevel(50) / full - 0.05) < 0.02);
        ctl.undoStack()->undo();
        QVERIFY(ctl.setTrackGain(a1, 0));

        // Balance: full right leaves the left channel silent; half left halves the right.
        QVERIFY(ctl.setTrackPan(a1, 1));
        player.setProject(ctl.project());
        player.audioLevel(50);
        EngineServices::MeterLevel r = player.meterLevel(a1Id);
        QVERIFY2(r.left < 0.005 && r.right > 0.05, qPrintable(QStringLiteral("%1 %2").arg(r.left).arg(r.right)));
        QVERIFY(ctl.setTrackPan(a1, -0.5));
        player.setProject(ctl.project());
        player.audioLevel(50);
        r = player.meterLevel(a1Id);
        QVERIFY(std::abs(r.right / r.left - 0.5) < 0.05);
        QVERIFY(ctl.setTrackPan(a1, 0));

        // Master: -6 dB halves everything; the track meter is before it.
        QVERIFY(ctl.setMasterGain(-6.02));
        player.setProject(ctl.project());
        QVERIFY(std::abs(player.audioLevel(50) / full - 0.5) < 0.05);
        QVERIFY(std::abs(player.meterLevel({}).left / player.meterLevel(a1Id).left - 0.5) < 0.05);

        // A muted track is silent and its meter stops.
        ctl.setTrackFlag(a1, QStringLiteral("muted"), true);
        player.setProject(ctl.project());
        const qint64 before = player.meterLevel(a1Id).serial;
        QVERIFY(player.audioLevel(50) < 0.01);
        QVERIFY(player.meterLevel(a1Id).serial == before || player.meterLevel(a1Id).left < 0.01);
    }

    void controller()
    {
        ProjectController ctl;
        const Timeline &tl = ctl.timeline();
        const int v1 = tl.indexOf(Kind::Video, 0);
        const int a1 = tl.indexOf(Kind::Audio, 0);
        QVERIFY(!ctl.setTrackGain(v1, -3)); // picture tracks have no volume
        QVERIFY(!ctl.setTrackPan(v1, 0.5));
        QVERIFY(!ctl.setTrackGain(99, -3));
        const int steps = ctl.undoStack()->count();
        for (double db = -1; db >= -10; db -= 1)
            QVERIFY(ctl.setTrackGain(a1, db)); // a fader drag
        QCOMPARE(ctl.undoStack()->count(), steps + 1);
        QVERIFY(!ctl.setTrackGain(a1, -10));
        QVERIFY(ctl.setTrackGain(a1, -500));
        QCOMPARE(tl.tracks[a1].gainDb, TimelineTrack::kMinGainDb);
        QVERIFY(ctl.setTrackPan(a1, 3));
        QCOMPARE(tl.tracks[a1].pan, 1.0);
        QVERIFY(ctl.setMasterGain(4.04));
        QCOMPARE(tl.masterGainDb, 4.0);
        ctl.undoStack()->undo();
        QCOMPARE(tl.masterGainDb, 0.0);
    }

    void panel()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        auto *mixer = w.findChild<AudioMixer *>(QStringLiteral("audioMixer"));
        QVERIFY(mixer);
        w.findChild<QTabWidget *>(QStringLiteral("rightTabs"))->setCurrentWidget(mixer);
        for (const QString &k : {"A1", "A2", "A3", "master"})
            QVERIFY2(mixer->findChild<QSlider *>(QStringLiteral("mixerFader_") + k), qPrintable(k));
        QVERIFY(!mixer->findChild<QSlider *>(QStringLiteral("mixerFader_V1")));
        QVERIFY(!mixer->findChild<QWidget *>(QStringLiteral("mixerEmpty"))->isVisibleTo(mixer));

        const int a2 = ctl->timeline().indexOf(Kind::Audio, 1);
        auto *fader = mixer->findChild<QSlider *>(QStringLiteral("mixerFader_A2"));
        auto *spin = mixer->findChild<QDoubleSpinBox *>(QStringLiteral("mixerGain_A2"));
        const int steps = ctl->undoStack()->count();
        for (int v = -10; v >= -60; v -= 10)
            fader->setValue(v); // -1 .. -6 dB
        QCOMPARE(ctl->timeline().tracks[a2].gainDb, -6.0);
        QCOMPARE(spin->value(), -6.0);
        QCOMPARE(ctl->undoStack()->count(), steps + 1);
        spin->setValue(3.5);
        QCOMPARE(ctl->timeline().tracks[a2].gainDb, 3.5);
        QCOMPARE(fader->value(), 35);

        mixer->findChild<QSlider *>(QStringLiteral("mixerPan_A2"))->setValue(-30);
        QCOMPARE(ctl->timeline().tracks[a2].pan, -0.3);
        QCOMPARE(AudioMixer::panText(-0.3), AudioMixer::tr("L %1").arg(30));
        QCOMPARE(AudioMixer::panText(0), AudioMixer::tr("C"));

        auto *mute = mixer->findChild<QToolButton *>(QStringLiteral("mixerMute_A2"));
        mute->click();
        QVERIFY(ctl->timeline().tracks[a2].muted);
        mixer->findChild<QToolButton *>(QStringLiteral("mixerSolo_A2"))->click();
        QVERIFY(ctl->timeline().tracks[a2].solo);
        // The timeline's M button and undo update the mixer too.
        ctl->setTrackFlag(a2, QStringLiteral("muted"), false);
        QVERIFY(!mute->isChecked());
        ctl->undoStack()->undo();
        QVERIFY(mute->isChecked());

        mixer->findChild<QDoubleSpinBox *>(QStringLiteral("mixerGain_master"))->setValue(-3);
        QCOMPARE(ctl->timeline().masterGainDb, -3.0);
        QCOMPARE(mixer->findChild<QSlider *>(QStringLiteral("mixerFader_master"))->value(), -30);

        // A new audio track gets a strip.
        ctl->addTrack(Kind::Audio);
        QVERIFY(mixer->findChild<QSlider *>(QStringLiteral("mixerFader_A4")));

        // Stopped: meters rest at the bottom.
        mixer->updateMeters();
        QCOMPARE(mixer->findChild<LevelMeter *>(QStringLiteral("mixerMeter_master"))->left(), LevelMeter::kFloorDb);
    }

    void metersWhilePlaying()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        if (!ctl->canPlay())
            QSKIP("video engine not available");
        loadVideo(*ctl);
        auto *mixer = w.findChild<AudioMixer *>(QStringLiteral("audioMixer"));
        w.findChild<QTabWidget *>(QStringLiteral("rightTabs"))->setCurrentWidget(mixer);
        if (!ctl->play())
            QSKIP("playback not available here");
        auto *a1 = mixer->findChild<LevelMeter *>(QStringLiteral("mixerMeter_A1"));
        auto *master = mixer->findChild<LevelMeter *>(QStringLiteral("mixerMeter_master"));
        QTRY_VERIFY_WITH_TIMEOUT(a1->left() > -20 && master->left() > -20, 5000);
        // A2 has nothing on it.
        QVERIFY(mixer->findChild<LevelMeter *>(QStringLiteral("mixerMeter_A2"))->left() < -50);
        ctl->pause();
        QTRY_VERIFY_WITH_TIMEOUT(a1->left() <= LevelMeter::kFloorDb, 5000);
    }
};

QTEST_MAIN(TestPhase10a)
#include "tst_phase10a.moc"
