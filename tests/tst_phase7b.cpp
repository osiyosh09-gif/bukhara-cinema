// Phase 7 (part 2) acceptance tests: clip speed (model, trims at speed,
// controller with ripple and undo, file, engine, dialog).
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/SpeedDialog.h"
#include "app/TimelinePanel.h"
#include "core/MediaCache.h"
#include "engine/TimelinePlayer.h"

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

using namespace bc;
using Kind = Timeline::Kind;

namespace {

bool makeVideo(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-f"),
             QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc=size=320x240:rate=25:duration=4"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("sine=frequency=440:sample_rate=48000:duration=4"), QStringLiteral("-c:v"),
             QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("2"), QStringLiteral("-c:a"),
             QStringLiteral("aac"), QStringLiteral("-shortest"), path});
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

double grey(const QImage &img)
{
    const QImage a = img.convertToFormat(QImage::Format_RGB32);
    double sum = 0;
    int n = 0;
    for (int y = 0; y < a.height(); y += 3)
        for (int x = 0; x < a.width(); x += 3) {
            sum += qGray(a.pixel(x, y));
            ++n;
        }
    return n ? sum / n : 0;
}

const TimelineClip &at(const Timeline &t, Kind kind, int n, int index)
{
    return t.tracks[t.indexOf(kind, n)].clips[index];
}

} // namespace

class TestPhase7b : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;

    // V1 + A1: the 100-frame clip twice, 0..200.
    QUuid twoClips(ProjectController &ctl)
    {
        ctl.importFiles({m_video});
        waitIdle(ctl);
        const QUuid media = ctl.project().items.first().id;
        ctl.placeMedia({media, media}, 0, 0);
        return at(ctl.timeline(), Kind::Video, 0, 0).id;
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase7b"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("clip.mp4"));
        QVERIFY(makeVideo(m_video));
    }

    void timelineAtSpeed()
    {
        const QUuid media = QUuid::createUuid();
        Timeline t = Timeline::makeDefault();
        TimelineClip c;
        c.id = QUuid::createUuid();
        c.mediaId = media;
        c.start = 0;
        c.in = 10;
        c.length = 40;
        c.speed = 2.0;
        t.tracks[0].clips << c;
        auto frames = [](const QUuid &) { return qint64(100); };

        // Split: the right part starts 2 source frames per timeline frame later.
        const QList<QUuid> made = t.splitAt(15, {c.id});
        QCOMPARE(made.size(), 1);
        QCOMPARE(t.clip(made.first())->in, qint64(10 + 30));
        t.remove(made, false);

        // Tail trim stops where the source ends: (100 - 10) / 2 = 45 frames.
        QVERIFY(t.trim(c.id, false, 200, frames));
        QCOMPARE(t.clip(c.id)->length, qint64(45));
        // Head trim to the left reaches source frame 0: 10 / 2 = 5 frames.
        t.tracks[0].clips[0].start = 20;
        QVERIFY(t.trim(c.id, true, 0, frames));
        QCOMPARE(t.clip(c.id)->start, qint64(15));
        QCOMPARE(t.clip(c.id)->in, qint64(0));

        // Slip moves the source by 2 frames per frame, within the media.
        QVERIFY(t.trim(c.id, false, 55, frames));
        QCOMPARE(t.clip(c.id)->length, qint64(40));
        QVERIFY(t.slip(c.id, 3, frames));
        QCOMPARE(t.clip(c.id)->in, qint64(6));

        // Saved and read again.
        QCOMPARE(Timeline::fromJson(t.toJson()).clip(c.id)->speed, 2.0);
    }

    void controllerSpeedRippleUndo()
    {
        ProjectController ctl;
        const QUuid first = twoClips(ctl);
        const QUuid second = at(ctl.timeline(), Kind::Video, 0, 1).id;
        const Timeline before = ctl.timeline();
        QVERIFY(ctl.canChangeSpeed(first));

        // Twice as fast: half as long, sound too; the gap stays.
        QVERIFY(ctl.setClipSpeed(first, 2.0));
        QCOMPARE(at(ctl.timeline(), Kind::Video, 0, 0).length, qint64(50));
        QCOMPARE(at(ctl.timeline(), Kind::Audio, 0, 0).length, qint64(50));
        QCOMPARE(at(ctl.timeline(), Kind::Audio, 0, 0).speed, 2.0);
        QCOMPARE(ctl.timeline().clip(second)->start, qint64(100));
        ctl.undoStack()->undo();
        QVERIFY(ctl.timeline() == before);

        // Half speed without ripple: the clip cannot grow over the next one.
        QVERIFY(ctl.setClipSpeed(first, 0.5));
        QCOMPARE(at(ctl.timeline(), Kind::Video, 0, 0).length, qint64(100));
        QCOMPARE(at(ctl.timeline(), Kind::Video, 0, 0).speed, 0.5);
        ctl.undoStack()->undo();
        // With ripple it grows and the next clips move.
        QVERIFY(ctl.setClipSpeed(first, 0.5, 0, true));
        QCOMPARE(at(ctl.timeline(), Kind::Video, 0, 0).length, qint64(200));
        QCOMPARE(ctl.timeline().clip(second)->start, qint64(200));
        QCOMPARE(at(ctl.timeline(), Kind::Audio, 0, 1).start, qint64(200));
        ctl.undoStack()->undo();

        // A length that the source cannot fill is shortened: 100 frames at 4x = 25.
        QVERIFY(ctl.setClipSpeed(first, 4.0, 80));
        QCOMPARE(at(ctl.timeline(), Kind::Video, 0, 0).length, qint64(25));
        ctl.undoStack()->undo();

        // Titles have no speed.
        const QUuid title = ctl.addTitle(300);
        QVERIFY(!ctl.canChangeSpeed(title));
        QVERIFY(!ctl.setClipSpeed(title, 2.0));
    }

    void engineUsesTheRightFrames()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        // A clip whose grey level grows by 2 every frame: the brightness says which frame shows.
        const QString ramp = m_dir.filePath(QStringLiteral("ramp.mp4"));
        QProcess p;
        p.start(QStringLiteral(BC_FFMPEG_EXE),
                {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-f"),
                 QStringLiteral("lavfi"), QStringLiteral("-i"),
                 QStringLiteral("nullsrc=s=160x120:r=25:d=4,geq=lum='16+N*2':cb=128:cr=128"), QStringLiteral("-f"),
                 QStringLiteral("lavfi"), QStringLiteral("-i"),
                 QStringLiteral("sine=frequency=440:sample_rate=48000:duration=4"), QStringLiteral("-c:v"),
                 QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("1"), QStringLiteral("-g"),
                 QStringLiteral("1"), QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), ramp});
        QVERIFY(p.waitForFinished(120000) && p.exitCode() == 0);
        ctl.importFiles({ramp});
        waitIdle(ctl);
        const QUuid media = ctl.project().items.first().id;
        ctl.placeMedia({media}, 0, 0);
        const QUuid clip = at(ctl.timeline(), Kind::Video, 0, 0).id;

        TimelinePlayer normal;
        normal.setPreviewLimit(QSize(160, 90));
        normal.setProject(ctl.project());
        QList<double> level;
        for (int k = 0; k < 30; ++k)
            level << grey(normal.renderFrame(k));
        QVERIFY2(level[20] > level[10] + 10, qPrintable(QStringLiteral("%1 %2").arg(level[10]).arg(level[20])));
        auto whichFrame = [&](const QImage &img) {
            const double g = grey(img);
            int best = 0;
            for (int k = 1; k < level.size(); ++k)
                if (qAbs(level[k] - g) < qAbs(level[best] - g))
                    best = k;
            return best;
        };

        QVERIFY(ctl.setClipSpeed(clip, 2.0));
        TimelinePlayer fast;
        fast.setPreviewLimit(QSize(160, 90));
        fast.setProject(ctl.project());
        QCOMPARE(whichFrame(fast.renderFrame(10)), 20);
        QVERIFY2(fast.audioLevel(20) > 0.05, qPrintable(QString::number(fast.audioLevel(20))));

        ctl.undoStack()->undo();
        QVERIFY(ctl.setClipSpeed(clip, 0.5));
        TimelinePlayer slow;
        slow.setPreviewLimit(QSize(160, 90));
        slow.setProject(ctl.project());
        QCOMPARE(whichFrame(slow.renderFrame(20)), 10);
        QVERIFY(slow.audioLevel(40) > 0.05);
    }

    void speedDialogInTheWindow()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        const QUuid first = twoClips(*ctl);
        auto *panel = w.findChild<TimelinePanel *>();
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        auto *act = w.findChild<QAction *>(QStringLiteral("actSpeed"));
        QVERIFY(act && !act->property("bc_planned").toBool());
        QVERIFY(!act->isEnabled());
        view->setSelection({first});
        QVERIFY(act->isEnabled());
        QCOMPARE(panel->speedClip(), first);

        SpeedDialog dlg(ctl, first, &w);
        auto *speed = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("speedPercent"));
        auto *duration = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("speedDuration"));
        QCOMPARE(speed->value(), 100.0);
        QCOMPARE(duration->value(), 4.0);
        speed->setValue(200);
        QCOMPARE(duration->value(), 2.0); // follows the speed
        duration->setValue(1.0);
        QCOMPARE(speed->value(), 400.0); // and the other way
        QVERIFY(dlg.apply());
        QCOMPARE(at(ctl->timeline(), Kind::Video, 0, 0).length, qint64(25));
        QCOMPARE(at(ctl->timeline(), Kind::Video, 0, 0).speed, 4.0);
    }
};

QTEST_MAIN(TestPhase7b)
#include "tst_phase7b.moc"
