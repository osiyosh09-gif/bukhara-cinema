// Phase 6 (part 3) acceptance tests: nested sequences (model, file, engine,
// editing inside, undo) and custom keyboard shortcuts.
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/Shortcuts.h"
#include "app/TimelinePanel.h"
#include "core/MediaCache.h"
#include "engine/TimelinePlayer.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
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

int count(const Timeline &t, Kind kind, int n)
{
    return int(t.tracks[t.indexOf(kind, n)].clips.size());
}

const TimelineClip &at(const Timeline &t, Kind kind, int n, int index)
{
    return t.tracks[t.indexOf(kind, n)].clips[index];
}

} // namespace

class TestPhase6c : public QObject
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
        return media;
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase6c"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("counter.mp4"));
        QVERIFY(makeVideo(m_video));
    }

    void nestMovesClipsIntoOne()
    {
        ProjectController ctl;
        twoClips(ctl);
        const Timeline before = ctl.timeline();
        const QUuid nest = ctl.nestClips({at(before, Kind::Video, 0, 0).id, at(before, Kind::Video, 0, 1).id});
        QVERIFY(!nest.isNull());
        QCOMPARE(ctl.project().nested.size(), 1);

        // Main sequence: one picture and one sound clip of the nest, linked, same length.
        const Timeline &main = ctl.timeline();
        QCOMPARE(count(main, Kind::Video, 0), 1);
        QCOMPARE(count(main, Kind::Audio, 0), 1);
        const TimelineClip &v = at(main, Kind::Video, 0, 0);
        QCOMPARE(v.nestId, nest);
        QVERIFY(v.isNested() && !v.isTitle());
        QCOMPARE(v.length, qint64(200));
        QVERIFY(main.isLinked(v.id));
        QCOMPARE(main.duration(), qint64(200));

        // The nested sequence holds the clips on the same tracks.
        const Timeline &inner = ctl.project().nested.first().timeline;
        QCOMPARE(count(inner, Kind::Video, 0), 2);
        QCOMPARE(count(inner, Kind::Audio, 0), 2);
        QCOMPARE(at(inner, Kind::Video, 0, 1).start, qint64(100));

        // One undo step.
        ctl.undoStack()->undo();
        QVERIFY(ctl.timeline() == before);
        QVERIFY(ctl.project().nested.isEmpty());
        ctl.undoStack()->redo();
        QCOMPARE(ctl.project().nested.size(), 1);

        // Locked tracks cannot be nested.
        ctl.setTrackFlag(main.indexOf(Kind::Audio, 0), QStringLiteral("locked"), true);
        QVERIFY(ctl.nestClips({at(ctl.timeline(), Kind::Video, 0, 0).id}).isNull());
    }

    void nestedProjectFile()
    {
        ProjectController ctl;
        twoClips(ctl);
        const QUuid nest = ctl.nestClips({at(ctl.timeline(), Kind::Video, 0, 0).id});
        QVERIFY(ctl.renameSequence(nest, QStringLiteral("Intro")));
        const QString file = m_dir.filePath(QStringLiteral("nested.bcproj"));
        QString err;
        QVERIFY2(ctl.saveAs(file, &err), qPrintable(err));
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("version")).toInt(), 5);

        ProjectController other;
        QVERIFY2(other.open(file, &err), qPrintable(err));
        QCOMPARE(other.project().nested.size(), 1);
        QCOMPARE(other.sequenceName(nest), QStringLiteral("Intro"));
        QCOMPARE(at(other.timeline(), Kind::Video, 0, 0).nestId, nest);
        QVERIFY(other.project().nested.first().timeline == ctl.project().nested.first().timeline);

        // Collecting keeps media used only inside the nest.
        const MediaItem m = ctl.project().items.first();
        bool usedInside = false;
        for (const TimelineTrack &t : other.project().nested.first().timeline.tracks)
            for (const TimelineClip &c : t.clips)
                usedInside |= c.mediaId == m.id;
        QVERIFY(usedInside);
    }

    void engineShowsTheNestLikeTheClips()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        twoClips(ctl);
        TimelinePlayer before;
        before.setPreviewLimit(QSize(320, 240));
        before.setProject(ctl.project());
        const QImage a = before.renderFrame(130);
        const double soundA = before.audioLevel(130);

        ctl.nestClips({at(ctl.timeline(), Kind::Video, 0, 0).id, at(ctl.timeline(), Kind::Video, 0, 1).id});
        TimelinePlayer after;
        after.setPreviewLimit(QSize(320, 240));
        after.setProject(ctl.project());
        const QImage b = after.renderFrame(130);
        QVERIFY(!a.isNull() && !b.isNull());
        QVERIFY2(difference(a, b) < 3, qPrintable(QString::number(difference(a, b))));
        const double soundB = after.audioLevel(130);
        QVERIFY(soundA > 0.05);
        QVERIFY2(qAbs(soundA - soundB) < 0.02, qPrintable(QStringLiteral("%1 %2").arg(soundA).arg(soundB)));

        // A nested clip on V2 lets V1 show where it has nothing.
        const QUuid media = ctl.project().items.first().id;
        ctl.undoStack()->undo(); // back to two clips
        ctl.addTitle(0);         // a title on a free upper track
        QUuid title;
        for (const TimelineTrack &t : ctl.timeline().tracks)
            for (const TimelineClip &c : t.clips)
                if (c.isTitle())
                    title = c.id;
        QVERIFY(!title.isNull());
        TimelinePlayer withTitle;
        withTitle.setPreviewLimit(QSize(320, 240));
        withTitle.setProject(ctl.project());
        const QImage c1 = withTitle.renderFrame(20);
        QVERIFY(!ctl.nestClips({title}).isNull());
        TimelinePlayer nestedTitle;
        nestedTitle.setPreviewLimit(QSize(320, 240));
        nestedTitle.setProject(ctl.project());
        const QImage c2 = nestedTitle.renderFrame(20);
        QVERIFY2(difference(c1, c2) < 3, qPrintable(QString::number(difference(c1, c2))));
        Q_UNUSED(media);
    }

    void editInsideTheNest()
    {
        ProjectController ctl;
        twoClips(ctl);
        const QUuid nest = ctl.nestClips({at(ctl.timeline(), Kind::Video, 0, 0).id, at(ctl.timeline(), Kind::Video, 0, 1).id});
        QSignalSpy switched(&ctl, &ProjectController::openSequenceChanged);
        ctl.setPlayhead(150);
        QVERIFY(ctl.openSequence(nest));
        QCOMPARE(ctl.openNest(), nest);
        QCOMPARE(switched.count(), 1);
        QCOMPARE(ctl.playhead(), qint64(0)); // each sequence has its own playhead
        QCOMPARE(count(ctl.timeline(), Kind::Video, 0), 2);

        // Remove the second clip inside: the nest gets shorter.
        QVERIFY(ctl.removeClips(ctl.timeline().withLinked({at(ctl.timeline(), Kind::Video, 0, 1).id}), false));
        QCOMPARE(ctl.project().nest(nest)->timeline.duration(), qint64(100));

        // Back to the main sequence; the playhead is where it was.
        QVERIFY(ctl.openSequence({}));
        QCOMPARE(ctl.playhead(), qint64(150));
        QCOMPARE(count(ctl.timeline(), Kind::Video, 0), 1);

        // Undo of the inner edit opens the nest again and restores it.
        ctl.undoStack()->undo();
        QCOMPARE(ctl.openNest(), nest);
        QCOMPARE(ctl.project().nest(nest)->timeline.duration(), qint64(200));
        QVERIFY(!ctl.openSequence(QUuid::createUuid()));
        QCOMPARE(ctl.sequenceName({}), ctl.tr("Main sequence"));
    }

    // --- shortcuts

    void customShortcuts()
    {
        QSettings().remove(QStringLiteral("shortcuts"));
        {
            MainWindow w;
            auto act = [&](const char *name) { return w.findChild<QAction *>(QString::fromLatin1(name)); };
            QVERIFY(!act("actShortcuts")->property("bc_planned").toBool());
            QVERIFY(act("actShortcuts")->isEnabled());
            const QList<QAction *> list = Shortcuts::actions(&w);
            QVERIFY(list.contains(act("actAddMarker")));
            QVERIFY(list.contains(act("actToolRazor")));
            for (QAction *a : list)
                QVERIFY2(!a->property("bc_planned").toBool(), qPrintable(a->objectName()));

            ShortcutsDialog dlg(&w);
            auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("shortcutTable"));
            QCOMPARE(table->rowCount(), int(list.size()));
            auto selectAction = [&](const QString &name) {
                for (int i = 0; i < table->rowCount(); ++i)
                    if (table->item(i, 0)->data(Qt::UserRole).toString() == name)
                        table->selectRow(i);
            };
            // K stops playback by default; give it to "Add marker".
            selectAction(QStringLiteral("actAddMarker"));
            dlg.assignToSelected(QKeySequence(QStringLiteral("K")));
            QVERIFY(!dlg.findChild<QLabel *>(QStringLiteral("shortcutNote"))->text().isEmpty()); // says who lost it
            dlg.apply();
            QCOMPARE(act("actAddMarker")->shortcut(), QKeySequence(QStringLiteral("K")));
            QVERIFY(act("actShuttleStop")->shortcut().isEmpty());
            QCOMPARE(QSettings().value(QStringLiteral("shortcuts/actAddMarker")).toString(), QStringLiteral("K"));
        }
        {
            // The next start uses the saved keys; defaults are still known.
            MainWindow w;
            auto act = [&](const char *name) { return w.findChild<QAction *>(QString::fromLatin1(name)); };
            QCOMPARE(act("actAddMarker")->shortcut(), QKeySequence(QStringLiteral("K")));
            QCOMPARE(Shortcuts::defaults(act("actAddMarker")).value(0), QKeySequence(QStringLiteral("M")));
            ShortcutsDialog dlg(&w);
            dlg.resetAll();
            dlg.apply();
            QCOMPARE(act("actAddMarker")->shortcut(), QKeySequence(QStringLiteral("M")));
            QCOMPARE(act("actShuttleStop")->shortcut(), QKeySequence(QStringLiteral("K")));
            QVERIFY(!QSettings().contains(QStringLiteral("shortcuts/actAddMarker")));
            // Redo keeps both of its built-in keys.
            QCOMPARE(act("actRedo")->shortcuts().size(), 2);
        }
    }

    // --- main window

    void nestInTheWindow()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        twoClips(*ctl);
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        auto *nest = w.findChild<QAction *>(QStringLiteral("actNest"));
        QVERIFY(nest && !nest->property("bc_planned").toBool());
        QVERIFY(!nest->isEnabled());
        view->setSelection({at(ctl->timeline(), Kind::Video, 0, 0).id});
        QVERIFY(nest->isEnabled());
        nest->trigger();
        QCOMPARE(ctl->project().nested.size(), 1);
        auto *combo = w.findChild<QComboBox *>(QStringLiteral("timelineSequence"));
        QCOMPARE(combo->count(), 2);

        // Double-click on the nested clip opens it.
        view->setPixelsPerFrame(4.0);
        view->horizontalScrollBar()->setValue(0);
        const QPoint p(view->xForFrame(50), view->trackRect(0).center().y());
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, {}, p);
        QCOMPARE(ctl->openNest(), ctl->project().nested.first().id);
        QCOMPARE(combo->currentIndex(), 1);
        // The list goes back to the main sequence.
        combo->activated(0);
        QVERIFY(ctl->openNest().isNull());
    }
};

QTEST_MAIN(TestPhase6c)
#include "tst_phase6c.moc"
