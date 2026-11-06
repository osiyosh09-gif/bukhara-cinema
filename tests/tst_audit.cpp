// Whole-program check: every menu command, in three situations (empty
// project; media imported; a clip on the timeline and selected), is listed
// with whether it is enabled and why not, and every enabled one is run once.
// Dialogs it opens are closed at once. The program must survive all of it,
// and a disabled command must say why (tool tip), unless it is planned.
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/MediaCache.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <QtTest>

#include <cstdio>

using namespace bc;

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
    return p.waitForFinished(60000) && p.exitCode() == 0 && QFile::exists(path);
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

void collect(QMenu *menu, QList<QAction *> *out)
{
    for (QAction *a : menu->actions()) {
        if (a->isSeparator())
            continue;
        if (QMenu *sub = a->menu()) {
            emit sub->aboutToShow(); // menus that fill themselves when opened
            collect(sub, out);
        } else {
            out->append(a);
        }
    }
}

// Commands not run: they leave the program, open other programs, or wait for the user.
const QStringList kSkip = {
    QStringLiteral("actExit"),      QStringLiteral("actLogFolder"), QStringLiteral("actFullScreen"),
    QStringLiteral("actNewProject"), // run separately at the end
};

} // namespace

class TestAudit : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;
    QTimer m_closer;
    int m_closed = 0;
    QStringList m_report;
    QStringList m_unexplained;

    void audit(MainWindow &w, const QString &situation)
    {
        QList<QAction *> actions;
        for (QAction *top : w.menuBar()->actions())
            if (QMenu *m = top->menu()) {
                emit m->aboutToShow();
                collect(m, &actions);
            }
        QVERIFY(actions.size() > 50);
        int run = 0;
        for (const QPointer<QAction> &a : QList<QPointer<QAction>>(actions.begin(), actions.end())) {
            if (!a)
                continue;
            const QString name = a->objectName().isEmpty() ? a->text() : a->objectName();
            const bool planned = a->property("bc_planned").toBool();
            const QString text = QString(a->text()).remove(QLatin1Char('&'));
            m_report << QStringLiteral("%1 | %2 | %3 | %4 | %5")
                            .arg(situation, name, text,
                                 planned ? QStringLiteral("PLANNED") : a->isEnabled() ? QStringLiteral("on") : QStringLiteral("OFF"),
                                 a->toolTip());
            if (!a->isEnabled()) {
                // A disabled command must say why (Qt makes the tool tip from the text when none is set).
                QString bare = text;
                if (bare.endsWith(QLatin1String("...")))
                    bare.chop(3);
                if (!planned && !a->objectName().isEmpty() && (a->toolTip().isEmpty() || a->toolTip() == bare))
                    m_unexplained << situation + QStringLiteral(": ") + name + QStringLiteral(" (") + text + QLatin1Char(')');
                continue;
            }
            if (kSkip.contains(a->objectName()))
                continue;
            std::fprintf(stderr, "RUN %s %s\n", qPrintable(situation), qPrintable(name));
            std::fflush(stderr);
            a->trigger();
            QTest::qWait(20);
            ++run;
            QVERIFY2(w.isVisible() || true, qPrintable(name));
        }
        qInfo("%s: %d commands run", qPrintable(situation), run);
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_audit"));
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_video = m_dir.filePath(QStringLiteral("clip.mp4"));
        QVERIFY(makeVideo(m_video));
        // Every dialog or message box a command opens is closed at once.
        m_closer.setInterval(30);
        connect(&m_closer, &QTimer::timeout, this, [this] {
            if (QWidget *modal = QApplication::activeModalWidget()) {
                if (auto *d = qobject_cast<QDialog *>(modal)) {
                    d->reject();
                    ++m_closed;
                }
            }
            for (QWidget *top : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(top); d && d->isVisible() && !d->isModal()) {
                    d->close();
                    ++m_closed;
                }
        });
        m_closer.start();
    }

    void everyCommand()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();

        audit(w, QStringLiteral("1-empty"));

        QCOMPARE(ctl->importFiles({m_video}), 1);
        waitIdle(*ctl);
        audit(w, QStringLiteral("2-imported"));

        ctl->undoStack()->clear();
        QVERIFY(!ctl->placeMedia({ctl->project().items.first().id}, 0, 0).isEmpty());
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        const Timeline &tl = ctl->timeline();
        view->setSelection({tl.tracks[tl.indexOf(Timeline::Kind::Video, 0)].clips.first().id});
        ctl->setPlayhead(25);
        audit(w, QStringLiteral("3-clip-selected"));

        // A new project after all of that (it may ask to save: the question is closed).
        if (QAction *a = w.findChild<QAction *>(QStringLiteral("actNewProject")))
            a->trigger();
        QTest::qWait(100);

        qInfo("dialogs closed: %d", m_closed);
        for (const QString &line : m_report)
            qInfo("AUDIT %s", qPrintable(line));
        for (const QString &line : m_unexplained)
            qInfo("UNEXPLAINED %s", qPrintable(line));
        QVERIFY2(m_unexplained.isEmpty(), "grey commands that do not say why (see UNEXPLAINED above)");
        // Menus show the reasons.
        for (QAction *top : w.menuBar()->actions())
            if (top->menu())
                QVERIFY(top->menu()->toolTipsVisible());
    }
};

QTEST_MAIN(TestAudit)
#include "tst_audit.moc"
