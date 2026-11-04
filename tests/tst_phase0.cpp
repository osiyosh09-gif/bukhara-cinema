// Phase 0 acceptance tests that can run without a screen (offscreen platform).
#include "app/AboutDialog.h"
#include "app/FeatureStatus.h"
#include "app/Language.h"
#include "app/MainWindow.h"
#include "app/MonitorView.h"
#include "app/PlaceholderPanel.h"
#include "app/StatusDialog.h"
#include "app/Theme.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

using namespace bc;

class TestPhase0 : public QObject
{
    Q_OBJECT

private:
    static QMenu *menuAt(MainWindow &w, int index)
    {
        return w.menuBar()->actions().at(index)->menu();
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setOrganizationName(QStringLiteral("BUKHARA CINEMA Tests"));
        QApplication::setApplicationName(QStringLiteral("tst_phase0"));
        QSettings().clear();
        Theme::apply(*qApp);
    }

    void fiveZonesExist()
    {
        MainWindow w;
        w.show();
        QVERIFY(w.findChild<QDockWidget *>(QStringLiteral("mediaDock")));
        QVERIFY(w.findChild<QDockWidget *>(QStringLiteral("inspectorDock")));
        QVERIFY(w.findChild<QDockWidget *>(QStringLiteral("timelineDock")));
        QVERIFY(w.findChild<MonitorView *>(QStringLiteral("sourceMonitor")));
        QVERIFY(w.findChild<MonitorView *>(QStringLiteral("programMonitor")));
        QVERIFY(w.findChild<QToolBar *>(QStringLiteral("mainToolbar")));
        QCOMPARE(w.menuBar()->actions().size(), 9);

        QCOMPARE(w.dockWidgetArea(w.findChild<QDockWidget *>(QStringLiteral("mediaDock"))), Qt::LeftDockWidgetArea);
        QCOMPARE(w.dockWidgetArea(w.findChild<QDockWidget *>(QStringLiteral("inspectorDock"))), Qt::RightDockWidgetArea);
        QCOMPARE(w.dockWidgetArea(w.findChild<QDockWidget *>(QStringLiteral("timelineDock"))), Qt::BottomDockWidgetArea);

        // Right panel has the four required sections.
        auto *right = w.findChild<QTabWidget *>(QStringLiteral("rightTabs"));
        QVERIFY(right);
        QCOMPARE(right->count(), 4);
        auto *left = w.findChild<QTabWidget *>(QStringLiteral("leftTabs"));
        QVERIFY(left);
        QCOMPARE(left->count(), 2);
    }

    void plannedFeaturesAreDisabled()
    {
        MainWindow w;
        int planned = 0;
        for (QAction *a : w.findChildren<QAction *>()) {
            if (a->property("bc_planned").toBool()) {
                ++planned;
                QVERIFY2(!a->isEnabled(), qPrintable(a->objectName()));
            }
        }
        QVERIFY(planned >= 12); // phases 2 and 3 turned editing, "New text" and "Audio gain" into real actions

        // Source monitor cannot play yet: its buttons must not pretend to.
        // (The Program monitor plays the timeline since phase 2.)
        auto *source = w.findChild<MonitorView *>(QStringLiteral("sourceMonitor"));
        for (auto *b : source->findChildren<QToolButton *>())
            QVERIFY(!b->isEnabled());

        // Working items are enabled.
        for (const char *name : {"actSettings", "actExit", "actResetLayout", "actStatus", "actLicenses", "actAbout",
                                 "actFullScreen", "actLogFolder", "actNewText"}) {
            auto *a = w.findChild<QAction *>(QString::fromLatin1(name));
            QVERIFY2(a, name);
            QVERIFY2(a->isEnabled(), name);
        }
    }

    void languageSwitchUpdatesUi_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("editMenu");
        QTest::addColumn<QString>("mediaBin");
        QTest::addColumn<QString>("phaseTag");
        QTest::newRow("uz_Cyrl") << "uz_Cyrl" << "&Таҳрир" << "Медиа кутубхонаси" << "[6-босқич]";
        QTest::newRow("uz_Latn") << "uz_Latn" << "&Tahrir" << "Media kutubxonasi" << "[6-bosqich]";
        QTest::newRow("ru") << "ru" << "&Правка" << "Медиатека" << "[этап 6]";
    }

    void languageSwitchUpdatesUi()
    {
        QFETCH(QString, code);
        QFETCH(QString, editMenu);
        QFETCH(QString, mediaBin);
        QFETCH(QString, phaseTag);

        MainWindow w;
        w.show();
        QVERIFY(Language::instance().setLanguage(code));
        QCoreApplication::processEvents();

        QCOMPARE(Language::instance().current(), code);
        QCOMPARE(menuAt(w, 1)->title(), editMenu);
        QCOMPARE(w.findChild<QTabWidget *>(QStringLiteral("leftTabs"))->tabText(0), mediaBin);
        auto *planned = w.findChild<QAction *>(QStringLiteral("actCut"));
        QVERIFY(planned->text().endsWith(phaseTag));

        // Persisted for the next start.
        QCOMPARE(QSettings().value(QStringLiteral("ui/language")).toString(), code);
    }

    void languageNamesAreNative()
    {
        QCOMPARE(Language::nativeName(QStringLiteral("uz_Cyrl")), QStringLiteral("Ўзбекча (кирилл)"));
        QCOMPARE(Language::nativeName(QStringLiteral("uz_Latn")), QStringLiteral("O‘zbekcha (lotin)"));
        QCOMPARE(Language::nativeName(QStringLiteral("ru")), QStringLiteral("Русский"));
        QVERIFY(!Language::instance().setLanguage(QStringLiteral("xx")));
    }

    void layoutIsRememberedAndCanBeReset()
    {
        QSettings().remove(QStringLiteral("window"));
        {
            MainWindow w;
            w.show();
            w.findChild<QDockWidget *>(QStringLiteral("inspectorDock"))->hide();
            w.close(); // saves layout
        }
        {
            MainWindow w;
            w.show();
            QCoreApplication::processEvents();
            auto *dock = w.findChild<QDockWidget *>(QStringLiteral("inspectorDock"));
            QVERIFY(dock->isHidden());
            w.resetLayout();
            QVERIFY(!dock->isHidden());
            w.close();
        }
    }

    void statusDialogListsAllStates()
    {
        StatusDialog dlg;
        auto *tree = dlg.findChild<QTreeWidget *>(QStringLiteral("statusTree"));
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), int(FeatureStatus::entries().size()));
        bool ready = false, partial = false, planned = false;
        for (const auto &e : FeatureStatus::entries()) {
            ready |= e.state == FeatureEntry::State::Ready;
            partial |= e.state == FeatureEntry::State::Partial;
            planned |= e.state == FeatureEntry::State::Planned;
        }
        QVERIFY(ready && partial && planned);
    }

    void licensesMentionQt()
    {
        QVERIFY(AboutDialog::licensesHtml().contains(QStringLiteral("LGPL")));
    }
};

QTEST_MAIN(TestPhase0)
#include "tst_phase0.moc"
