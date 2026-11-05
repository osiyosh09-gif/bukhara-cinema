// Deep check of multicamera editing and export the way a user meets them:
// files and folders named in Uzbek Cyrillic (letters outside the Windows code
// page), a Full HD sequence, the menu command enabled from the timeline, sync
// by sound, camera switching, the automatic edit, and export through every
// video codec found on this computer (each file must have picture and sound
// and the full length). A codec that stops is replaced by the one that needs
// no hardware.
#include "app/MainWindow.h"
#include "app/MediaBinPanel.h"
#include "app/MulticamDialog.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/AudioSync.h"
#include "core/MediaCache.h"
#include "core/MediaProbe.h"
#include "engine/Exporter.h"

#include <QAction>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace bc;
using Kind = Timeline::Kind;

namespace {

bool ffmpeg(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE), QStringList{QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error")} + args);
    return p.waitForFinished(120000) && p.exitCode() == 0;
}

void waitIdle(ProjectController &ctl)
{
    for (int i = 0; i < 200; ++i) {
        ctl.waitForBackgroundWork();
        bool probing = false;
        for (const MediaItem &m : ctl.project().items)
            probing |= ctl.isProbing(m.id);
        if (!probing)
            return;
        QTest::qWait(50);
    }
}

// Speech-like envelope: bursts of random length and level with pauses.
std::vector<float> bursts(int seconds, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    std::vector<float> env;
    while (int(env.size()) < seconds * AudioSync::kRate) {
        const int len = 5 + int(u(rng) * 40);
        const float level = u(rng) < 0.35f ? 0.002f : 0.05f + u(rng) * 0.4f;
        for (int i = 0; i < len; ++i)
            env.push_back(level * (0.8f + 0.4f * u(rng)));
    }
    env.resize(size_t(seconds) * AudioSync::kRate);
    return env;
}

// 20 s mono 48 kHz WAV: noise whose level follows bursts(), sounding in the
// first second (so every part of it has sound soon).
bool writeBurstsWav(const QString &path, int seconds)
{
    const int rate = 48000;
    std::vector<float> env = bursts(seconds, 42);
    for (int i = 0; i < AudioSync::kRate; ++i)
        env[size_t(i)] = 0.3f;
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    QByteArray pcm;
    const int n = rate * seconds;
    pcm.resize(n * 2);
    auto *out = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < n; ++i) {
        const float level = env[size_t(std::min<qint64>(qint64(env.size()) - 1, qint64(i) * AudioSync::kRate / rate))];
        out[i] = qint16(std::clamp(level * 2.f * u(rng), -1.f, 1.f) * 32767);
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream s(&f);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData("RIFF", 4);
    s << quint32(36 + pcm.size());
    s.writeRawData("WAVEfmt ", 8);
    s << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
    s.writeRawData("data", 4);
    s << quint32(pcm.size());
    s.writeRawData(pcm.constData(), int(pcm.size()));
    return true;
}

// Exports and checks the file: picture, sound, length.
void exportAndCheck(ProjectController &ctl, const QString &path, const MediaProbe::VideoEncoder &enc, QString *note)
{
    ExportSettings s;
    s.path = path;
    s.encoder = enc;
    s.videoKbps = 4000;
    QSignalSpy done(&ctl, &ProjectController::exportFinished);
    QString error;
    QVERIFY2(ctl.startExport(s, &error), qPrintable(enc.codec + QStringLiteral(": ") + error));
    QVERIFY2(done.wait(300000), qPrintable(enc.codec));
    QVERIFY2(done.first().at(0).toBool(), qPrintable(enc.codec + QStringLiteral(": ") + done.first().at(1).toString()));
    *note = ctl.exportNote();
    MediaInfo info;
    QVERIFY2(MediaProbe::probe(path, info, &error), qPrintable(enc.codec + QStringLiteral(": ") + error));
    QVERIFY2(info.hasVideo(), qPrintable(enc.codec));
    QVERIFY2(info.hasAudio(), qPrintable(enc.codec));
    QCOMPARE(info.width, ctl.project().sequence.width);
    QCOMPARE(info.height, ctl.project().sequence.height);
    const double expected = double(ctl.timeline().duration()) / ctl.project().sequence.fps() * 1e6;
    QVERIFY2(std::abs(double(info.durationUs) - expected) < 300000,
             qPrintable(QStringLiteral("%1: %2 us, expected %3").arg(enc.codec).arg(info.durationUs).arg(expected)));
    const QImage frame = MediaProbe::frameAt(path, 1000000, 320, 180);
    QVERIFY2(!frame.isNull(), qPrintable(enc.codec));
}

} // namespace

class TestDeepCheck : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_dir; // "Ўзбекча қўшиқ ғҳ"
    QString m_camA, m_camB;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_deepcheck"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_tmp.isValid());
        m_dir = m_tmp.filePath(QStringLiteral("Ўзбекча қўшиқ ғҳ"));
        QVERIFY(QDir().mkpath(m_dir));
        // Two cameras of one event: the same speech-like sound, B starts 3 s later.
        const QString work = m_tmp.filePath(QStringLiteral("work"));
        QVERIFY(QDir().mkpath(work));
        const QString master = work + QStringLiteral("/event.wav");
        QVERIFY(writeBurstsWav(master, 16));
        m_camA = m_dir + QStringLiteral("/Камера ўнг.mkv");
        m_camB = m_dir + QStringLiteral("/Камера чап ҳ.mkv");
        // Written to ASCII names first: the ffmpeg program on Windows may not take the letters.
        QVERIFY(ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("testsrc=size=640x360:rate=25:duration=12"), QStringLiteral("-i"), master,
                        QStringLiteral("-t"), QStringLiteral("12"), QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
                        QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), work + QStringLiteral("/a.mkv")}));
        QVERIFY(ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("color=c=blue:size=640x360:rate=25:duration=10"), QStringLiteral("-ss"),
                        QStringLiteral("3"), QStringLiteral("-i"), master, QStringLiteral("-t"), QStringLiteral("10"),
                        QStringLiteral("-af"), QStringLiteral("volume=0.6"), QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
                        QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), work + QStringLiteral("/b.mkv")}));
        QVERIFY(QFile::copy(work + QStringLiteral("/a.mkv"), m_camA));
        QVERIFY(QFile::copy(work + QStringLiteral("/b.mkv"), m_camB));
    }

    void multicamFromTimelineAndExport()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        QVERIFY2(ctl->canPlay(), qPrintable(ctl->engineError()));
        SequenceSettings seq = ctl->project().sequence;
        seq.width = 1920;
        seq.height = 1080;
        ctl->setSequence(seq);

        // Selected in the bin while still being read: the command turns on when reading ends.
        auto *bin = w.findChild<MediaBinPanel *>(QStringLiteral("mediaBin"));
        QAction *act = w.findChild<QAction *>(QStringLiteral("actCreateMulticam"));
        QVERIFY(bin && act);
        QCOMPARE(ctl->importFiles({m_camA, m_camB}), 2);
        bin->tree()->selectAll();
        waitIdle(*ctl);
        QTest::qWait(100);
        QVERIFY2(act->isEnabled(), "selected before probing ended");
        QList<QUuid> ids(2);
        for (const MediaItem &m : ctl->project().items) {
            QVERIFY2(m.probed && m.info.hasVideo() && m.info.hasAudio(), qPrintable(m.path));
            ids[m.path.endsWith(QStringLiteral("ҳ.mkv")) ? 1 : 0] = m.id;
        }

        // One file selected: off, and the tool tip says how many are selected.
        bin->tree()->clearSelection();
        bin->tree()->topLevelItem(0)->setSelected(true);
        QVERIFY(!act->isEnabled());
        QVERIFY2(act->toolTip().count(QLatin1Char('\n')) >= 1, qPrintable(act->toolTip()));

        // Nothing in the bin, both clips selected on the timeline: also on.
        bin->tree()->clearSelection();
        QVERIFY(!act->isEnabled());
        const QUuid a = ctl->placeMedia({ids[0]}, 0, 0).value(0);
        const QUuid b = ctl->placeMedia({ids[1]}, 1, 0).value(0);
        QVERIFY(!a.isNull() && !b.isNull());
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        QVERIFY(view);
        view->setSelection({a, b});
        QTest::qWait(50);
        QVERIFY2(act->isEnabled(), "clips selected on the timeline");
        QCOMPARE(bin->multicamMedia(), ids);
        QVERIFY(!act->toolTip().isEmpty());

        // Sync by sound with the Uzbek names (B is 3 s later).
        ctl->setPlayhead(0); // the multicamera clip replaces camera A on the first tracks
        MulticamDialog dlg(ctl, ids, &w);
        dlg.setSyncBySound(true);
        QSignalSpy made(&dlg, &MulticamDialog::finished);
        dlg.start();
        QVERIFY(made.wait(60000));
        QVERIFY2(made.first().first().toBool(), "sound did not match");
        QVERIFY2(std::abs(dlg.offsets().value(1) - 3.0) < 0.05, qPrintable(QString::number(dlg.offsets().value(1))));
        const NestedSequence *nest = ctl->project().nest(dlg.result());
        QVERIFY(nest && nest->multicam);
        QCOMPARE(nest->timeline.tracks[nest->timeline.indexOf(Kind::Video, 1)].clips.first().start, 75);

        // Only the multicamera clip stays on the timeline.
        QList<QUuid> rest;
        for (const TimelineTrack &t : ctl->timeline().tracks)
            for (const TimelineClip &c : t.clips)
                if (c.nestId.isNull())
                    rest << c.id;
        QVERIFY(!rest.isEmpty()); // camera B on the second tracks
        QVERIFY(ctl->removeClips(rest, false));
        const TimelineTrack &v1 = ctl->timeline().tracks[ctl->timeline().indexOf(Kind::Video, 0)];
        QUuid mc;
        for (const TimelineClip &c : v1.clips)
            if (c.nestId == dlg.result())
                mc = c.id;
        QVERIFY(!mc.isNull());

        // Switch cameras at the playhead (keys 1-9).
        const TimelineClip *clip = ctl->timeline().clip(mc);
        ctl->setPlayhead(clip->start + 125);
        QVERIFY(ctl->switchMulticamAngle(1));
        QAction *cam1 = w.findChild<QAction *>(QStringLiteral("actMulticamCamera1"));
        QVERIFY(cam1 && cam1->isEnabled());
        ctl->setPlayhead(ctl->timeline().clip(mc)->start + 50);
        cam1->trigger();

        // Automatic edit by who speaks (sound read first).
        const QList<QUuid> clips = ctl->autoMulticamClips({mc});
        QVERIFY(!clips.isEmpty());
        if (!ctl->multicamSoundKnown(clips)) {
            QSignalSpy heard(ctl, &ProjectController::multicamSoundReady);
            ctl->analyzeMulticamSound(clips);
            QVERIFY(heard.wait(60000));
            QVERIFY(heard.first().first().toBool());
        }
        AutoMulticam::Settings rhythm;
        rhythm.mode = AutoMulticam::Mode::Rhythm;
        rhythm.minShot = 1;
        rhythm.maxShot = 2;
        QVERIFY(ctl->applyAutoMulticam(clips, rhythm) > 0);

        // Export through every codec this computer has, into the Uzbek folder.
        const QList<MediaProbe::VideoEncoder> encoders = MediaProbe::usableVideoEncoders();
        QVERIFY(!encoders.isEmpty());
        for (const MediaProbe::VideoEncoder &enc : encoders) {
            const QString out = m_dir + QStringLiteral("/Лойиҳа ўқув %1.mp4").arg(enc.codec);
            QString note;
            exportAndCheck(*ctl, out, enc, &note);
            if (QTest::currentTestFailed())
                return;
            qInfo("export with %s: ok%s", qPrintable(enc.codec), note.isEmpty() ? "" : qPrintable(QStringLiteral(" (") + note + QLatin1Char(')')));
        }

        // A codec that opens but cannot write this picture size (H.261 knows
        // only 176x144 and 352x288): written again with the last codec, said so.
        MediaProbe::VideoEncoder broken{QStringLiteral("h261"), QStringLiteral("H.261"), QStringLiteral("yuv420p"), false};
        QString note;
        exportAndCheck(*ctl, m_dir + QStringLiteral("/Синов ҳ261.mp4"), broken, &note);
        QVERIFY2(!note.isEmpty(), "the stopped codec was not replaced");
        qInfo("fallback: %s", qPrintable(note));
    }

    // The owner's way: clips stacked on V1..V3 and nested (Clip > Nest); the
    // nested clip selected, the command makes that sequence a multicamera one.
    void nestedClipBecomesMulticam()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        QCOMPARE(ctl->importFiles({m_camA, m_camB}), 2);
        waitIdle(*ctl);
        const QList<QUuid> va = ctl->placeMedia({ctl->project().items[0].id}, 0, 0);
        const QList<QUuid> vb = ctl->placeMedia({ctl->project().items[1].id}, 1, 0);
        QVERIFY(!va.isEmpty() && !vb.isEmpty());
        QList<QUuid> all;
        for (const TimelineTrack &t : ctl->timeline().tracks)
            for (const TimelineClip &c : t.clips)
                all << c.id;
        const QUuid nestId = ctl->nestClips(all);
        QVERIFY(!nestId.isNull());
        QVERIFY(!ctl->isMulticam(nestId));
        QUuid nestClip;
        for (const TimelineTrack &t : ctl->timeline().tracks)
            for (const TimelineClip &c : t.clips)
                if (c.nestId == nestId && t.kind == Kind::Video)
                    nestClip = c.id;
        QVERIFY(!nestClip.isNull());

        auto *bin = w.findChild<MediaBinPanel *>(QStringLiteral("mediaBin"));
        bin->tree()->clearSelection();
        QAction *act = w.findChild<QAction *>(QStringLiteral("actCreateMulticam"));
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        view->setSelection({nestClip});
        QTest::qWait(50);
        QVERIFY2(act->isEnabled(), "a selected nested clip with two cameras");
        act->trigger();
        QVERIFY(ctl->isMulticam(nestId));
        QCOMPARE(ctl->multicamAngles(nestId, Kind::Video).size(), qsizetype(2));
        QVERIFY(!act->isEnabled()); // already a multicamera one
        ctl->setPlayhead(50);
        QAction *cam2 = w.findChild<QAction *>(QStringLiteral("actMulticamCamera2"));
        QVERIFY(cam2 && cam2->isEnabled());
        cam2->trigger();
        bool switched = false;
        for (const TimelineTrack &t : ctl->timeline().tracks)
            for (const TimelineClip &c : t.clips)
                switched |= c.nestId == nestId && t.kind == Kind::Video && c.angle == 1;
        QVERIFY(switched);
        ctl->undoStack()->undo(); // the switch
        ctl->undoStack()->undo(); // multicamera again a plain nested sequence
        QVERIFY(!ctl->isMulticam(nestId));
    }
};

QTEST_MAIN(TestDeepCheck)
#include "tst_deepcheck.moc"
