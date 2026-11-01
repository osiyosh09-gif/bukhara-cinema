// Phase 6 (part 2) acceptance tests: roll, slip, slide, razor, copy / cut /
// paste and link / unlink. Every edit is checked with undo.
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/MediaCache.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QScrollBar>
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
             QStringLiteral("mpeg4"), QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), path});
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

MediaItem fakeVideo()
{
    MediaItem m;
    m.id = QUuid::createUuid();
    m.probed = true;
    m.info.kind = MediaInfo::Kind::Video;
    m.info.durationUs = 10 * 1000000; // 250 frames at 25 fps
    m.info.width = 1920;
    m.info.height = 1080;
    m.info.channels = 2;
    m.info.sampleRate = 48000;
    return m;
}

const TimelineClip &at(const Timeline &t, Kind kind, int n, int index)
{
    return t.tracks[t.indexOf(kind, n)].clips[index];
}

int count(const Timeline &t, Kind kind, int n)
{
    return int(t.tracks[t.indexOf(kind, n)].clips.size());
}

// Three video clips with linked sound, back to back:
// A 0..100 (in 0), B 100..200 (in 50), C 200..300 (in 20); sources 250 frames.
Timeline threeClips(QList<QUuid> *ids)
{
    Timeline t = Timeline::makeDefault();
    const MediaItem m = fakeVideo();
    *ids << t.place(m, 0, 0, 0, 0, 100).first();
    *ids << t.place(m, 0, 0, 100, 50, 100).first();
    *ids << t.place(m, 0, 0, 200, 20, 100).first();
    return t;
}

const Timeline::MediaFrames k250 = [](const QUuid &) { return qint64(250); };

} // namespace

class TestPhase6b : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase6b"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("counter.mp4"));
        QVERIFY(makeVideo(m_video));
    }

    void rollMovesTheCut()
    {
        QList<QUuid> ids;
        Timeline t = threeClips(&ids);
        QVERIFY(t.roll(ids[1], true, 120, k250));
        QCOMPARE(at(t, Kind::Video, 0, 0).length, qint64(120));
        QCOMPARE(at(t, Kind::Video, 0, 1).start, qint64(120));
        QCOMPARE(at(t, Kind::Video, 0, 1).in, qint64(70));
        QCOMPARE(at(t, Kind::Video, 0, 1).length, qint64(80));
        QCOMPARE(at(t, Kind::Audio, 0, 1).in, qint64(70)); // linked sound follows
        QCOMPARE(t.duration(), qint64(300));               // nothing else moved

        // Limits: B keeps one frame; B cannot start before its source.
        Timeline t2 = threeClips(&ids);
        QVERIFY(t2.roll(ids[4], true, 1000, k250)); // ids[4] = B of the second set
        QCOMPARE(at(t2, Kind::Video, 0, 1).length, qint64(1));
        Timeline t3 = threeClips(&ids);
        QVERIFY(t3.roll(ids[7], true, -50, k250));
        QCOMPARE(at(t3, Kind::Video, 0, 1).start, qint64(50)); // B.in was 50
        QCOMPARE(at(t3, Kind::Video, 0, 1).in, qint64(0));

        // The tail of the last clip rolls into empty space, as far as its source goes.
        Timeline t4 = threeClips(&ids);
        QVERIFY(t4.roll(ids[11], false, 1000, k250));
        QCOMPARE(at(t4, Kind::Video, 0, 2).end(), qint64(200 + 250 - 20));

        // Locked tracks do not change.
        Timeline t5 = threeClips(&ids);
        t5.tracks[t5.indexOf(Kind::Audio, 0)].locked = true;
        QVERIFY(!t5.roll(ids[13], true, 120, k250));
    }

    void slipChangesOnlyTheSourcePart()
    {
        QList<QUuid> ids;
        Timeline t = threeClips(&ids);
        QVERIFY(t.slip(ids[1], 30, k250));
        QCOMPARE(at(t, Kind::Video, 0, 1).in, qint64(80));
        QCOMPARE(at(t, Kind::Video, 0, 1).start, qint64(100));
        QCOMPARE(at(t, Kind::Audio, 0, 1).in, qint64(80));
        QVERIFY(t.slip(ids[1], 1000, k250));
        QCOMPARE(at(t, Kind::Video, 0, 1).in, qint64(150)); // 250 - 100
        QVERIFY(t.slip(ids[1], -1000, k250));
        QCOMPARE(at(t, Kind::Video, 0, 1).in, qint64(0));
        QVERIFY(!t.slip(ids[1], -5, k250)); // already at the start

        // Still pictures look the same anywhere: nothing to slip.
        QVERIFY(!t.slip(ids[1], 5, [](const QUuid &) { return qint64(-1); }));
    }

    void slideMovesBetweenNeighbours()
    {
        QList<QUuid> ids;
        Timeline t = threeClips(&ids);
        QVERIFY(t.slide(ids[1], 10, k250));
        QCOMPARE(at(t, Kind::Video, 0, 0).length, qint64(110));
        QCOMPARE(at(t, Kind::Video, 0, 1).start, qint64(110));
        QCOMPARE(at(t, Kind::Video, 0, 1).in, qint64(50)); // the clip itself shows the same
        QCOMPARE(at(t, Kind::Video, 0, 2).start, qint64(210));
        QCOMPARE(at(t, Kind::Video, 0, 2).in, qint64(30));
        QCOMPARE(at(t, Kind::Video, 0, 2).length, qint64(90));
        QCOMPARE(at(t, Kind::Audio, 0, 1).start, qint64(110));
        QCOMPARE(t.duration(), qint64(300));

        // Left: limited by C's source start (C.in was 20).
        Timeline t2 = threeClips(&ids);
        QVERIFY(t2.slide(ids[4], -1000, k250));
        QCOMPARE(at(t2, Kind::Video, 0, 1).start, qint64(80));
        QCOMPARE(at(t2, Kind::Video, 0, 0).length, qint64(80));
        QCOMPARE(at(t2, Kind::Video, 0, 2).in, qint64(0));
        QCOMPARE(at(t2, Kind::Video, 0, 2).length, qint64(120));

        // A clip with empty space around it just moves inside the space.
        Timeline t3 = Timeline::makeDefault();
        const MediaItem m = fakeVideo();
        t3.place(m, 0, -1, 0, 0, 50);
        const QUuid mid = t3.place(m, 0, -1, 100, 0, 50).first();
        t3.place(m, 0, -1, 200, 0, 50);
        QVERIFY(t3.slide(mid, -1000, k250));
        QCOMPARE(t3.clip(mid)->start, qint64(50));
        QCOMPARE(at(t3, Kind::Video, 0, 0).length, qint64(50));
    }

    void linkAndUnlink()
    {
        QList<QUuid> ids;
        Timeline t = threeClips(&ids);
        QVERIFY(t.isLinked(ids[0]));
        QVERIFY(t.unlink({ids[0]}));
        QVERIFY(!t.isLinked(ids[0]));
        QVERIFY(at(t, Kind::Audio, 0, 0).linkId.isNull());
        QVERIFY(!t.unlink({ids[0]}));
        QVERIFY(!t.link({ids[0]})); // one clip is not a link
        QVERIFY(t.link({ids[0], at(t, Kind::Audio, 0, 0).id}));
        QCOMPARE(t.withLinked({ids[0]}).size(), 2);
    }

    // --- controller: undo, clipboard

    void controllerEditsUndo()
    {
        ProjectController ctl;
        ctl.importFiles({m_video});
        waitIdle(ctl);
        const QUuid media = ctl.project().items.first().id;
        // Two copies of the 100-frame clip, then split the second at 150.
        ctl.placeMedia({media, media}, 0, 0);
        QCOMPARE(ctl.timeline().duration(), qint64(200));
        const QUuid second = at(ctl.timeline(), Kind::Video, 0, 1).id;
        QVERIFY(ctl.trimClip(second, true, 120)); // second: 120..200, in 20
        const Timeline trimmed = ctl.timeline();

        QVERIFY(ctl.rollClip(second, true, 110));
        QCOMPARE(at(ctl.timeline(), Kind::Video, 0, 1).start, qint64(110));
        QVERIFY(ctl.slipClip(second, -5));
        QVERIFY(ctl.slideClip(second, -3));
        ctl.undoStack()->undo();
        ctl.undoStack()->undo();
        ctl.undoStack()->undo();
        QVERIFY(ctl.timeline() == trimmed);

        // Link toggle: unlink the first pair, then link it again.
        const QUuid v = at(ctl.timeline(), Kind::Video, 0, 0).id;
        const QUuid a = at(ctl.timeline(), Kind::Audio, 0, 0).id;
        QVERIFY(ctl.toggleLink({v}));
        QVERIFY(!ctl.timeline().isLinked(v));
        QVERIFY(ctl.toggleLink({v, a}));
        QVERIFY(ctl.timeline().isLinked(a));
        ctl.undoStack()->undo();
        QVERIFY(!ctl.timeline().isLinked(v));
    }

    void copyCutPaste()
    {
        ProjectController ctl;
        ctl.importFiles({m_video});
        waitIdle(ctl);
        const QUuid media = ctl.project().items.first().id;
        const QList<QUuid> placed = ctl.placeMedia({media}, 0, 0); // V1 + A1, 0..100
        QVERIFY(!ctl.canPaste());
        QVERIFY(ctl.copyClips(placed));
        QVERIFY(ctl.canPaste());

        ctl.setPlayhead(150);
        const QList<QUuid> pasted = ctl.pasteClips();
        QCOMPARE(pasted.size(), 2);
        QCOMPARE(ctl.timeline().clip(pasted[0])->start, qint64(150));
        QCOMPARE(ctl.playhead(), qint64(250));
        // The copies are linked to each other, not to the originals.
        QVERIFY(ctl.timeline().isLinked(pasted[0]));
        QVERIFY(ctl.timeline().clip(pasted[0])->linkId != ctl.timeline().clip(placed[0])->linkId);

        // Paste insert at 50: splits the first clip and moves the rest by 100.
        ctl.setPlayhead(50);
        QCOMPARE(ctl.pasteClips(true).size(), 2);
        QCOMPARE(count(ctl.timeline(), Kind::Video, 0), 4);
        QCOMPARE(ctl.timeline().duration(), qint64(350));
        QCOMPARE(at(ctl.timeline(), Kind::Video, 0, 2).start, qint64(150));

        // Cut removes and keeps the clips for pasting.
        const int before = count(ctl.timeline(), Kind::Video, 0);
        QVERIFY(ctl.cutClips(ctl.timeline().withLinked({at(ctl.timeline(), Kind::Video, 0, 3).id})));
        QCOMPARE(count(ctl.timeline(), Kind::Video, 0), before - 1);
        ctl.undoStack()->undo();
        QCOMPARE(count(ctl.timeline(), Kind::Video, 0), before);

        // A locked track gets nothing.
        ctl.setTrackFlag(ctl.timeline().indexOf(Kind::Video, 0), QStringLiteral("locked"), true);
        ctl.setTrackFlag(ctl.timeline().indexOf(Kind::Audio, 0), QStringLiteral("locked"), true);
        ctl.setPlayhead(500);
        QVERIFY(ctl.pasteClips().isEmpty());
    }

    // --- main window

    void toolsInTheWindow()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        auto act = [&](const char *name) { return w.findChild<QAction *>(QString::fromLatin1(name)); };
        for (const char *name : {"actCut", "actCopy", "actPaste", "actPasteInsert", "actLink", "actToolSelect",
                                 "actToolRazor", "actToolRoll", "actToolSlip", "actToolSlide"}) {
            QVERIFY2(act(name), name);
            QVERIFY2(!act(name)->property("bc_planned").toBool(), name);
        }
        QCOMPARE(act("actToolRazor")->shortcut(), QKeySequence(QStringLiteral("C")));
        QCOMPARE(act("actToolRoll")->shortcut(), QKeySequence(QStringLiteral("N")));
        QCOMPARE(act("actToolSlip")->shortcut(), QKeySequence(QStringLiteral("Y")));
        QCOMPARE(act("actToolSlide")->shortcut(), QKeySequence(QStringLiteral("U")));
        QCOMPARE(act("actLink")->shortcut(), QKeySequence(QStringLiteral("Ctrl+L")));
        QVERIFY(!act("actPaste")->isEnabled()); // nothing copied yet

        ctl->importFiles({m_video});
        waitIdle(*ctl);
        ctl->placeMedia({ctl->project().items.first().id}, 0, 0);
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        view->setPixelsPerFrame(4.0);
        view->horizontalScrollBar()->setValue(0);
        view->setSnapping(false);
        QWidget *vp = view->viewport();
        const QRect v1 = view->trackRect(0);

        // Razor: a click splits the clip (and its sound) there.
        act("actToolRazor")->trigger();
        QCOMPARE(view->tool(), TimelineView::Tool::Razor);
        QTest::mouseClick(vp, Qt::LeftButton, {}, QPoint(view->xForFrame(40), v1.center().y()));
        QCOMPARE(count(ctl->timeline(), Kind::Video, 0), 2);
        QCOMPARE(count(ctl->timeline(), Kind::Audio, 0), 2);
        QCOMPARE(at(ctl->timeline(), Kind::Video, 0, 1).start, qint64(40));

        // Roll the cut at 40 by 40 px (10 frames) to the right.
        act("actToolRoll")->trigger();
        const QPoint cut(view->xForFrame(40) + 1, v1.center().y());
        QTest::mousePress(vp, Qt::LeftButton, {}, cut);
        QTest::mouseMove(vp, cut + QPoint(20, 0));
        QTest::mouseMove(vp, cut + QPoint(40, 0));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, cut + QPoint(40, 0));
        QCOMPARE(at(ctl->timeline(), Kind::Video, 0, 0).length, qint64(50));
        QCOMPARE(at(ctl->timeline(), Kind::Video, 0, 1).in, qint64(50));
        QCOMPARE(ctl->timeline().duration(), qint64(100));

        // Slip: dragging the second part 80 px (20 frames) right shows 20 frames earlier.
        act("actToolSlip")->trigger();
        const QPoint body(view->xForFrame(70), v1.center().y());
        QTest::mousePress(vp, Qt::LeftButton, {}, body);
        QTest::mouseMove(vp, body + QPoint(40, 0));
        QTest::mouseMove(vp, body + QPoint(80, 0));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, body + QPoint(80, 0));
        QCOMPARE(at(ctl->timeline(), Kind::Video, 0, 1).in, qint64(30)); // 20 frames earlier
        QCOMPARE(at(ctl->timeline(), Kind::Video, 0, 1).start, qint64(50));

        // V goes back to selection; copy from the window enables paste.
        act("actToolSelect")->trigger();
        QCOMPARE(view->tool(), TimelineView::Tool::Select);
        view->setSelection(ctl->timeline().withLinked({at(ctl->timeline(), Kind::Video, 0, 0).id}));
        QVERIFY(act("actCopy")->isEnabled());
        act("actCopy")->trigger();
        QVERIFY(act("actPaste")->isEnabled());
        QVERIFY(act("actLink")->isEnabled());
        act("actLink")->trigger(); // unlink
        QVERIFY(!ctl->timeline().isLinked(at(ctl->timeline(), Kind::Video, 0, 0).id));
    }
};

QTEST_MAIN(TestPhase6b)
#include "tst_phase6b.moc"
