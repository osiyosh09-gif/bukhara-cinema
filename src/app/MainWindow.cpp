#include "app/MainWindow.h"

#include "app/AboutDialog.h"
#include "app/AppInfo.h"
#include "app/FeatureStatus.h"
#include "app/Logging.h"
#include "app/MonitorView.h"
#include "app/PlaceholderPanel.h"
#include "app/SettingsDialog.h"
#include "app/StatusDialog.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>

namespace bc {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("mainWindow"));
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    buildCentral();
    buildDocks();
    buildMenus();
    buildToolbar();

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel, 1);

    retranslate();

    resize(1366, 768);
    m_defaultState = saveState();
    m_defaultSplit = m_monitorSplit->saveState();
    restoreLayout();
}

// ---------------------------------------------------------------------------
// Layout

void MainWindow::buildCentral()
{
    m_monitorSplit = new QSplitter(Qt::Horizontal, this);
    m_monitorSplit->setObjectName(QStringLiteral("monitorSplit"));
    auto *source = new MonitorView([] { return MainWindow::tr("Source"); }, m_monitorSplit);
    source->setObjectName(QStringLiteral("sourceMonitor"));
    auto *program = new MonitorView([] { return MainWindow::tr("Program"); }, m_monitorSplit);
    program->setObjectName(QStringLiteral("programMonitor"));
    m_monitorSplit->addWidget(source);
    m_monitorSplit->addWidget(program);
    m_monitorSplit->setChildrenCollapsible(false);
    setCentralWidget(m_monitorSplit);
}

void MainWindow::buildDocks()
{
    // Left: media bin + project files
    m_leftTabs = new QTabWidget;
    m_leftTabs->setObjectName(QStringLiteral("leftTabs"));
    m_leftTabs->addTab(new PlaceholderPanel([] { return MainWindow::tr("Media bin"); }, 1), QString());
    m_leftTabs->addTab(new PlaceholderPanel([] { return MainWindow::tr("Project files"); }, 1), QString());
    m_mediaDock = new QDockWidget(this);
    m_mediaDock->setObjectName(QStringLiteral("mediaDock"));
    m_mediaDock->setWidget(m_leftTabs);
    addDockWidget(Qt::LeftDockWidgetArea, m_mediaDock);

    // Right: clip parameters, effects, color, audio
    m_rightTabs = new QTabWidget;
    m_rightTabs->setObjectName(QStringLiteral("rightTabs"));
    m_rightTabs->addTab(new PlaceholderPanel([] { return MainWindow::tr("Clip parameters"); }, 3), QString());
    m_rightTabs->addTab(new PlaceholderPanel([] { return MainWindow::tr("Effects"); }, 8), QString());
    m_rightTabs->addTab(new PlaceholderPanel([] { return MainWindow::tr("Color"); }, 9), QString());
    m_rightTabs->addTab(new PlaceholderPanel([] { return MainWindow::tr("Audio"); }, 3), QString());
    m_inspectorDock = new QDockWidget(this);
    m_inspectorDock->setObjectName(QStringLiteral("inspectorDock"));
    m_inspectorDock->setWidget(m_rightTabs);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

    // Bottom: timeline
    auto *timeline = new TimelinePlaceholder;
    timeline->setObjectName(QStringLiteral("timelinePlaceholder"));
    m_timelineDock = new QDockWidget(this);
    m_timelineDock->setObjectName(QStringLiteral("timelineDock"));
    m_timelineDock->setWidget(timeline);
    addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);

    resizeDocks({m_mediaDock, m_inspectorDock}, {300, 320}, Qt::Horizontal);
    resizeDocks({m_timelineDock}, {260}, Qt::Vertical);
}

QAction *MainWindow::addPlanned(QMenu *menu, const char *sourceText, int phase, const QString &objectName)
{
    auto *a = menu->addAction(QString());
    a->setObjectName(objectName);
    a->setEnabled(false);
    a->setProperty("bc_planned", true);
    a->setProperty("bc_phase", phase);
    m_planned.append({a, sourceText, phase});
    return a;
}

void MainWindow::buildMenus()
{
    auto *mb = menuBar();

    // NOTE: texts are set in retranslate(); QT_TR_NOOP marks them for lupdate.
    m_fileMenu = mb->addMenu(QString());
    addPlanned(m_fileMenu, QT_TR_NOOP("New project"), 1, QStringLiteral("actNewProject"));
    addPlanned(m_fileMenu, QT_TR_NOOP("Open project..."), 1, QStringLiteral("actOpenProject"));
    addPlanned(m_fileMenu, QT_TR_NOOP("Save"), 1, QStringLiteral("actSave"));
    addPlanned(m_fileMenu, QT_TR_NOOP("Save as..."), 1, QStringLiteral("actSaveAs"));
    m_fileMenu->addSeparator();
    addPlanned(m_fileMenu, QT_TR_NOOP("Import media..."), 1, QStringLiteral("actImport"));
    addPlanned(m_fileMenu, QT_TR_NOOP("Export..."), 4, QStringLiteral("actExport"));
    m_fileMenu->addSeparator();
    m_settingsAct = m_fileMenu->addAction(QString(), this, &MainWindow::openSettings);
    m_settingsAct->setObjectName(QStringLiteral("actSettings"));
    m_settingsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    m_fileMenu->addSeparator();
    m_exitAct = m_fileMenu->addAction(QString(), this, &QWidget::close);
    m_exitAct->setObjectName(QStringLiteral("actExit"));
    m_exitAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));

    m_editMenu = mb->addMenu(QString());
    addPlanned(m_editMenu, QT_TR_NOOP("Undo"), 2, QStringLiteral("actUndo"));
    addPlanned(m_editMenu, QT_TR_NOOP("Redo"), 2, QStringLiteral("actRedo"));
    m_editMenu->addSeparator();
    addPlanned(m_editMenu, QT_TR_NOOP("Cut"), 2, QStringLiteral("actCut"));
    addPlanned(m_editMenu, QT_TR_NOOP("Copy"), 2, QStringLiteral("actCopy"));
    addPlanned(m_editMenu, QT_TR_NOOP("Paste"), 2, QStringLiteral("actPaste"));
    addPlanned(m_editMenu, QT_TR_NOOP("Delete"), 2, QStringLiteral("actDelete"));
    addPlanned(m_editMenu, QT_TR_NOOP("Ripple delete"), 2, QStringLiteral("actRippleDelete"));
    m_editMenu->addSeparator();
    addPlanned(m_editMenu, QT_TR_NOOP("Keyboard shortcuts..."), 6, QStringLiteral("actShortcuts"));

    m_clipMenu = mb->addMenu(QString());
    addPlanned(m_clipMenu, QT_TR_NOOP("Split at playhead"), 2, QStringLiteral("actSplit"));
    addPlanned(m_clipMenu, QT_TR_NOOP("Audio gain..."), 3, QStringLiteral("actAudioGain"));
    addPlanned(m_clipMenu, QT_TR_NOOP("Speed / duration..."), 7, QStringLiteral("actSpeed"));
    addPlanned(m_clipMenu, QT_TR_NOOP("Link / unlink audio and video"), 6, QStringLiteral("actLink"));
    addPlanned(m_clipMenu, QT_TR_NOOP("Nest..."), 6, QStringLiteral("actNest"));

    m_sequenceMenu = mb->addMenu(QString());
    addPlanned(m_sequenceMenu, QT_TR_NOOP("Sequence settings..."), 1, QStringLiteral("actSequenceSettings"));
    addPlanned(m_sequenceMenu, QT_TR_NOOP("Add track"), 2, QStringLiteral("actAddTrack"));
    addPlanned(m_sequenceMenu, QT_TR_NOOP("Snapping"), 2, QStringLiteral("actSnapping"));
    addPlanned(m_sequenceMenu, QT_TR_NOOP("Render preview"), 13, QStringLiteral("actRenderPreview"));

    m_markersMenu = mb->addMenu(QString());
    addPlanned(m_markersMenu, QT_TR_NOOP("Add marker"), 6, QStringLiteral("actAddMarker"));
    addPlanned(m_markersMenu, QT_TR_NOOP("Go to next marker"), 6, QStringLiteral("actNextMarker"));

    m_graphicsMenu = mb->addMenu(QString());
    addPlanned(m_graphicsMenu, QT_TR_NOOP("New text"), 3, QStringLiteral("actNewText"));
    addPlanned(m_graphicsMenu, QT_TR_NOOP("Import subtitles (SRT)..."), 11, QStringLiteral("actImportSrt"));

    m_viewMenu = mb->addMenu(QString());
    m_fullScreenAct = m_viewMenu->addAction(QString());
    m_fullScreenAct->setObjectName(QStringLiteral("actFullScreen"));
    m_fullScreenAct->setCheckable(true);
    m_fullScreenAct->setShortcut(QKeySequence(QStringLiteral("F11")));
    connect(m_fullScreenAct, &QAction::toggled, this, [this](bool on) {
        on ? showFullScreen() : showMaximized();
    });

    m_windowMenu = mb->addMenu(QString());
    m_windowMenu->addAction(m_mediaDock->toggleViewAction());
    m_windowMenu->addAction(m_inspectorDock->toggleViewAction());
    m_windowMenu->addAction(m_timelineDock->toggleViewAction());
    m_windowMenu->addSeparator();
    m_resetLayoutAct = m_windowMenu->addAction(QString(), this, &MainWindow::resetLayout);
    m_resetLayoutAct->setObjectName(QStringLiteral("actResetLayout"));

    m_helpMenu = mb->addMenu(QString());
    m_statusAct = m_helpMenu->addAction(QString(), this, &MainWindow::openStatus);
    m_statusAct->setObjectName(QStringLiteral("actStatus"));
    m_licensesAct = m_helpMenu->addAction(QString(), this, &MainWindow::openLicenses);
    m_licensesAct->setObjectName(QStringLiteral("actLicenses"));
    m_logFolderAct = m_helpMenu->addAction(QString(), this, &MainWindow::openLogFolder);
    m_logFolderAct->setObjectName(QStringLiteral("actLogFolder"));
    m_helpMenu->addSeparator();
    m_aboutAct = m_helpMenu->addAction(QString(), this, &MainWindow::openAbout);
    m_aboutAct->setObjectName(QStringLiteral("actAbout"));
}

void MainWindow::buildToolbar()
{
    m_toolbar = addToolBar(QString());
    m_toolbar->setObjectName(QStringLiteral("mainToolbar"));
    m_toolbar->setMovable(false);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_tbSave = m_toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), QString());
    m_tbSave->setObjectName(QStringLiteral("tbSave"));
    m_tbSave->setEnabled(false);
    m_tbSave->setProperty("bc_planned", true);

    m_tbExport = m_toolbar->addAction(style()->standardIcon(QStyle::SP_ArrowUp), QString());
    m_tbExport->setObjectName(QStringLiteral("tbExport"));
    m_tbExport->setEnabled(false);
    m_tbExport->setProperty("bc_planned", true);

    auto *spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
    m_toolbar->addAction(m_settingsAct);
}

void MainWindow::retranslate()
{
    setWindowTitle(QStringLiteral("%1 %2").arg(appName(), appVersion()));

    m_fileMenu->setTitle(tr("&File"));
    m_editMenu->setTitle(tr("&Edit"));
    m_clipMenu->setTitle(tr("&Clip"));
    m_sequenceMenu->setTitle(tr("&Sequence"));
    m_markersMenu->setTitle(tr("&Markers"));
    m_graphicsMenu->setTitle(tr("&Graphics"));
    m_viewMenu->setTitle(tr("&View"));
    m_windowMenu->setTitle(tr("&Window"));
    m_helpMenu->setTitle(tr("&Help"));

    // Planned items: the phase tag sits in the shortcut column.
    for (const PlannedAction &p : m_planned) {
        p.action->setText(tr(p.sourceText) + QLatin1Char('\t') + FeatureStatus::phaseTag(p.phase));
        p.action->setToolTip(tr("Planned for phase %1").arg(p.phase));
    }

    m_settingsAct->setText(tr("Settings..."));
    m_exitAct->setText(tr("Exit"));
    m_fullScreenAct->setText(tr("Full screen"));
    m_resetLayoutAct->setText(tr("Reset layout"));
    m_statusAct->setText(tr("Feature status..."));
    m_licensesAct->setText(tr("Licenses..."));
    m_logFolderAct->setText(tr("Open log folder"));
    m_aboutAct->setText(tr("About BUKHARA CINEMA..."));

    m_toolbar->setWindowTitle(tr("Main toolbar"));
    m_tbSave->setText(tr("Save"));
    m_tbSave->setToolTip(tr("Planned for phase %1").arg(1));
    m_tbExport->setText(tr("Export"));
    m_tbExport->setToolTip(tr("Planned for phase %1").arg(4));

    m_mediaDock->setWindowTitle(tr("Project"));
    m_inspectorDock->setWindowTitle(tr("Properties"));
    m_timelineDock->setWindowTitle(tr("Timeline"));

    m_leftTabs->setTabText(0, tr("Media bin"));
    m_leftTabs->setTabText(1, tr("Project files"));
    m_rightTabs->setTabText(0, tr("Clip"));
    m_rightTabs->setTabText(1, tr("Effects"));
    m_rightTabs->setTabText(2, tr("Color"));
    m_rightTabs->setTabText(3, tr("Audio"));

    m_statusLabel->setText(tr("Phase 0: interface only. Editing features are not working yet."));
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QMainWindow::changeEvent(event);
}

// ---------------------------------------------------------------------------
// Layout persistence

void MainWindow::saveLayout()
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
    s.setValue(QStringLiteral("window/monitorSplit"), m_monitorSplit->saveState());
}

void MainWindow::restoreLayout()
{
    QSettings s;
    const QByteArray geom = s.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geom.isEmpty())
        restoreGeometry(geom);
    const QByteArray state = s.value(QStringLiteral("window/state")).toByteArray();
    if (!state.isEmpty())
        restoreState(state);
    const QByteArray split = s.value(QStringLiteral("window/monitorSplit")).toByteArray();
    if (!split.isEmpty())
        m_monitorSplit->restoreState(split);
}

void MainWindow::resetLayout()
{
    restoreState(m_defaultState);
    m_monitorSplit->restoreState(m_defaultSplit);
    m_mediaDock->show();
    m_inspectorDock->show();
    m_timelineDock->show();
    resizeDocks({m_mediaDock, m_inspectorDock}, {300, 320}, Qt::Horizontal);
    resizeDocks({m_timelineDock}, {260}, Qt::Vertical);
    const int half = m_monitorSplit->width() / 2;
    m_monitorSplit->setSizes({half, half});
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveLayout();
    QMainWindow::closeEvent(event);
}

// ---------------------------------------------------------------------------
// Dialogs

void MainWindow::openSettings()
{
    SettingsDialog dlg(this);
    dlg.exec();
}

void MainWindow::openAbout()
{
    AboutDialog dlg(AboutDialog::Page::About, this);
    dlg.exec();
}

void MainWindow::openLicenses()
{
    AboutDialog dlg(AboutDialog::Page::Licenses, this);
    dlg.exec();
}

void MainWindow::openStatus()
{
    StatusDialog dlg(this);
    dlg.exec();
}

void MainWindow::openLogFolder()
{
    QDir().mkpath(Logging::logDirectory());
    QDesktopServices::openUrl(QUrl::fromLocalFile(Logging::logDirectory()));
}

} // namespace bc
