// Phase 14 (part 1) acceptance tests: scene change detection (offline, no AI
// model): the cuts of a test video with three shots, splitting and markers.
#include "app/ProjectController.h"
#include "app/SceneDialog.h"
#include "core/MediaCache.h"
#include "core/SceneDetect.h"
#include "engine/TimelinePlayer.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

#include <cmath>

using namespace bc;

namespace {

// Three shots of 2 s each (25 fps): a moving test pattern, colour bars, a
// moving mandelbrot; plus a tone, so the clip has linked sound.
bool makeShots(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc=size=320x180:rate=25:duration=2"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("smptebars=size=320x180:rate=25:duration=2"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("mandelbrot=size=320x180:rate=25"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=440:sample_rate=48000:duration=6"),
             QStringLiteral("-filter_complex"),
             QStringLiteral("[2:v]trim=duration=2,setpts=PTS-STARTPTS[m];[0:v][1:v][m]concat=n=3:v=1:a=0,format=yuv420p[v]"),
             QStringLiteral("-map"), QStringLiteral("[v]"), QStringLiteral("-map"), QStringLiteral("3:a"),
             QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("3"),
             QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), path});
    if (!p.waitForFinished(60000) || p.exitCode() != 0)
        qWarning() << p.readAllStandardError();
    return p.exitCode() == 0 && QFile::exists(path);
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

// Mean absolute difference per channel (0..255).
double meanDiff(const QImage &a, const QImage &b)
{
    if (a.isNull() || a.size() != b.size())
        return 255;
    const QImage x = a.convertToFormat(QImage::Format_RGB888), y = b.convertToFormat(QImage::Format_RGB888);
    double sum = 0;
    qint64 n = 0;
    for (int r = 0; r < x.height(); r += 4) {
        const uchar *p = x.constScanLine(r), *q = y.constScanLine(r);
        for (int i = 0; i < x.width() * 3; i += 3, ++n)
            sum += std::abs(int(p[i]) - int(q[i])) + std::abs(int(p[i + 1]) - int(q[i + 1])) + std::abs(int(p[i + 2]) - int(q[i + 2]));
    }
    return n ? sum / (3.0 * n) : 255;
}

bool analyzed(ProjectController &ctl, const QList<QUuid> &clips)
{
    if (ctl.scenesKnown(clips))
        return true;
    QSignalSpy done(&ctl, &ProjectController::scenesAnalyzed);
    ctl.analyzeScenes(clips);
    for (int i = 0; i < 600 && !ctl.scenesKnown(clips); ++i)
        done.wait(200);
    return ctl.scenesKnown(clips);
}

} // namespace

class TestPhase14a : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;

    QList<QUuid> place(ProjectController &ctl, qint64 start)
    {
        if (ctl.project().items.isEmpty()) {
            ctl.importFiles({m_video});
            waitIdle(ctl);
        }
        return ctl.placeMedia({ctl.project().items.first().id}, 0, start);
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase14a"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("shots.mkv"));
        QVERIFY(makeShots(m_video));
    }

    void difference()
    {
        std::vector<unsigned char> a(SceneDetect::kWidth * SceneDetect::kHeight * 3, 40), b = a;
        QCOMPARE(SceneDetect::difference(a.data(), b.data()), 0.f);
        std::fill(b.begin(), b.end(), 220);
        QVERIFY(SceneDetect::difference(a.data(), b.data()) > 0.9f);
        b = a;
        for (size_t i = 0; i < b.size(); i += 7)
            b[i] = 46; // a little noise
        QVERIFY(SceneDetect::difference(a.data(), b.data()) < 0.1f);
    }

    void findsTheCuts()
    {
        SceneDetect::Profile p;
        QString err;
        QVERIFY2(SceneDetect::compute(m_video, p, &err), qPrintable(err));
        QVERIFY(std::abs(int(p.timeUs.size()) - 150) <= 2);
        for (double s : {0.2, 0.5, 0.9}) {
            const QList<qint64> cuts = SceneDetect::cuts(p, s);
            QVERIFY2(cuts.size() == 2, qPrintable(QStringLiteral("sensitivity %1: %2 cuts").arg(s).arg(cuts.size())));
            QVERIFY(std::abs(cuts[0] - 2000000) <= 40000);
            QVERIFY(std::abs(cuts[1] - 4000000) <= 40000);
        }

        // Saved and loaded unchanged.
        const QString file = m_dir.filePath(QStringLiteral("p.bcsc"));
        QVERIFY(SceneDetect::save(file, p));
        SceneDetect::Profile q;
        QVERIFY(SceneDetect::load(file, q));
        QCOMPARE(q.timeUs, p.timeUs);
        QCOMPARE(q.score, p.score);

        // A steady picture has no cuts, a lone flash frame neither (needs a gap).
        SceneDetect::Profile flat;
        for (int i = 0; i < 100; ++i) {
            flat.timeUs.push_back(i * 40000);
            flat.score.push_back(i == 0 ? 0.f : 0.02f);
        }
        QVERIFY(SceneDetect::cuts(flat, 1.0).isEmpty());
    }

    void splitAndMark()
    {
        ProjectController ctl;
        const QList<QUuid> placed = place(ctl, 50);
        QCOMPARE(placed.size(), qsizetype(2)); // video + linked sound
        QVERIFY(analyzed(ctl, placed));
        QCOMPARE(ctl.sceneClips(placed).size(), qsizetype(1));
        const QUuid video = ctl.sceneClips(placed).first();
        const QList<qint64> cuts = ctl.sceneCuts(video, 0.5);
        // The new shot shows from the cut frame on, as played (the file's sound
        // starts a little before its picture, so file time 0 is not frame 0).
        QCOMPARE(cuts.size(), qsizetype(2));
        if (ctl.canPlay()) {
            TimelinePlayer player;
            player.setProject(ctl.project());
            for (qint64 f : cuts) {
                const QImage before = player.renderFrame(f - 1), at = player.renderFrame(f),
                             later = player.renderFrame(f + 10);
                QVERIFY(meanDiff(at, later) < meanDiff(before, later));
                QVERIFY2(meanDiff(before, at) > 30, qPrintable(QStringLiteral("frame %1").arg(f)));
            }
        }
        qInfo() << "cuts" << cuts;
        QVERIFY(std::abs(cuts[0] - 100) <= 1 && std::abs(cuts[1] - 150) <= 1); // 2 s and 4 s after the start at 50
        QCOMPARE(ctl.sceneCutCount(placed, 0.5), 2);

        // Markers: one step.
        const int undoBefore = ctl.undoStack()->count();
        QCOMPARE(ctl.markScenes(placed, 0.5), 2);
        QCOMPARE(ctl.undoStack()->count(), undoBefore + 1);
        QCOMPARE(ctl.timeline().markers.size(), qsizetype(2));
        QCOMPARE(ctl.markScenes(placed, 0.5), 0); // already there
        ctl.undoStack()->undo();
        QVERIFY(ctl.timeline().markers.isEmpty());

        // Split: 3 video and 3 audio clips, sound cut at the same frames.
        const qint64 end = ctl.timeline().clip(video)->end();
        QCOMPARE(ctl.splitAtScenes(placed, 0.5), 2);
        QCOMPARE(ctl.undoStack()->count(), undoBefore + 1);
        for (const TimelineTrack &t : ctl.timeline().tracks) {
            if (t.clips.isEmpty())
                continue;
            QCOMPARE(t.clips.size(), qsizetype(3));
            QCOMPARE(t.clips[0].start, qint64(50));
            QCOMPARE(t.clips[1].start, cuts[0]);
            QCOMPARE(t.clips[1].in, cuts[0] - 50);
            QCOMPARE(t.clips[2].start, cuts[1]);
            QCOMPARE(t.clips[2].end(), end);
        }
        ctl.undoStack()->undo();
        for (const TimelineTrack &t : ctl.timeline().tracks)
            QVERIFY(t.clips.size() <= 1);

        // A trimmed clip: only the cut inside it counts.
        QVERIFY(ctl.splitClips(120, {video}));
        QCOMPARE(ctl.sceneCuts(video, 0.5), (QList<qint64>{cuts[0]}));

        // A second controller reads the cached result.
        ProjectController other;
        const QList<QUuid> again = place(other, 0);
        QVERIFY(analyzed(other, again));
        QCOMPARE(other.sceneCuts(other.sceneClips(again).first(), 0.5), (QList<qint64>{cuts[0] - 50, cuts[1] - 50}));
    }

    void dialog()
    {
        ProjectController ctl;
        const QList<QUuid> placed = place(ctl, 0);
        QSignalSpy done(&ctl, &ProjectController::scenesAnalyzed);
        QDir(MediaCache::folder(MediaCache::Kind::Scenes)).removeRecursively(); // read the video again
        SceneDialog dlg(&ctl, placed);
        auto *apply = dlg.findChild<QPushButton *>(QStringLiteral("sceneApply"));
        auto *result = dlg.findChild<QLabel *>(QStringLiteral("sceneResult"));
        auto *mode = dlg.findChild<QComboBox *>(QStringLiteral("sceneMode"));
        auto *slider = dlg.findChild<QSlider *>(QStringLiteral("sceneSensitivity"));
        QVERIFY(apply && result && mode && slider);
        QVERIFY(ctl.isAnalyzingScenes());
        QVERIFY(!apply->isEnabled());
        QVERIFY(done.wait(60000));
        QVERIFY(apply->isEnabled());
        QVERIFY(result->text().contains(QLatin1Char('2')));
        mode->setCurrentIndex(1);
        apply->click();
        QCOMPARE(ctl.timeline().markers.size(), qsizetype(2));

        // Nothing selectable: told so.
        SceneDialog none(&ctl, {});
        QVERIFY(!none.findChild<QPushButton *>(QStringLiteral("sceneApply"))->isEnabled());
    }
};

QTEST_MAIN(TestPhase14a)
#include "tst_phase14a.moc"
