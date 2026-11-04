// Phase 13 (part 1) acceptance tests: the export queue (project copies,
// one export after another, failures, stop, dialogs).
#include "app/ExportDialog.h"
#include "app/ExportQueue.h"
#include "app/ExportQueueDialog.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "core/MediaProbe.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

using namespace bc;
using State = ExportQueue::Job::State;

namespace {

ExportSettings settingsFor(const QString &path)
{
    ExportSettings s;
    s.path = path;
    s.encoder = MediaProbe::usableVideoEncoders().last();
    s.videoKbps = 1000;
    return s;
}

double seconds(const QString &file)
{
    MediaInfo info;
    QString err;
    return MediaProbe::probe(file, info, &err) ? info.durationUs / 1e6 : -1;
}

} // namespace

class TestPhase13a : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase13a"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QVERIFY(m_dir.isValid());
    }

    void queueKeepsCopiesAndRunsInOrder()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        ExportQueue queue(&ctl);
        ctl.addTitle(0); // 5 s
        const QString a = m_dir.filePath(QStringLiteral("a.mp4"));
        const QString b = m_dir.filePath(QStringLiteral("sub/b.mp4")); // folder made when it runs
        QString error;
        const QUuid ja = queue.add(ctl.project(), settingsFor(a), QStringLiteral("A"), &error);
        QVERIFY(!ja.isNull());
        // The same file twice is refused (also spelled differently).
        QVERIFY(queue.add(ctl.project(), settingsFor(m_dir.filePath(QStringLiteral("./A.mp4"))), QStringLiteral("A again"), &error)
                    .isNull());
        QVERIFY(!error.isEmpty());
        // Editing after adding does not change the queued job.
        ctl.addTitle(125); // now 10 s
        const QUuid jb = queue.add(ctl.project(), settingsFor(b), QStringLiteral("B"));
        QVERIFY(!jb.isNull());
        QCOMPARE(queue.waitingCount(), 2);
        QCOMPARE(queue.job(ja)->total, 125);
        QCOMPARE(queue.job(jb)->total, 250);

        QSignalSpy all(&queue, &ExportQueue::finishedAll);
        QSignalSpy prog(&queue, &ExportQueue::jobProgress);
        queue.start();
        QVERIFY(queue.isRunning());
        QCOMPARE(queue.job(ja)->state, State::Running);
        QCOMPARE(queue.job(jb)->state, State::Waiting);
        // The Export dialog cannot start another export meanwhile.
        QVERIFY(!ctl.startExport(settingsFor(m_dir.filePath(QStringLiteral("c.mp4"))), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(all.wait(240000));
        QCOMPARE(all.first().at(0).toInt(), 2);
        QCOMPARE(all.first().at(1).toInt(), 0);
        QVERIFY(!queue.isRunning());
        QVERIFY(prog.count() >= 2);
        QCOMPARE(queue.job(ja)->state, State::Done);
        QCOMPARE(queue.job(jb)->state, State::Done);
        QVERIFY2(std::abs(seconds(a) - 5.0) < 0.3, qPrintable(QString::number(seconds(a))));
        QVERIFY2(std::abs(seconds(b) - 10.0) < 0.3, qPrintable(QString::number(seconds(b))));
        QCOMPARE(queue.clearFinished(), 2);
        QVERIFY(queue.jobs().isEmpty());
    }

    void failuresStopAndRetry()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        ExportQueue queue(&ctl);
        ctl.addTitle(0);
        // A folder that cannot be made (a file is in the way).
        QFile blocker(m_dir.filePath(QStringLiteral("blocker")));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        const QUuid bad = queue.add(ctl.project(), settingsFor(m_dir.filePath(QStringLiteral("blocker/x.mp4"))),
                                    QStringLiteral("bad"));
        const QString goodPath = m_dir.filePath(QStringLiteral("good.mp4"));
        const QUuid good = queue.add(ctl.project(), settingsFor(goodPath), QStringLiteral("good"));
        QSignalSpy all(&queue, &ExportQueue::finishedAll);
        queue.start();
        QVERIFY(all.wait(240000));
        QCOMPARE(queue.job(bad)->state, State::Failed); // the queue went on after it
        QVERIFY(!queue.job(bad)->error.isEmpty());
        QCOMPARE(queue.job(good)->state, State::Done);
        QCOMPARE(all.first().at(1).toInt(), 1);
        QVERIFY(!queue.retry(good)); // only failed or cancelled jobs
        QVERIFY(queue.retry(bad));
        QCOMPARE(queue.job(bad)->state, State::Waiting);
        QVERIFY(queue.remove(bad));
        QVERIFY(!queue.job(bad));

        // Stop: the running export is cancelled, its unfinished file deleted, the rest waits.
        ctl.addTitle(125);
        ctl.addTitle(250);
        const QString longPath = m_dir.filePath(QStringLiteral("long.mp4"));
        const QUuid longJob = queue.add(ctl.project(), settingsFor(longPath), QStringLiteral("long"));
        const QUuid after = queue.add(ctl.project(), settingsFor(m_dir.filePath(QStringLiteral("after.mp4"))),
                                      QStringLiteral("after"));
        queue.start();
        QCOMPARE(queue.job(longJob)->state, State::Running);
        QVERIFY(!queue.remove(longJob)); // not while it runs
        queue.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!queue.isRunning(), 30000);
        QCOMPARE(queue.job(longJob)->state, State::Cancelled);
        QCOMPARE(queue.job(after)->state, State::Waiting);
        QVERIFY(!QFile::exists(longPath));
        QVERIFY(!ctl.isExporting());
        QVERIFY(queue.retry(longJob));
        QCOMPARE(queue.waitingCount(), 2);
    }

    void dialogs()
    {
        MainWindow w;
        w.resize(1200, 700);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        ExportQueue *queue = w.exportQueue();
        QVERIFY(queue);
        QAction *act = w.findChild<QAction *>(QStringLiteral("actExportQueue"));
        QVERIFY(act && act->isEnabled());
        if (!ctl->canPlay())
            return;
        ctl->addTitle(0);

        ExportDialog dlg(ctl, &w, queue);
        auto *add = dlg.findChild<QPushButton *>(QStringLiteral("exportAddQueue"));
        QVERIFY(add && !add->isHidden());
        dlg.setPath(m_dir.filePath(QStringLiteral("queued")));
        QVERIFY(dlg.addToQueue());
        QCOMPARE(queue->jobs().size(), qsizetype(1));
        QCOMPARE(queue->jobs().first().settings.path, m_dir.filePath(QStringLiteral("queued.mp4"))); // .mp4 added
        QVERIFY(!dlg.addToQueue()); // the same file again
        QVERIFY(!dlg.findChild<QLabel *>(QStringLiteral("exportStatus"))->text().isEmpty());
        // Without a queue there is no such button.
        ExportDialog plain(ctl, &w);
        QVERIFY(plain.findChild<QPushButton *>(QStringLiteral("exportAddQueue"))->isHidden());

        act->trigger();
        auto *qd = w.findChild<ExportQueueDialog *>(QStringLiteral("exportQueueDialog"));
        QVERIFY(qd && qd->isVisible());
        auto *list = qd->findChild<QTreeWidget *>(QStringLiteral("exportQueueList"));
        auto *start = qd->findChild<QPushButton *>(QStringLiteral("exportQueueStart"));
        auto *stop = qd->findChild<QPushButton *>(QStringLiteral("exportQueueStop"));
        auto *remove = qd->findChild<QPushButton *>(QStringLiteral("exportQueueRemove"));
        QVERIFY(list && start && stop && remove);
        QCOMPARE(list->topLevelItemCount(), 1);
        QVERIFY(start->isEnabled());
        QVERIFY(!stop->isEnabled());
        QVERIFY(remove->isEnabled());

        // The plain dialog does not show the queue's progress as its own.
        QSignalSpy all(queue, &ExportQueue::finishedAll);
        start->click();
        QVERIFY(stop->isEnabled());
        QVERIFY(!remove->isEnabled());
        QVERIFY(all.wait(240000));
        QVERIFY(QFile::exists(m_dir.filePath(QStringLiteral("queued.mp4"))));
        QVERIFY(list->topLevelItem(0)->text(2).contains(ExportQueueDialog::tr("Done")));
        QVERIFY(plain.findChild<QLabel *>(QStringLiteral("exportStatus"))->text().isEmpty());
        qd->close();
    }
};

QTEST_MAIN(TestPhase13a)
#include "tst_phase13a.moc"
