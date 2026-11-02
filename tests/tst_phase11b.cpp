// Phase 11 (part 2) acceptance tests: the subtitle strip on the timeline
// (drawing, moving, trimming, adding, picking with the Subtitles panel).
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/SubtitlesPanel.h"
#include "app/TimelinePanel.h"

#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QUndoStack>
#include <QtTest>

using namespace bc;

class TestPhase11b : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase11b"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
    }

    void strip()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        auto *panel = w.findChild<SubtitlesPanel *>(QStringLiteral("subtitlesPanel"));
        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("rightTabs"));
        QVERIFY(view && panel && tabs);
        view->setPixelsPerFrame(4.0);
        view->horizontalScrollBar()->setValue(0);
        view->setSnapping(false);
        QWidget *vp = view->viewport();

        // No subtitles: no strip, tracks start under the ruler.
        QCOMPARE(view->subtitleStripHeight(), 0);
        QCOMPARE(view->tracksTop(), view->rulerHeight());
        const int trackTop = view->trackRect(0).top();

        const QUuid a = ctl->addSubtitle(25, QStringLiteral("Birinchi"));
        const QUuid b = ctl->addSubtitle(150, QStringLiteral("Ikkinchi"));
        QVERIFY(view->subtitleStripHeight() > 0);
        QCOMPARE(view->trackRect(0).top(), trackTop + view->subtitleStripHeight()); // tracks moved down
        const QRect ra = view->subtitleRect(*ctl->timeline().subtitle(a));
        QCOMPARE(ra.left(), view->xForFrame(25));
        QCOMPARE(view->subtitleAt(ra.center()), a);
        QVERIFY(view->subtitleAt(QPoint(view->xForFrame(120), ra.center().y())).isNull());
        bool head = false, tail = false;
        QCOMPARE(view->subtitleAt(QPoint(ra.left() + 1, ra.center().y()), &head, &tail), a);
        QVERIFY(head && !tail);

        // A click picks it in the Subtitles panel (the playhead stays).
        ctl->setPlayhead(120);
        QTest::mouseClick(vp, Qt::LeftButton, {}, ra.center());
        QCOMPARE(view->selectedSubtitle(), a);
        QCOMPARE(tabs->currentWidget(), panel);
        QCOMPARE(panel->current(), a);
        QCOMPARE(ctl->playhead(), 120);
        // Picking in the panel marks it on the strip.
        panel->select(b);
        QCOMPARE(view->selectedSubtitle(), b);

        // Drag the body: moves by 40 px = 10 frames, one undo step.
        const int steps = ctl->undoStack()->count();
        const QPoint body = ra.center();
        QTest::mousePress(vp, Qt::LeftButton, {}, body);
        QTest::mouseMove(vp, body + QPoint(20, 0));
        QTest::mouseMove(vp, body + QPoint(40, 0));
        QCOMPARE(ctl->timeline().subtitle(a)->start, 25); // only a preview while dragging
        QTest::mouseRelease(vp, Qt::LeftButton, {}, body + QPoint(40, 0));
        QCOMPARE(ctl->timeline().subtitle(a)->start, 35);
        QCOMPARE(ctl->timeline().subtitle(a)->length, 75);
        QCOMPARE(ctl->undoStack()->count(), steps + 1);

        // Drag the tail edge: 2 s long.
        const QRect r2 = view->subtitleRect(*ctl->timeline().subtitle(a));
        const QPoint edge(r2.right() - 1, r2.center().y());
        QTest::mousePress(vp, Qt::LeftButton, {}, edge);
        QTest::mouseMove(vp, QPoint(view->xForFrame(60), edge.y()));
        QTest::mouseMove(vp, QPoint(view->xForFrame(85), edge.y()));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, QPoint(view->xForFrame(85), edge.y()));
        QCOMPARE(ctl->timeline().subtitle(a)->start, 35);
        QCOMPARE(ctl->timeline().subtitle(a)->length, 50);

        // Drag the head edge: start later, end stays.
        const QRect r3 = view->subtitleRect(*ctl->timeline().subtitle(a));
        const QPoint left(r3.left() + 1, r3.center().y());
        QTest::mousePress(vp, Qt::LeftButton, {}, left);
        QTest::mouseMove(vp, QPoint(view->xForFrame(40), left.y()));
        QTest::mouseMove(vp, QPoint(view->xForFrame(45), left.y()));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, QPoint(view->xForFrame(45), left.y()));
        QCOMPARE(ctl->timeline().subtitle(a)->start, 45);
        QCOMPARE(ctl->timeline().subtitle(a)->end(), 85);

        // Cannot move before the start of the sequence.
        const QRect r4 = view->subtitleRect(*ctl->timeline().subtitle(a));
        QTest::mousePress(vp, Qt::LeftButton, {}, r4.center());
        QTest::mouseMove(vp, r4.center() - QPoint(100, 0));
        QTest::mouseMove(vp, r4.center() - QPoint(400, 0));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, r4.center() - QPoint(400, 0));
        QCOMPARE(ctl->timeline().subtitle(a)->start, 0);

        ctl->undoStack()->undo();
        QCOMPARE(ctl->timeline().subtitle(a)->start, 45);

        // Double-click on a free place adds one there and opens it in the panel.
        const QPoint freePlace(view->xForFrame(260), ra.center().y());
        QTest::mouseDClick(vp, Qt::LeftButton, {}, freePlace);
        QCOMPARE(ctl->timeline().subtitles.size(), qsizetype(3));
        const Subtitle *c = ctl->timeline().subtitleAt(262);
        QVERIFY(c);
        QCOMPARE(c->start, 260);
        QCOMPARE(panel->current(), c->id);
        QCOMPARE(view->selectedSubtitle(), c->id);

        // With snapping on, a move snaps to the playhead.
        view->setSnapping(true);
        ctl->setPlayhead(100);
        const QRect rb = view->subtitleRect(*ctl->timeline().subtitle(b));
        const int toPlayhead = view->xForFrame(101) - rb.left(); // 1 frame away
        QTest::mousePress(vp, Qt::LeftButton, {}, rb.center());
        QTest::mouseMove(vp, rb.center() + QPoint(toPlayhead / 2, 0));
        QTest::mouseMove(vp, rb.center() + QPoint(toPlayhead, 0));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, rb.center() + QPoint(toPlayhead, 0));
        QCOMPARE(ctl->timeline().subtitle(b)->start, 100);

        // All removed: the strip goes away.
        ctl->removeSubtitles({a, b, c->id});
        QCOMPARE(view->subtitleStripHeight(), 0);
        QCOMPARE(view->trackRect(0).top(), trackTop);
    }
};

QTEST_MAIN(TestPhase11b)
#include "tst_phase11b.moc"
