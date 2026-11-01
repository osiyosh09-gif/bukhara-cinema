// Phase 2 acceptance tests: timeline model, undo/redo, saving, timeline
// panel mouse editing, Program monitor frame at the playhead.
#include "app/Language.h"
#include "app/MainWindow.h"
#include "app/MonitorView.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/MediaProbe.h"
#include "core/Project.h"
#include "core/Timeline.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSignalSpy>
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

MediaItem fakeVideo(qint64 seconds, bool audio = true)
{
    MediaItem m;
    m.id = QUuid::createUuid();
    m.name = QStringLiteral("clip.mp4");
    m.probed = true;
    m.info.kind = MediaInfo::Kind::Video;
    m.info.durationUs = seconds * 1000000;
    m.info.width = 320;
    m.info.height = 240;
    m.info.fpsNum = 25;
    if (audio) {
        m.info.channels = 2;
        m.info.sampleRate = 48000;
    }
    return m;
}

MediaItem fakeImage()
{
    MediaItem m = fakeVideo(0, false);
    m.name = QStringLiteral("photo.png");
    m.info.kind = MediaInfo::Kind::Image;
    return m;
}

const TimelineClip &clipOn(const Timeline &t, Kind k, int n, int i)
{
    return t.tracks[t.indexOf(k, n)].clips[i];
}

int clipCount(const Timeline &t, Kind k, int n)
{
    return int(t.tracks[t.indexOf(k, n)].clips.size());
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

class TestPhase2 : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;
    SequenceSettings m_seq; // 25 fps

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("count.mp4"));
        QVERIFY2(makeVideo(m_video), "ffmpeg could not create test media");
        Language::instance().setLanguage(QStringLiteral("uz_Cyrl"), false);
    }

    // --- model ---------------------------------------------------------

    void defaultTracks()
    {
        const Timeline t = Timeline::makeDefault();
        QCOMPARE(t.count(Kind::Video), 3);
        QCOMPARE(t.count(Kind::Audio), 3);
        QCOMPARE(t.trackName(0), QStringLiteral("V1"));
        QCOMPARE(t.trackName(t.indexOf(Kind::Audio, 1)), QStringLiteral("A2"));
        QVERIFY(t.isEmpty());
    }

    void placeLinkedVideoAndAudio()
    {
        Timeline t = Timeline::makeDefault();
        const MediaItem m = fakeVideo(4);
        const QList<QUuid> ids = t.place(m, m_seq, 0, 0, 10);
        QCOMPARE(ids.size(), 2);
        const TimelineClip &v = clipOn(t, Kind::Video, 0, 0);
        const TimelineClip &a = clipOn(t, Kind::Audio, 0, 0);
        QCOMPARE(v.start, 10);
        QCOMPARE(v.length, 100); // 4 s at 25 fps
        QCOMPARE(a.length, 100);
        QVERIFY(!v.linkId.isNull());
        QCOMPARE(v.linkId, a.linkId);
        QCOMPARE(t.withLinked({v.id}).size(), 2);
        QCOMPARE(t.duration(), 110);

        // Images get 5 seconds and no audio clip.
        const QList<QUuid> img = t.place(fakeImage(), m_seq, 1, 1, 0);
        QCOMPARE(img.size(), 1);
        QCOMPARE(clipOn(t, Kind::Video, 1, 0).length, 125);
    }

    void overwriteSplitsClipUnderneath()
    {
        Timeline t = Timeline::makeDefault();
        t.place(fakeVideo(10, false), m_seq, 0, -1, 0);   // 0..250
        t.place(fakeVideo(2, false), m_seq, 0, -1, 100);  // 100..150 on top of it
        QCOMPARE(clipCount(t, Kind::Video, 0), 3);
        const auto &c = t.tracks[t.indexOf(Kind::Video, 0)].clips;
        QCOMPARE(c[0].start, 0);
        QCOMPARE(c[0].length, 100);
        QCOMPARE(c[1].start, 100);
        QCOMPARE(c[1].length, 50);
        QCOMPARE(c[2].start, 150);
        QCOMPARE(c[2].in, 150);   // the right part keeps showing the right source frames
        QCOMPARE(c[2].length, 100);
    }

    void moveClipsAndTracks()
    {
        Timeline t = Timeline::makeDefault();
        const QList<QUuid> ids = t.place(fakeVideo(2), m_seq, 0, 0, 0);
        QVERIFY(t.move(ids, 50, Kind::Video, 1)); // video to V2, audio stays on A1
        QCOMPARE(clipCount(t, Kind::Video, 0), 0);
        QCOMPARE(clipOn(t, Kind::Video, 1, 0).start, 50);
        QCOMPARE(clipOn(t, Kind::Audio, 0, 0).start, 50);

        QVERIFY(!t.move(ids, -100, Kind::Video, 0)); // would start before 0
        QVERIFY(!t.move(ids, 0, Kind::Video, 5));    // no such track

        t.tracks[t.indexOf(Kind::Video, 2)].locked = true;
        QVERIFY(!t.move(ids, 0, Kind::Video, 1)); // V3 locked
        QCOMPARE(clipOn(t, Kind::Video, 1, 0).start, 50); // unchanged
    }

    void trimRespectsMediaAndNeighbours()
    {
        Timeline t = Timeline::makeDefault();
        const MediaItem m = fakeVideo(4); // 100 frames
        auto len = [&](const QUuid &) { return qint64(100); };
        const QUuid v = t.place(m, m_seq, 0, 0, 20).first();

        QVERIFY(t.trim(v, false, 70, len)); // shorten end to 70
        QCOMPARE(t.clip(v)->length, 50);
        QCOMPARE(clipOn(t, Kind::Audio, 0, 0).length, 50); // linked audio follows

        QVERIFY(t.trim(v, false, 500, len)); // cannot pass the media end
        QCOMPARE(t.clip(v)->end(), 120);

        QVERIFY(t.trim(v, true, 40, len)); // head: start later
        QCOMPARE(t.clip(v)->start, 40);
        QCOMPARE(t.clip(v)->in, 20);
        QVERIFY(t.trim(v, true, 0, len)); // cannot go before source start (frame 20)
        QCOMPARE(t.clip(v)->start, 20);
        QCOMPARE(t.clip(v)->in, 0);

        // A neighbour stops the trim.
        Timeline t2 = Timeline::makeDefault();
        const QUuid a = t2.place(fakeVideo(2, false), m_seq, 0, -1, 0).first();   // 0..50
        t2.place(fakeVideo(2, false), m_seq, 0, -1, 60);                          // 60..110
        QVERIFY(t2.trim(a, false, 90, [](const QUuid &) { return qint64(1000); }));
        QCOMPARE(t2.clip(a)->end(), 60);

        // Images can be stretched freely.
        Timeline t3 = Timeline::makeDefault();
        const QUuid img = t3.place(fakeImage(), m_seq, 0, -1, 0).first();
        QVERIFY(t3.trim(img, false, 1000, [](const QUuid &) { return qint64(-1); }));
        QCOMPARE(t3.clip(img)->length, 1000);
    }

    void splitAtFrame()
    {
        Timeline t = Timeline::makeDefault();
        t.place(fakeVideo(4), m_seq, 0, 0, 0);
        const QList<QUuid> created = t.splitAt(30);
        QCOMPARE(created.size(), 2);
        QCOMPARE(clipCount(t, Kind::Video, 0), 2);
        const TimelineClip &rightV = clipOn(t, Kind::Video, 0, 1);
        const TimelineClip &rightA = clipOn(t, Kind::Audio, 0, 1);
        QCOMPARE(rightV.start, 30);
        QCOMPARE(rightV.in, 30);
        QCOMPARE(rightV.length, 70);
        QCOMPARE(rightV.linkId, rightA.linkId); // right halves linked to each other
        QVERIFY(rightV.linkId != clipOn(t, Kind::Video, 0, 0).linkId);

        // Only the selected clip (and its linked partner).
        Timeline t2 = Timeline::makeDefault();
        const QUuid first = t2.place(fakeVideo(4, false), m_seq, 0, -1, 0).first();
        t2.place(fakeVideo(4, false), m_seq, 1, -1, 0);
        QCOMPARE(t2.splitAt(50, {first}).size(), 1);
        QCOMPARE(clipCount(t2, Kind::Video, 0), 2);
        QCOMPARE(clipCount(t2, Kind::Video, 1), 1);
        QVERIFY(t2.splitAt(0).isEmpty()); // at a clip edge nothing happens
    }

    void removeAndRippleDelete()
    {
        Timeline t = Timeline::makeDefault();
        const QUuid a = t.place(fakeVideo(2, false), m_seq, 0, -1, 0).first();   // 0..50
        t.place(fakeVideo(2, false), m_seq, 0, -1, 50);                          // 50..100
        t.place(fakeVideo(2, false), m_seq, 0, -1, 100);                         // 100..150

        Timeline lift = t;
        QVERIFY(lift.remove({a}, false));
        QCOMPARE(clipOn(lift, Kind::Video, 0, 0).start, 50); // gap stays

        QVERIFY(t.remove({a}, true));
        QCOMPARE(clipOn(t, Kind::Video, 0, 0).start, 0);   // gap closed
        QCOMPARE(clipOn(t, Kind::Video, 0, 1).start, 50);
        QCOMPARE(t.duration(), 100);
    }

    void jsonRoundTripAndRepair()
    {
        Timeline t = Timeline::makeDefault();
        t.place(fakeVideo(2), m_seq, 0, 0, 5);
        t.tracks[0].hidden = true;
        t.tracks[t.indexOf(Kind::Audio, 0)].locked = true;
        QCOMPARE(Timeline::fromJson(t.toJson()), t);

        // Overlapping clips in a hand-edited file are dropped, not crashed on.
        QJsonArray a = t.toJson();
        QJsonObject v1 = a[0].toObject();
        QJsonArray clips = v1.value(QStringLiteral("clips")).toArray();
        QJsonObject dup = clips[0].toObject();
        dup[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        dup[QStringLiteral("start")] = 10.0;
        clips.append(dup);
        v1[QStringLiteral("clips")] = clips;
        a[0] = v1;
        const Timeline r = Timeline::fromJson(a);
        QCOMPARE(clipCount(r, Kind::Video, 0), 1);
    }

    void rescaleOnFrameRateChange()
    {
        Timeline t = Timeline::makeDefault();
        t.place(fakeVideo(2), m_seq, 0, 0, 25); // 1 s .. 3 s at 25 fps
        t.rescale(25.0, 50.0);
        QCOMPARE(clipOn(t, Kind::Video, 0, 0).start, 50);
        QCOMPARE(clipOn(t, Kind::Video, 0, 0).length, 100);
    }

    // --- project file -------------------------------------------------

    void projectSavesTimeline()
    {
        Project p;
        MediaItem &m = p.addItem(m_video);
        m.info = fakeVideo(4).info;
        m.probed = true;
        p.timeline.place(m, p.sequence, 0, 0, 12);
        const QString file = m_dir.filePath(QStringLiteral("tl.bcproj"));
        QString err;
        QVERIFY2(p.save(file, &err), qPrintable(err));

        Project q;
        QVERIFY2(Project::load(file, q, &err), qPrintable(err));
        QCOMPARE(q.timeline, p.timeline);

        // Removing a media item removes its clips.
        QVERIFY(q.removeItem(q.items.first().id));
        QVERIFY(q.timeline.isEmpty());
    }

    void oldProjectFileGetsDefaultTimeline()
    {
        Project p;
        QJsonObject root = QJsonDocument::fromJson(p.toJson({})).object();
        root[QStringLiteral("version")] = 1;
        root.remove(QStringLiteral("timeline"));
        Project q;
        QString err;
        QVERIFY2(Project::fromJson(QJsonDocument(root).toJson(), {}, q, &err), qPrintable(err));
        QCOMPARE(q.timeline.count(Kind::Video), 3);
        QCOMPARE(q.timeline.count(Kind::Audio), 3);
    }

    // --- controller: undo / redo -------------------------------------

    void undoRedo()
    {
        ProjectController ctl;
        QCOMPARE(ctl.importFiles({m_video}), 1);
        waitIdle(ctl);
        const QUuid media = ctl.project().items.first().id;
        QVERIFY(ctl.isOnline(media));

        QCOMPARE(ctl.placeMedia({media}, 0, 0).size(), 2);
        QVERIFY(ctl.isModified());
        QVERIFY(ctl.splitClips(50, {}));
        QCOMPARE(clipCount(ctl.timeline(), Kind::Video, 0), 2);

        ctl.undoStack()->undo();
        QCOMPARE(clipCount(ctl.timeline(), Kind::Video, 0), 1);
        ctl.undoStack()->undo();
        QVERIFY(ctl.timeline().isEmpty());
        ctl.undoStack()->redo();
        ctl.undoStack()->redo();
        QCOMPARE(clipCount(ctl.timeline(), Kind::Video, 0), 2);

        // Removing the media from the bin removes its clips and clears undo.
        ctl.removeItems({media});
        QVERIFY(ctl.timeline().isEmpty());
        QVERIFY(!ctl.undoStack()->canUndo());
    }

    // --- Program monitor ---------------------------------------------

    void frameAtDecodesRequestedTime()
    {
        const QImage a = MediaProbe::frameAt(m_video, 0, 160, 120);
        const QImage b = MediaProbe::frameAt(m_video, 1500000, 160, 120);
        const QImage b2 = MediaProbe::frameAt(m_video, 1500000, 160, 120);
        QVERIFY(!a.isNull());
        QVERIFY(!b.isNull());
        QVERIFY(a != b);    // testsrc changes every frame
        QCOMPARE(b, b2);    // and the same time gives the same frame
        QVERIFY(!MediaProbe::frameAt(m_video, 60000000, 160, 120).isNull()); // past the end: last frame
    }

    void programMonitorFollowsPlayhead()
    {
        MainWindow w;
        w.show();
        ProjectController *ctl = w.controller();
        QCOMPARE(ctl->importFiles({m_video}), 1);
        waitIdle(*ctl);
        auto *program = w.findChild<MonitorView *>(QStringLiteral("programMonitor"));
        QVERIFY(program);
        QVERIFY(program->image().isNull());

        ctl->placeMedia({ctl->project().items.first().id}, 0, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!program->image().isNull(), 10000);
        const QImage first = program->image();
        ctl->setPlayhead(60);
        QTRY_VERIFY_WITH_TIMEOUT(program->image() != first, 10000);

        // Past the clip: nothing to show.
        ctl->setPlayhead(500);
        QTRY_VERIFY_WITH_TIMEOUT(program->image().isNull(), 10000);

        // Hiding V1 hides it in the monitor.
        ctl->setPlayhead(10);
        QTRY_VERIFY_WITH_TIMEOUT(!program->image().isNull(), 10000);
        ctl->setTrackFlag(0, QStringLiteral("hidden"), true);
        QTRY_VERIFY_WITH_TIMEOUT(program->image().isNull(), 10000);
    }

    // --- timeline panel: mouse and actions ---------------------------

    void mouseMoveTrimAndActions()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        QCOMPARE(ctl->importFiles({m_video}), 1);
        waitIdle(*ctl);
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        QVERIFY(view);
        view->setPixelsPerFrame(4.0);
        view->horizontalScrollBar()->setValue(0);

        QCOMPARE(ctl->placeMedia({ctl->project().items.first().id}, 0, 0).size(), 2);
        const QUuid vid = ctl->timeline().tracks[0].clips.first().id;
        const QRect v1 = view->trackRect(0);
        QWidget *vp = view->viewport();

        // Drag the clip body 100 px (= 25 frames) to the right.
        const QPoint from(view->xForFrame(40), v1.center().y());
        QTest::mousePress(vp, Qt::LeftButton, {}, from);
        QTest::mouseMove(vp, from + QPoint(50, 0));
        QTest::mouseMove(vp, from + QPoint(100, 0));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, from + QPoint(100, 0));
        QCOMPARE(ctl->timeline().clip(vid)->start, 25);
        QCOMPARE(clipOn(ctl->timeline(), Kind::Audio, 0, 0).start, 25); // linked audio moved too

        // Drag the tail edge 40 px left (= 10 frames shorter).
        view->setSnapping(false);
        const QPoint edge(view->xForFrame(125) - 1, v1.center().y());
        QTest::mousePress(vp, Qt::LeftButton, {}, edge);
        QTest::mouseMove(vp, edge + QPoint(-20, 0));
        QTest::mouseMove(vp, edge + QPoint(-40, 0));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, edge + QPoint(-40, 0));
        QCOMPARE(ctl->timeline().clip(vid)->length, 90);

        // Click on the ruler moves the playhead; Ctrl+K splits there.
        QTest::mouseClick(vp, Qt::LeftButton, {}, QPoint(view->xForFrame(60) + 1, 10));
        QCOMPARE(ctl->playhead(), 60);
        auto *split = w.findChild<QAction *>(QStringLiteral("actSplit"));
        QVERIFY(split && split->isEnabled());
        split->trigger();
        QCOMPARE(clipCount(ctl->timeline(), Kind::Video, 0), 2);

        // Select the right part and ripple delete it; then undo from the menu.
        view->setSelection(ctl->timeline().withLinked({clipOn(ctl->timeline(), Kind::Video, 0, 1).id}));
        auto *ripple = w.findChild<QAction *>(QStringLiteral("actRippleDelete"));
        QVERIFY(ripple->isEnabled());
        ripple->trigger();
        QCOMPARE(clipCount(ctl->timeline(), Kind::Video, 0), 1);
        auto *undo = w.findChild<QAction *>(QStringLiteral("actUndo"));
        QVERIFY(undo->isEnabled());
        QVERIFY(!undo->property("bc_planned").toBool());
        undo->trigger();
        QCOMPARE(clipCount(ctl->timeline(), Kind::Video, 0), 2);

        // Locked track: dragging does nothing.
        ctl->setTrackFlag(0, QStringLiteral("locked"), true);
        const qint64 before = clipOn(ctl->timeline(), Kind::Video, 0, 0).start;
        const QPoint p2(view->xForFrame(before + 5), v1.center().y());
        QTest::mousePress(vp, Qt::LeftButton, {}, p2);
        QTest::mouseMove(vp, p2 + QPoint(60, 0));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, p2 + QPoint(60, 0));
        QCOMPARE(clipOn(ctl->timeline(), Kind::Video, 0, 0).start, before);

        // Add track from the menu action.
        auto *addV = w.findChild<QAction *>(QStringLiteral("actAddVideoTrack"));
        addV->trigger();
        QCOMPARE(ctl->timeline().count(Kind::Video), 4);
    }

    // --- engine: composition and playback ----------------------------

    void engineComposesTracks()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        const QString png = m_dir.filePath(QStringLiteral("green.png"));
        QImage g(320, 240, QImage::Format_RGB32);
        g.fill(QColor(0, 200, 0));
        QVERIFY(g.save(png));
        QCOMPARE(ctl.importFiles({m_video, png}), 2);
        waitIdle(ctl);
        QUuid video, image;
        for (const MediaItem &m : ctl.project().items)
            (m.info.kind == MediaInfo::Kind::Image ? image : video) = m.id;
        ctl.placeMedia({video}, 0, 0);
        ctl.placeMedia({image}, 1, 20); // V2 from frame 20

        QImage shown;
        connect(&ctl, &ProjectController::programFrameReady, this, [&](const QImage &i) { shown = i; });
        auto centre = [&] { return QColor(shown.pixel(shown.width() / 2, shown.height() / 2)); };

        ctl.setPlayhead(30); // V2 (green image) covers V1
        QTRY_VERIFY_WITH_TIMEOUT(!shown.isNull() && centre().green() > 150 && centre().red() < 60, 10000);
        ctl.setTrackFlag(1, QStringLiteral("hidden"), true); // hide V2: the video shows
        QTRY_VERIFY_WITH_TIMEOUT(!shown.isNull() && !(centre().green() > 150 && centre().red() < 60), 10000);
    }

    void playbackAdvancesAndPauses()
    {
        ProjectController ctl;
        QVERIFY2(ctl.canPlay(), qPrintable(ctl.engineError()));
        QCOMPARE(ctl.importFiles({m_video}), 1);
        waitIdle(ctl);
        ctl.placeMedia({ctl.project().items.first().id}, 0, 0); // 100 frames
        int frames = 0;
        connect(&ctl, &ProjectController::programFrameReady, this, [&](const QImage &i) { frames += !i.isNull(); });

        QSignalSpy playing(&ctl, &ProjectController::playingChanged);
        QVERIFY(ctl.play());
        QVERIFY(ctl.isPlaying());
        QTRY_VERIFY_WITH_TIMEOUT(ctl.playhead() > 10, 10000);
        QVERIFY(frames > 5);
        ctl.pause();
        QVERIFY(!ctl.isPlaying());
        const qint64 stoppedAt = ctl.playhead();
        QTest::qWait(300);
        QCOMPARE(ctl.playhead(), stoppedAt);

        // Playback stops by itself at the end of the sequence.
        ctl.setPlayhead(90);
        QVERIFY(ctl.play());
        QTRY_VERIFY_WITH_TIMEOUT(!ctl.isPlaying(), 10000);
        QVERIFY(ctl.playhead() >= 98);
        QVERIFY(playing.count() >= 4);

        // An edit stops playback (the graph is rebuilt).
        ctl.setPlayhead(0);
        QVERIFY(ctl.play());
        QVERIFY(ctl.splitClips(50, {}));
        QVERIFY(!ctl.isPlaying());
    }

    void phase2ActionsAreReal()
    {
        MainWindow w;
        auto *program = w.findChild<MonitorView *>(QStringLiteral("programMonitor"));
        QVERIFY(program->findChild<QToolButton *>(QStringLiteral("tcPlay"))->isEnabled());
        for (const char *name : {"actPlayPause", "actUndo", "actRedo", "actSplit", "actDelete", "actRippleDelete", "actSnapping",
                                 "actAddVideoTrack", "actAddAudioTrack", "actSelectAll"}) {
            auto *a = w.findChild<QAction *>(QString::fromLatin1(name));
            QVERIFY2(a, name);
            QVERIFY2(!a->property("bc_planned").toBool(), name);
        }
    }
};

QTEST_MAIN(TestPhase2)
#include "tst_phase2.moc"
