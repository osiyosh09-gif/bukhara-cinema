// Phase 12 (part 2) acceptance tests: multicamera monitor (grid of all
// angles) and switching angles at the playhead (keys, menu, clicks).
#include "app/MainWindow.h"
#include "app/MulticamMonitor.h"
#include "app/ProjectController.h"
#include "core/MediaCache.h"
#include "core/Multicam.h"

#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

using namespace bc;
using Kind = Timeline::Kind;

namespace {

bool makeCamera(const QString &path, const QString &color, int freq)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-f"),
             QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("color=c=%1:size=320x240:rate=25:duration=8").arg(color), QStringLiteral("-f"),
             QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("sine=frequency=%1:sample_rate=48000:duration=8").arg(freq), QStringLiteral("-c:v"),
             QStringLiteral("mpeg4"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), QStringLiteral("-shortest"),
             path});
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

QList<TimelineClip> clipsOf(const Timeline &tl, Kind kind)
{
    return tl.tracks[tl.indexOf(kind, 0)].clips;
}

QColor centreOf(const QImage &img, const QRectF &cell)
{
    return img.pixelColor(int(cell.center().x() * img.width()), int(cell.center().y() * img.height()));
}

} // namespace

class TestPhase12b : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QStringList m_cams; // red, green, blue

    // A three-camera multicamera clip at 0 on V1 + A1 (200 frames).
    QUuid setup(ProjectController &ctl)
    {
        ctl.importFiles(m_cams);
        waitIdle(ctl);
        QList<QUuid> ids(3);
        for (const MediaItem &m : ctl.project().items)
            for (int i = 0; i < 3; ++i)
                if (m.path == m_cams[i])
                    ids[i] = m.id;
        ctl.setPlayhead(0);
        return ctl.createMulticam(ids, {0, 0, 0});
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase12b"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        const QStringList colors{QStringLiteral("red"), QStringLiteral("lime"), QStringLiteral("blue")};
        for (int i = 0; i < 3; ++i) {
            m_cams << m_dir.filePath(QStringLiteral("cam%1.mkv").arg(i + 1));
            QVERIFY2(makeCamera(m_cams.last(), colors[i], 300 + 200 * i), "ffmpeg could not create test media");
        }
    }

    void grid()
    {
        QCOMPARE(Multicam::columns(1), 1);
        QCOMPARE(Multicam::columns(2), 2);
        QCOMPARE(Multicam::columns(4), 2);
        QCOMPARE(Multicam::columns(5), 3);
        QCOMPARE(Multicam::columns(9), 3);
        QCOMPARE(Multicam::cell(0, 1), QRectF(0, 0, 1, 1));
        QCOMPARE(Multicam::cell(1, 2), QRectF(0.5, 0.25, 0.5, 0.5)); // one row, centred
        QCOMPARE(Multicam::cell(2, 3), QRectF(0, 0.5, 0.5, 0.5));
        QVERIFY(Multicam::cell(3, 3).isNull());
        QCOMPARE(Multicam::angleAt({0.75, 0.3}, 3), 1);
        QCOMPARE(Multicam::angleAt({0.75, 0.75}, 3), -1); // the empty cell
        QCOMPARE(Multicam::angleAt({0.5, 0.1}, 2), -1);   // above the row

        ProjectController ctl;
        const QUuid nest = setup(ctl);
        QVERIFY(!nest.isNull());
        const Project g = Multicam::gridProject(ctl.project(), nest);
        QCOMPARE(g.timeline.count(Kind::Video), 3);
        QCOMPARE(g.timeline.duration(), 200);
        for (const TimelineTrack &t : g.timeline.tracks)
            if (t.kind == Kind::Audio)
                QVERIFY(t.clips.isEmpty());
        const TimelineClip &third = g.timeline.tracks[g.timeline.indexOf(Kind::Video, 2)].clips.first();
        QCOMPARE(third.motion.base.scale, 50.0);
        QCOMPARE(third.motion.base.x, -0.25);
        QCOMPARE(third.motion.base.y, 0.25);
        // The project itself is not changed; a plain sequence gives nothing.
        QVERIFY(ctl.project().nest(nest)->timeline.tracks[0].clips.first().motion.isIdentity());
        QVERIFY(Multicam::gridProject(ctl.project(), QUuid::createUuid()).timeline.tracks.isEmpty());
    }

    void switching()
    {
        ProjectController ctl;
        QVERIFY(!setup(ctl).isNull());
        QVERIFY(ctl.multicamClipAt(50).isNull() == false);
        QVERIFY(!ctl.multicamClipAt(50, Kind::Audio).isNull());
        QVERIFY(ctl.multicamClipAt(250).isNull());

        const int steps = ctl.undoStack()->count();
        ctl.setPlayhead(50);
        QVERIFY(ctl.switchMulticamAngle(1));
        QCOMPARE(ctl.undoStack()->count(), steps + 1);
        QList<TimelineClip> v = clipsOf(ctl.timeline(), Kind::Video);
        QCOMPARE(v.size(), qsizetype(2));
        QCOMPARE(v[0].angle, 0);
        QCOMPARE(v[0].end(), 50);
        QCOMPARE(v[1].angle, 1);
        QCOMPARE(v[1].in, 50); // the same moment of the sequence
        // The sound was cut with it (pairs stay linked) and keeps its angle.
        QList<TimelineClip> a = clipsOf(ctl.timeline(), Kind::Audio);
        QCOMPARE(a.size(), qsizetype(2));
        QCOMPARE(a[1].angle, 0);
        QCOMPARE(a[1].linkId, v[1].linkId);
        QVERIFY(v[0].linkId != v[1].linkId);

        QVERIFY(!ctl.switchMulticamAngle(1)); // already showing it
        QVERIFY(!ctl.switchMulticamAngle(3)); // no camera 4
        QVERIFY(!ctl.switchMulticamAngle(-1));

        // Exactly at a cut: no new cut, the part after it changes.
        QVERIFY(ctl.switchMulticamAngle(2));
        QCOMPARE(clipsOf(ctl.timeline(), Kind::Video).size(), qsizetype(2));
        QCOMPARE(clipsOf(ctl.timeline(), Kind::Video)[1].angle, 2);

        // Sound on its own.
        ctl.setPlayhead(120);
        QVERIFY(ctl.switchMulticamAngle(2, Kind::Audio));
        a = clipsOf(ctl.timeline(), Kind::Audio);
        QCOMPARE(a.size(), qsizetype(3));
        QCOMPARE(a[2].start, 120);
        QCOMPARE(a[2].angle, 2);
        QCOMPARE(clipsOf(ctl.timeline(), Kind::Video).size(), qsizetype(3)); // the picture was cut with it
        QCOMPARE(clipsOf(ctl.timeline(), Kind::Video)[2].angle, 2);

        // Undo goes back one switch at a time.
        ctl.undoStack()->undo();
        QCOMPARE(clipsOf(ctl.timeline(), Kind::Audio).size(), qsizetype(2));
        ctl.undoStack()->undo();
        QCOMPARE(clipsOf(ctl.timeline(), Kind::Video)[1].angle, 1);
        ctl.undoStack()->undo();
        QCOMPARE(clipsOf(ctl.timeline(), Kind::Video).size(), qsizetype(1));

        // Nothing to switch past the clip.
        ctl.setPlayhead(300);
        QVERIFY(!ctl.switchMulticamAngle(1));
    }

    void gridFrame()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        QVERIFY(!setup(ctl).isNull());
        ctl.setPlayhead(60);
        QSignalSpy ready(&ctl, &ProjectController::multicamGridReady);
        ctl.requestMulticamGrid();
        QVERIFY(ready.wait(20000));
        const QImage img = ready.last().first().value<QImage>().convertToFormat(QImage::Format_RGB32);
        QVERIFY(!img.isNull());
        QCOMPARE(ready.last().at(1).toUuid(), ctl.multicamClipAt(60));
        const QColor red = centreOf(img, Multicam::cell(0, 3));
        const QColor green = centreOf(img, Multicam::cell(1, 3));
        const QColor blue = centreOf(img, Multicam::cell(2, 3));
        const QColor empty = centreOf(img, QRectF(0.5, 0.5, 0.5, 0.5));
        QVERIFY2(red.red() > 200 && red.green() < 60, qPrintable(red.name()));
        QVERIFY2(green.green() > 200 && green.red() < 60, qPrintable(green.name()));
        QVERIFY2(blue.blue() > 200 && blue.red() < 60, qPrintable(blue.name()));
        QVERIFY2(empty.red() < 30 && empty.green() < 30 && empty.blue() < 30, qPrintable(empty.name()));
        // The same moment again: no new render; past the clip: an empty grid.
        ready.clear();
        ctl.requestMulticamGrid();
        QTest::qWait(200);
        QCOMPARE(ready.size(), 0);
        ctl.setPlayhead(400);
        ctl.requestMulticamGrid();
        QCOMPARE(ready.size(), 1);
        QVERIFY(ready.last().first().value<QImage>().isNull());
    }

    void monitorAndKeys()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        QVERIFY(!setup(*ctl).isNull());
        ctl->setPlayhead(40);

        QAction *cam2 = w.findChild<QAction *>(QStringLiteral("actMulticamCamera2"));
        QAction *cam4 = w.findChild<QAction *>(QStringLiteral("actMulticamCamera4"));
        QAction *snd3 = w.findChild<QAction *>(QStringLiteral("actMulticamSound3"));
        QVERIFY(cam2 && cam4 && snd3);
        QCOMPARE(cam2->shortcut(), QKeySequence(QStringLiteral("2")));
        QCOMPARE(snd3->shortcut(), QKeySequence(QStringLiteral("Alt+3")));
        QVERIFY(cam2->isEnabled());
        QVERIFY(!cam4->isEnabled()); // only three cameras
        cam2->trigger();
        QCOMPARE(clipsOf(ctl->timeline(), Kind::Video).last().angle, 1);
        snd3->trigger();
        QCOMPARE(clipsOf(ctl->timeline(), Kind::Audio).last().angle, 2);
        ctl->setPlayhead(500);
        QVERIFY(!cam2->isEnabled());

        // The key itself, with the timeline focused.
        ctl->setPlayhead(80);
        w.findChild<QWidget *>(QStringLiteral("timelineView"))->setFocus();
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_1);
        QCOMPARE(clipsOf(ctl->timeline(), Kind::Video).last().start, 80);
        QCOMPARE(clipsOf(ctl->timeline(), Kind::Video).last().angle, 0);

        // The monitor: hidden at first; shown, it gets the grid; a click switches.
        QAction *show = w.findChild<QAction *>(QStringLiteral("actMulticamMonitor"));
        auto *dock = w.findChild<QDockWidget *>(QStringLiteral("multicamDock"));
        auto *mon = w.findChild<MulticamMonitor *>(QStringLiteral("multicamMonitor"));
        QVERIFY(show && dock && mon);
        QVERIFY(dock->isHidden());
        if (!ctl->canPlay())
            return;
        show->trigger();
        QVERIFY(dock->isVisible());
        dock->resize(480, 320);
        QTRY_VERIFY_WITH_TIMEOUT(!mon->image().isNull(), 20000);
        ctl->setPlayhead(150);
        QTRY_COMPARE_WITH_TIMEOUT(mon->clip(), ctl->multicamClipAt(150), 20000);
        const QRect r = mon->imageRect();
        const QRectF c3 = Multicam::cell(2, 3);
        const QPoint at(r.x() + int(c3.center().x() * r.width()), r.y() + int(c3.center().y() * r.height()));
        QCOMPARE(mon->angleAt(at), 2);
        QTest::mouseClick(mon, Qt::LeftButton, {}, at);
        QCOMPARE(clipsOf(ctl->timeline(), Kind::Video).last().start, 150);
        QCOMPARE(clipsOf(ctl->timeline(), Kind::Video).last().angle, 2);
        QCOMPARE(mon->angleAt(QPoint(r.right() - 2, r.bottom() - 2)), -1); // the empty cell
        if (qEnvironmentVariableIsSet("BC_SHOT_DIR")) { // visual check
            QTRY_VERIFY_WITH_TIMEOUT(!mon->image().isNull(), 20000);
            QTest::qWait(300);
            w.grab().save(QDir(qEnvironmentVariable("BC_SHOT_DIR")).filePath(QStringLiteral("multicam.png")));
        }
    }
};

QTEST_MAIN(TestPhase12b)
#include "tst_phase12b.moc"
