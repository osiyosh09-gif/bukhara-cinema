// Phase 6 (part 1) acceptance tests: Source monitor, In/Out points,
// insert/overwrite edits, J/K/L shuttle and markers. Every edit is undone and
// redone.
#include "app/MainWindow.h"
#include "app/MediaBinPanel.h"
#include "app/MonitorView.h"
#include "app/ProjectController.h"
#include "core/MediaCache.h"
#include "engine/TimelinePlayer.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>
#include <QtTest>

using namespace bc;

namespace {

bool ffmpeg(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral(BC_FFMPEG_EXE), QStringList{QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error")} + args);
    return p.waitForFinished(120000) && p.exitCode() == 0;
}

// 4 s of testsrc (a running counter, so frames differ) with a tone.
bool makeVideo(const QString &path)
{
    return ffmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                   QStringLiteral("testsrc=size=320x240:rate=25:duration=4"),
                   QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                   QStringLiteral("sine=frequency=440:sample_rate=48000:duration=4"),
                   QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("4"),
                   QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest"), path})
           && QFile::exists(path);
}

double difference(const QImage &a0, const QImage &b0)
{
    const QImage a = a0.convertToFormat(QImage::Format_RGB32);
    const QImage b = b0.convertToFormat(QImage::Format_RGB32).scaled(a.size());
    double d = 0;
    int n = 0;
    for (int y = 0; y < a.height(); y += 4)
        for (int x = 0; x < a.width(); x += 4) {
            const QColor p = a.pixelColor(x, y), q = b.pixelColor(x, y);
            d += qAbs(p.red() - q.red()) + qAbs(p.green() - q.green()) + qAbs(p.blue() - q.blue());
            n += 3;
        }
    return n ? d / n : 255;
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

MediaItem fakeVideo(qint64 seconds = 10)
{
    MediaItem m;
    m.id = QUuid::createUuid();
    m.name = QStringLiteral("clip.mp4");
    m.probed = true;
    m.info.kind = MediaInfo::Kind::Video;
    m.info.durationUs = seconds * 1000000;
    m.info.width = 1920;
    m.info.height = 1080;
    m.info.videoCodec = QStringLiteral("h264");
    m.info.audioCodec = QStringLiteral("aac");
    m.info.channels = 2;
    m.info.sampleRate = 48000;
    return m;
}

const TimelineClip &clipOn(const Timeline &t, Timeline::Kind kind, int n, int index)
{
    return t.tracks[t.indexOf(kind, n)].clips[index];
}

int clipsOn(const Timeline &t, Timeline::Kind kind, int n)
{
    return int(t.tracks[t.indexOf(kind, n)].clips.size());
}

// Waits for the Source monitor to show the frame at the current position.
QImage sourceFrame(ProjectController &ctl)
{
    QSignalSpy spy(&ctl, &ProjectController::sourceFrameReady);
    for (int i = 0; i < 40 && spy.isEmpty(); ++i)
        spy.wait(100);
    return spy.isEmpty() ? QImage() : spy.last().at(0).value<QImage>();
}

} // namespace

class TestPhase6a : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase6a"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("counter.mp4"));
        QVERIFY(makeVideo(m_video));
    }

    // --- core model

    void markersInTheModel()
    {
        Timeline t = Timeline::makeDefault();
        const QUuid a = t.addMarker(50, QStringLiteral("B"));
        const QUuid b = t.addMarker(10, QStringLiteral("A"));
        QCOMPARE(t.addMarker(50), a); // one marker per frame
        QCOMPARE(t.markers.size(), 2);
        QCOMPARE(t.markers[0].id, b); // sorted by frame
        QCOMPARE(t.nextMarker(0), qint64(10));
        QCOMPARE(t.nextMarker(10), qint64(50));
        QCOMPARE(t.nextMarker(50), qint64(-1));
        QCOMPARE(t.prevMarker(50), qint64(10));
        QCOMPARE(t.prevMarker(10), qint64(-1));
        QVERIFY(t.snapPoints({}).contains(50));
        QVERIFY(t.renameMarker(a, QStringLiteral("Climax")));
        QVERIFY(!t.renameMarker(a, QStringLiteral("Climax")));

        Timeline other = t;
        QVERIFY(other == t);
        QVERIFY(other.removeMarker(b));
        QVERIFY(other != t); // markers are part of the edit (undo sees them)

        t.rescale(25, 50);
        QCOMPARE(t.markers[0].frame, qint64(20));
        QCOMPARE(t.markers[1].frame, qint64(100));

        // Saved in the project file (format 4) and read back.
        Project p;
        p.timeline = t;
        const QByteArray json = p.toJson(QString());
        QCOMPARE(QJsonDocument::fromJson(json).object().value(QStringLiteral("version")).toInt(), 4);
        Project back;
        QString err;
        QVERIFY2(Project::fromJson(json, QString(), back, &err), qPrintable(err));
        QCOMPARE(back.timeline.markers, t.markers);
        QCOMPARE(back.timeline.markers[1].name, QStringLiteral("Climax"));
    }

    void placePartOfSource()
    {
        Timeline t = Timeline::makeDefault();
        const MediaItem m = fakeVideo();
        const QList<QUuid> ids = t.place(m, 0, 0, 100, 25, 50);
        QCOMPARE(ids.size(), 2);
        const TimelineClip &v = clipOn(t, Timeline::Kind::Video, 0, 0);
        const TimelineClip &a = clipOn(t, Timeline::Kind::Audio, 0, 0);
        QCOMPARE(v.start, qint64(100));
        QCOMPARE(v.in, qint64(25));
        QCOMPARE(v.length, qint64(50));
        QCOMPARE(a.in, qint64(25));
        QVERIFY(!v.linkId.isNull());
        QCOMPARE(v.linkId, a.linkId);

        // A locked audio track: only the picture is placed, without a link.
        Timeline t2 = Timeline::makeDefault();
        t2.tracks[t2.indexOf(Timeline::Kind::Audio, 0)].locked = true;
        const QList<QUuid> one = t2.place(m, 0, 0, 0, 0, 25);
        QCOMPARE(one.size(), 1);
        QVERIFY(t2.clip(one.first())->linkId.isNull());
    }

    void insertSpaceSplitsAndShifts()
    {
        Timeline t = Timeline::makeDefault();
        const MediaItem m = fakeVideo();
        t.place(m, 0, 0, 0, 0, 100);     // V1+A1: 0..100
        t.place(m, 1, -1, 200, 0, 50);   // V2: 200..250
        t.tracks[t.indexOf(Timeline::Kind::Audio, 1)].locked = true;
        t.place(m, -1, 1, 0, 0, 100);    // locked A2 cannot get a clip
        QCOMPARE(clipsOn(t, Timeline::Kind::Audio, 1), 0);
        t.tracks[t.indexOf(Timeline::Kind::Audio, 1)].locked = false;
        t.place(m, -1, 1, 0, 0, 100);    // A2: 0..100
        t.tracks[t.indexOf(Timeline::Kind::Audio, 1)].locked = true;
        t.addMarker(40);
        t.addMarker(10);

        QVERIFY(t.insertSpace(40, 30));
        // V1 split at 40: 0..40 and 70..130 with the source continuing.
        QCOMPARE(clipsOn(t, Timeline::Kind::Video, 0), 2);
        QCOMPARE(clipOn(t, Timeline::Kind::Video, 0, 0).length, qint64(40));
        QCOMPARE(clipOn(t, Timeline::Kind::Video, 0, 1).start, qint64(70));
        QCOMPARE(clipOn(t, Timeline::Kind::Video, 0, 1).in, qint64(40));
        QCOMPARE(clipOn(t, Timeline::Kind::Video, 0, 1).length, qint64(60));
        // The right halves of V1 and A1 are linked to each other.
        QCOMPARE(clipOn(t, Timeline::Kind::Video, 0, 1).linkId, clipOn(t, Timeline::Kind::Audio, 0, 1).linkId);
        QVERIFY(clipOn(t, Timeline::Kind::Video, 0, 1).linkId != clipOn(t, Timeline::Kind::Video, 0, 0).linkId);
        // Later clips move; locked tracks do not change.
        QCOMPARE(clipOn(t, Timeline::Kind::Video, 1, 0).start, qint64(230));
        QCOMPARE(clipsOn(t, Timeline::Kind::Audio, 1), 1);
        QCOMPARE(clipOn(t, Timeline::Kind::Audio, 1, 0).length, qint64(100));
        // Markers at or after the insert point move with the edit.
        QCOMPARE(t.markers[0].frame, qint64(10));
        QCOMPARE(t.markers[1].frame, qint64(70));
    }

    // --- controller: Source monitor, In/Out, insert/overwrite

    void sourceMonitorShowsFrames()
    {
        ProjectController ctl;
        QCOMPARE(ctl.importFiles({m_video}), 1);
        waitIdle(ctl);
        const QUuid id = ctl.project().items.first().id;
        QSignalSpy changed(&ctl, &ProjectController::sourceChanged);
        QVERIFY(ctl.setSourceMedia(id));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(ctl.sourceMedia(), id);
        QCOMPARE(ctl.sourceDuration(), qint64(100)); // 4 s at 25 fps
        QVERIFY(!ctl.setSourceMedia(QUuid::createUuid()));

        const QImage first = sourceFrame(ctl);
        QVERIFY(!first.isNull());
        ctl.setSourcePosition(60);
        QCOMPARE(ctl.sourcePosition(), qint64(60));
        const QImage later = sourceFrame(ctl);
        QVERIFY(!later.isNull());
        QVERIFY2(difference(first, later) > 2, "the counter must change between frames");
        ctl.setSourcePosition(5000);
        QCOMPARE(ctl.sourcePosition(), qint64(99)); // kept inside the media

        // Removing the item closes the Source monitor.
        ctl.removeItems({id});
        QVERIFY(ctl.sourceMedia().isNull());
    }

    void inOutAndEdits()
    {
        ProjectController ctl;
        ctl.importFiles({m_video});
        waitIdle(ctl);
        const QUuid id = ctl.project().items.first().id;
        QVERIFY(ctl.setSourceMedia(id));

        qint64 in = -1, len = -1;
        QVERIFY(ctl.sourceRange(id, &in, &len));
        QCOMPARE(in, qint64(0));
        QCOMPARE(len, qint64(100));

        ctl.setSourcePosition(10);
        ctl.setSourceIn(ctl.sourcePosition());
        ctl.setSourceOut(29);
        QVERIFY(ctl.hasSourceIn() && ctl.hasSourceOut());
        QVERIFY(ctl.sourceRange(id, &in, &len));
        QCOMPARE(in, qint64(10));
        QCOMPARE(len, qint64(20));
        // An Out before the In removes the In.
        ctl.setSourceOut(5);
        QVERIFY(!ctl.hasSourceIn());
        ctl.clearSourceInOut();
        ctl.setSourceIn(10);
        ctl.setSourceOut(29);

        // Overwrite at the playhead: V1 and A1, the playhead jumps to the end.
        QVERIFY(ctl.overwriteEdit());
        const Timeline &t = ctl.timeline();
        QCOMPARE(clipsOn(t, Timeline::Kind::Video, 0), 1);
        QCOMPARE(clipOn(t, Timeline::Kind::Video, 0, 0).in, qint64(10));
        QCOMPARE(clipOn(t, Timeline::Kind::Video, 0, 0).length, qint64(20));
        QCOMPARE(clipOn(t, Timeline::Kind::Audio, 0, 0).length, qint64(20));
        QCOMPARE(ctl.playhead(), qint64(20));

        // Overwrite again in the middle: replaces 10..30.
        ctl.setPlayhead(10);
        QVERIFY(ctl.overwriteEdit());
        QCOMPARE(ctl.timeline().duration(), qint64(30));
        QCOMPARE(clipsOn(ctl.timeline(), Timeline::Kind::Video, 0), 2);

        // Insert at 5: everything from 5 moves right by 20.
        ctl.setPlayhead(5);
        QVERIFY(ctl.insertEdit());
        QCOMPARE(ctl.timeline().duration(), qint64(50));
        QCOMPARE(clipsOn(ctl.timeline(), Timeline::Kind::Video, 0), 4);
        QCOMPARE(clipOn(ctl.timeline(), Timeline::Kind::Video, 0, 1).start, qint64(5));
        QCOMPARE(clipOn(ctl.timeline(), Timeline::Kind::Video, 0, 1).in, qint64(10));
        QCOMPARE(clipOn(ctl.timeline(), Timeline::Kind::Video, 0, 2).start, qint64(25));
        QCOMPARE(ctl.playhead(), qint64(25));

        // Every edit can be undone and redone.
        const Timeline afterInsert = ctl.timeline();
        ctl.undoStack()->undo();
        QCOMPARE(ctl.timeline().duration(), qint64(30));
        ctl.undoStack()->undo();
        ctl.undoStack()->undo();
        QVERIFY(ctl.timeline().isEmpty());
        ctl.undoStack()->redo();
        ctl.undoStack()->redo();
        ctl.undoStack()->redo();
        QVERIFY(ctl.timeline() == afterInsert);

        // Dragging from the bin uses the In/Out part too.
        Timeline preview = ctl.timeline();
        QList<QUuid> made;
        QVERIFY(ctl.previewPlacement(preview, {id}, 0, 100, &made));
        QCOMPARE(preview.clip(made.first())->in, qint64(10));
        QCOMPARE(preview.clip(made.first())->length, qint64(20));

        // With every video and audio track locked there is nothing to do.
        for (int i = 0; i < ctl.timeline().tracks.size(); ++i)
            ctl.setTrackFlag(i, QStringLiteral("locked"), true);
        QVERIFY(!ctl.insertEdit());
        QVERIFY(!ctl.overwriteEdit());
    }

    void markersThroughController()
    {
        ProjectController ctl;
        const QUuid a = ctl.addMarker(30);
        QVERIFY(!a.isNull());
        QCOMPARE(ctl.addMarker(30), a);
        ctl.addMarker(80);
        QCOMPARE(ctl.undoStack()->count(), 2);
        QVERIFY(ctl.goToNextMarker());
        QCOMPARE(ctl.playhead(), qint64(30));
        QVERIFY(ctl.goToNextMarker());
        QCOMPARE(ctl.playhead(), qint64(80));
        QVERIFY(!ctl.goToNextMarker());
        QVERIFY(ctl.goToPreviousMarker());
        QCOMPARE(ctl.playhead(), qint64(30));
        QVERIFY(ctl.renameMarker(a, QStringLiteral("Intro")));
        QCOMPARE(ctl.timeline().markerAt(30)->name, QStringLiteral("Intro"));
        QVERIFY(ctl.removeMarker(a));
        QCOMPARE(ctl.timeline().markers.size(), 1);
        ctl.undoStack()->undo();
        QCOMPARE(ctl.timeline().markerAt(30)->name, QStringLiteral("Intro"));
        ctl.undoStack()->undo();
        QVERIFY(ctl.timeline().markerAt(30)->name.isEmpty());

        // Saved with the project.
        const QString file = m_dir.filePath(QStringLiteral("markers.bcproj"));
        QString err;
        QVERIFY2(ctl.saveAs(file, &err), qPrintable(err));
        ProjectController other;
        QVERIFY2(other.open(file, &err), qPrintable(err));
        QCOMPARE(other.timeline().markers.size(), 2);
        QVERIFY(other.timeline().markerAt(80));
    }

    void shuttleSpeeds()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        ctl.importFiles({m_video});
        waitIdle(ctl);
        const QUuid id = ctl.project().items.first().id;
        QVERIFY(ctl.setSourceMedia(id));
        using M = ProjectController::Monitor;

        ctl.shuttle(M::Source, +1);
        QVERIFY(ctl.isSourcePlaying());
        QCOMPARE(ctl.speed(M::Source), 1.0);
        ctl.shuttle(M::Source, +1);
        QCOMPARE(ctl.speed(M::Source), 2.0);
        ctl.shuttle(M::Source, +1);
        ctl.shuttle(M::Source, +1);
        ctl.shuttle(M::Source, +1);
        QCOMPARE(ctl.speed(M::Source), 8.0); // the limit
        ctl.shuttle(M::Source, -1);
        QCOMPARE(ctl.speed(M::Source), -1.0);
        ctl.shuttle(M::Source, -1);
        QCOMPARE(ctl.speed(M::Source), -2.0);
        ctl.shuttle(M::Source, 0);
        QVERIFY(!ctl.isSourcePlaying());
        QCOMPARE(ctl.speed(M::Source), 0.0);

        // Twice as fast forwards.
        ctl.setSourcePosition(0);
        ctl.shuttle(M::Source, +1);
        ctl.shuttle(M::Source, +1);
        QTest::qWait(800);
        ctl.shuttle(M::Source, 0);
        QVERIFY2(ctl.sourcePosition() > 20, qPrintable(QString::number(ctl.sourcePosition())));
        // Backwards from the start: nothing to play.
        ctl.setSourcePosition(0);
        ctl.shuttle(M::Source, -1);
        QVERIFY(!ctl.isSourcePlaying());

        // Backwards really goes back.
        ctl.setSourcePosition(80);
        ctl.shuttle(M::Source, -1);
        QSignalSpy pos(&ctl, &ProjectController::sourcePositionChanged);
        for (int i = 0; i < 30 && pos.count() < 5; ++i)
            pos.wait(100);
        ctl.shuttle(M::Source, 0);
        QVERIFY(pos.count() >= 2);
        QVERIFY(ctl.sourcePosition() < 80);
        QVERIFY(ctl.sourcePosition() > 40); // no jump to the start

        // Backwards to the start: playback stops there.
        ctl.setSourcePosition(6);
        ctl.shuttle(M::Source, -1);
        ctl.shuttle(M::Source, -1);
        QTRY_VERIFY_WITH_TIMEOUT(!ctl.isSourcePlaying(), 3000);
        QCOMPARE(ctl.sourcePosition(), qint64(0));

        // J then L: the direction changes where the picture is.
        ctl.setSourcePosition(50);
        ctl.shuttle(M::Source, -1);
        QTest::qWait(200);
        ctl.shuttle(M::Source, +1);
        QCOMPARE(ctl.speed(M::Source), 1.0);
        QVERIFY(ctl.isSourcePlaying());
        ctl.shuttle(M::Source, 0);

        // One monitor plays at a time.
        ctl.overwriteEdit();
        ctl.setPlayhead(0);
        ctl.shuttle(M::Program, +1);
        QVERIFY(ctl.isPlaying());
        ctl.shuttle(M::Source, +1);
        QVERIFY(ctl.isSourcePlaying());
        QVERIFY(!ctl.isPlaying());
        ctl.togglePlay();
        QVERIFY(ctl.isPlaying());
        QVERIFY(!ctl.isSourcePlaying());
        ctl.pause();
    }

    // --- main window

    void windowActions()
    {
        MainWindow w;
        ProjectController *ctl = w.controller();
        auto act = [&](const char *name) { return w.findChild<QAction *>(QString::fromLatin1(name)); };
        for (const char *name : {"actAddMarker", "actNextMarker", "actPrevMarker", "actRenameMarker",
                                 "actDeleteMarker", "actMarkIn", "actMarkOut", "actClearInOut", "actInsert",
                                 "actOverwrite", "actShuttleBack", "actShuttleStop", "actShuttleForward"}) {
            QAction *a = act(name);
            QVERIFY2(a, name);
            QVERIFY2(!a->property("bc_planned").toBool(), name); // real features, not placeholders
        }
        QCOMPARE(act("actAddMarker")->shortcut(), QKeySequence(QStringLiteral("M")));
        QCOMPARE(act("actInsert")->shortcut(), QKeySequence(Qt::Key_Comma));
        QCOMPARE(act("actOverwrite")->shortcut(), QKeySequence(Qt::Key_Period));
        QCOMPARE(act("actShuttleForward")->shortcut(), QKeySequence(QStringLiteral("L")));

        // Without a source clip the source actions are off.
        QVERIFY(!act("actInsert")->isEnabled());
        QVERIFY(!act("actMarkIn")->isEnabled());
        QVERIFY(!act("actNextMarker")->isEnabled());

        // Double-click in the bin opens the clip in the Source monitor.
        ctl->importFiles({m_video});
        waitIdle(*ctl);
        auto *bin = w.findChild<MediaBinPanel *>();
        QVERIFY(bin);
        QTreeWidgetItem *row = nullptr;
        for (QTreeWidgetItemIterator it(bin->tree()); *it; ++it)
            if (!(*it)->data(0, MediaBinPanel::IsFolderRole).toBool())
                row = *it;
        QVERIFY(row);
        emit bin->tree()->itemDoubleClicked(row, 0);
        QCOMPARE(ctl->sourceMedia(), ctl->project().items.first().id);
        QVERIFY(act("actInsert")->isEnabled());
        auto *source = w.findChild<MonitorView *>(QStringLiteral("sourceMonitor"));
        QVERIFY(source->isActive());
        QVERIFY(w.findChild<QToolButton *>(QStringLiteral("mb_actInsert")));

        // I / O through the actions, then "." puts the part on the timeline.
        ctl->setSourcePosition(20);
        act("actMarkIn")->trigger();
        ctl->setSourcePosition(44);
        act("actMarkOut")->trigger();
        act("actOverwrite")->trigger();
        QCOMPARE(ctl->timeline().duration(), qint64(25));
        act("actInsert")->trigger();
        QCOMPARE(ctl->timeline().duration(), qint64(50));

        // M adds a marker at the playhead; next/previous jump between them.
        ctl->setPlayhead(10);
        act("actAddMarker")->trigger();
        QVERIFY(ctl->timeline().markerAt(10));
        QVERIFY(act("actDeleteMarker")->isEnabled());
        ctl->setPlayhead(0);
        QVERIFY(act("actNextMarker")->isEnabled());
        act("actNextMarker")->trigger();
        QCOMPARE(ctl->playhead(), qint64(10));
        act("actDeleteMarker")->trigger();
        QVERIFY(ctl->timeline().markers.isEmpty());
        ctl->undoStack()->undo();
        QCOMPARE(ctl->timeline().markers.size(), 1);

        // Clicking the Program monitor makes it the active one.
        auto *program = w.findChild<MonitorView *>(QStringLiteral("programMonitor"));
        emit program->activated();
        QVERIFY(program->isActive());
        QVERIFY(!source->isActive());
    }
};

QTEST_MAIN(TestPhase6a)
#include "tst_phase6a.moc"
