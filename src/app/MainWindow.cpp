#include "app/MainWindow.h"

#include "app/AboutDialog.h"
#include "app/AppInfo.h"
#include "app/Autosave.h"
#include "app/RecoveryDialog.h"
#include "app/FeatureStatus.h"
#include "app/Logging.h"
#include "app/MediaBinPanel.h"
#include "app/MonitorView.h"
#include "app/ProjectController.h"
#include "app/ProjectInfoPanel.h"
#include "app/SequenceSettingsDialog.h"
#include "app/CacheDialog.h"
#include "app/ClipInspector.h"
#include "app/AudioMixer.h"
#include "app/ColorPanel.h"
#include "app/ScopesPanel.h"
#include "app/EffectsPanel.h"
#include "app/CollectDialog.h"
#include "app/ExportDialog.h"
#include "app/PlaceholderPanel.h"
#include "app/SettingsDialog.h"
#include "app/Shortcuts.h"
#include "app/StatusDialog.h"
#include "app/Formatting.h"
#include "app/TimelinePanel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QUrl>

namespace bc {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("mainWindow"));
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    m_ctl = new ProjectController(this);
    m_autosave = new Autosave(m_ctl, this);
    connect(m_autosave, &Autosave::saved, this, [this](const QDateTime &t) {
        statusBar()->showMessage(tr("Autosaved at %1").arg(QLocale().toString(t.time(), QStringLiteral("HH:mm"))), 4000);
    });
    connect(m_autosave, &Autosave::failed, this, [this](const QString &e) {
        statusBar()->showMessage(tr("Autosave failed: %1").arg(e), 8000);
    });

    buildCentral();
    buildDocks();
    buildMenus();
    buildToolbar();

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel, 1);
    // Proxy work in the background: "Making proxy: 37% (2 more)" + stop.
    m_proxyLabel = new QLabel(this);
    m_proxyLabel->setObjectName(QStringLiteral("proxyStatus"));
    m_proxyCancel = new QToolButton(this);
    m_proxyCancel->setObjectName(QStringLiteral("proxyCancel"));
    m_proxyCancel->setAutoRaise(true);
    m_proxyCancel->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
    connect(m_proxyCancel, &QToolButton::clicked, m_ctl, &ProjectController::cancelProxies);
    statusBar()->addPermanentWidget(m_proxyLabel);
    statusBar()->addPermanentWidget(m_proxyCancel);
    m_proxyLabel->hide();
    m_proxyCancel->hide();
    connect(m_ctl, &ProjectController::proxyProgress, this, [this](const QUuid &, double f, int left) {
        m_proxyLabel->setText(left > 1 ? tr("Making proxy: %1% (%2 more)").arg(int(f * 100)).arg(left - 1)
                                       : tr("Making proxy: %1%").arg(int(f * 100)));
        m_proxyLabel->show();
        m_proxyCancel->show();
    });
    connect(m_ctl, &ProjectController::proxyFinished, this, [this](const QUuid &, bool ok, const QString &err) {
        if (!ok && err != QLatin1String("cancelled"))
            statusBar()->showMessage(tr("Could not make a proxy: %1").arg(err), 8000);
        updateProxyStatus();
    });
    connect(m_ctl, &ProjectController::itemUpdated, this, &MainWindow::updateProxyStatus);

    connect(m_ctl, &ProjectController::modifiedChanged, this, &MainWindow::updateTitle);
    connect(m_ctl, &ProjectController::fileNameChanged, this, &MainWindow::updateTitle);
    connect(m_info, &ProjectInfoPanel::sequenceSettingsRequested, this, &MainWindow::editSequence);

    // Program monitor: frame at the playhead.
    connect(m_ctl, &ProjectController::programFrameReady, m_programMonitor, &MonitorView::setImage);
    auto updateProgramTimecode = [this] {
        m_programMonitor->setTimecode(Fmt::frameTimecode(m_ctl->playhead(), m_ctl->project().sequence.fps()));
    };
    connect(m_ctl, &ProjectController::playheadChanged, this, updateProgramTimecode);
    connect(m_ctl, &ProjectController::sequenceChanged, this, updateProgramTimecode);
    connect(m_ctl, &ProjectController::projectReset, this, updateProgramTimecode);

    // Program monitor transport (real-time playback through MLT).
    m_programMonitor->setDisabledReason(
        [] { return tr("The video engine (MLT) could not be loaded: playback is off. See the log file."); });
    m_programMonitor->setTransportEnabled(m_ctl->canPlay());
    connect(m_programMonitor, &MonitorView::playPause, m_ctl, &ProjectController::togglePlay);
    connect(m_programMonitor, &MonitorView::goToStart, this, [this] { m_ctl->setPlayhead(0); });
    connect(m_programMonitor, &MonitorView::goToEnd, this,
            [this] { m_ctl->setPlayhead(std::max<qint64>(0, m_ctl->timeline().duration() - 1)); });
    connect(m_programMonitor, &MonitorView::stepBack, this, [this] {
        m_ctl->pause();
        m_ctl->setPlayhead(m_ctl->playhead() - 1);
    });
    connect(m_programMonitor, &MonitorView::stepForward, this, [this] {
        m_ctl->pause();
        m_ctl->setPlayhead(m_ctl->playhead() + 1);
    });
    connect(m_ctl, &ProjectController::playingChanged, this, [this](bool on) {
        m_programMonitor->setPlaying(on);
        m_playAct->setText(on ? tr("Pause") : tr("Play"));
    });

    wireMonitors();
    retranslate();
    Shortcuts::init(this); // the user's own keys (Edit > Keyboard shortcuts)

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
    program->setEmptyText([this] {
        return m_ctl->timeline().isEmpty() ? MainWindow::tr("Drag media from the bin to the timeline.")
                                           : MainWindow::tr("No video at the playhead.");
    });
    m_programMonitor = program;
    source->setEmptyText([this] {
        return m_ctl->sourceMedia().isNull()
                   ? MainWindow::tr("Double-click a clip in the media bin to watch it here.")
                   : MainWindow::tr("No picture (sound only).");
    });
    m_sourceMonitor = source;
    m_monitorSplit->addWidget(source);
    m_monitorSplit->addWidget(program);
    m_monitorSplit->setChildrenCollapsible(false);
    m_monitorSplit->setSizes({600, 600}); // both monitors equally wide by default
    setCentralWidget(m_monitorSplit);
}

void MainWindow::buildDocks()
{
    // Left: media bin + project files
    m_leftTabs = new QTabWidget;
    m_leftTabs->setObjectName(QStringLiteral("leftTabs"));
    m_bin = new MediaBinPanel(m_ctl);
    m_info = new ProjectInfoPanel(m_ctl);
    m_leftTabs->addTab(m_bin, QString());
    m_leftTabs->addTab(m_info, QString());
    m_mediaDock = new QDockWidget(this);
    m_mediaDock->setObjectName(QStringLiteral("mediaDock"));
    m_mediaDock->setWidget(m_leftTabs);
    addDockWidget(Qt::LeftDockWidgetArea, m_mediaDock);

    // The timeline panel exists first: the clip inspector follows its selection.
    m_timeline = new TimelinePanel(m_ctl);

    // Right: clip parameters, effects, color, audio
    m_rightTabs = new QTabWidget;
    m_rightTabs->setObjectName(QStringLiteral("rightTabs"));
    m_clipInspector = new ClipInspector(m_ctl, m_timeline->view());
    m_rightTabs->addTab(m_clipInspector, QString());
    m_effectsPanel = new EffectsPanel(m_ctl, m_timeline->view());
    m_rightTabs->addTab(m_effectsPanel, QString());
    m_rightTabs->addTab(new ColorPanel(m_ctl, m_timeline->view()), QString());
    m_rightTabs->addTab(new AudioMixer(m_ctl), QString());
    m_inspectorDock = new QDockWidget(this);
    m_inspectorDock->setObjectName(QStringLiteral("inspectorDock"));
    m_inspectorDock->setWidget(m_rightTabs);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

    // Scopes: off until opened from the Window menu (they cost time on a slow computer).
    m_scopes = new ScopesPanel;
    m_scopesDock = new QDockWidget(this);
    m_scopesDock->setObjectName(QStringLiteral("scopesDock"));
    m_scopesDock->setWidget(m_scopes);
    addDockWidget(Qt::RightDockWidgetArea, m_scopesDock);
    m_scopesDock->hide();
    connect(m_ctl, &ProjectController::programFrameReady, m_scopes, &ScopesPanel::setFrame);

    // Bottom: timeline
    m_timelineDock = new QDockWidget(this);
    m_timelineDock->setObjectName(QStringLiteral("timelineDock"));
    m_timelineDock->setWidget(m_timeline);
    addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);

    resizeDocks({m_mediaDock, m_inspectorDock}, {380, 300}, Qt::Horizontal);
    resizeDocks({m_timelineDock}, {300}, Qt::Vertical);
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
    m_newAct = m_fileMenu->addAction(QString(), this, &MainWindow::newProject);
    m_newAct->setObjectName(QStringLiteral("actNewProject"));
    m_newAct->setShortcut(QKeySequence::New);
    m_openAct = m_fileMenu->addAction(QString(), this, &MainWindow::openProject);
    m_openAct->setObjectName(QStringLiteral("actOpenProject"));
    m_openAct->setShortcut(QKeySequence::Open);
    m_recentMenu = m_fileMenu->addMenu(QString());
    m_recentMenu->setObjectName(QStringLiteral("menuRecent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    m_saveAct = m_fileMenu->addAction(QString(), this, &MainWindow::saveProject);
    m_saveAct->setObjectName(QStringLiteral("actSave"));
    m_saveAct->setShortcut(QKeySequence::Save);
    m_saveAsAct = m_fileMenu->addAction(QString(), this, &MainWindow::saveProjectAs);
    m_saveAsAct->setObjectName(QStringLiteral("actSaveAs"));
    m_saveAsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    m_versionsAct = m_fileMenu->addAction(QString(), this, &MainWindow::openVersions);
    m_versionsAct->setObjectName(QStringLiteral("actVersions"));
    m_versionsAct->setEnabled(false);
    connect(m_ctl, &ProjectController::fileNameChanged, this,
            [this](const QString &path) { m_versionsAct->setEnabled(!path.isEmpty()); });
    m_collectAct = m_fileMenu->addAction(QString(), this, &MainWindow::openCollect);
    m_collectAct->setObjectName(QStringLiteral("actCollect"));
    m_fileMenu->addSeparator();
    m_importAct = m_fileMenu->addAction(QString(), m_bin, &MediaBinPanel::importDialog);
    m_importAct->setObjectName(QStringLiteral("actImport"));
    m_importAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    m_exportAct = m_fileMenu->addAction(QString(), this, &MainWindow::openExport);
    m_exportAct->setObjectName(QStringLiteral("actExport"));
    m_exportAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    auto updateExport = [this] {
        const bool on = m_ctl->canPlay() && !m_ctl->timeline().isEmpty();
        m_exportAct->setEnabled(on);
        if (m_tbExport)
            m_tbExport->setEnabled(on);
    };
    connect(m_ctl, &ProjectController::timelineChanged, this, updateExport);
    connect(m_ctl, &ProjectController::projectReset, this, updateExport);
    m_exportAct->setEnabled(false);
    m_fileMenu->addSeparator();
    m_cacheAct = m_fileMenu->addAction(QString(), this, &MainWindow::openCache);
    m_cacheAct->setObjectName(QStringLiteral("actMediaCache"));
    m_settingsAct = m_fileMenu->addAction(QString(), this, &MainWindow::openSettings);
    m_settingsAct->setObjectName(QStringLiteral("actSettings"));
    m_settingsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    m_fileMenu->addSeparator();
    m_exitAct = m_fileMenu->addAction(QString(), this, &QWidget::close);
    m_exitAct->setObjectName(QStringLiteral("actExit"));
    m_exitAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));

    m_editMenu = mb->addMenu(QString());
    m_undoAct = m_editMenu->addAction(QString(), m_ctl->undoStack(), &QUndoStack::undo);
    m_undoAct->setObjectName(QStringLiteral("actUndo"));
    m_undoAct->setShortcut(QKeySequence::Undo);
    m_redoAct = m_editMenu->addAction(QString(), m_ctl->undoStack(), &QUndoStack::redo);
    m_redoAct->setObjectName(QStringLiteral("actRedo"));
    m_redoAct->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Shift+Z")), QKeySequence(QStringLiteral("Ctrl+Y"))});
    connect(m_ctl->undoStack(), &QUndoStack::indexChanged, this, &MainWindow::updateUndoActions);
    connect(m_ctl->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateUndoActions);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_timeline->cutAction());
    m_editMenu->addAction(m_timeline->copyAction());
    m_editMenu->addAction(m_timeline->pasteAction());
    m_editMenu->addAction(m_timeline->pasteInsertAction());
    m_editMenu->addAction(m_timeline->deleteAction());
    m_editMenu->addAction(m_timeline->rippleDeleteAction());
    m_editMenu->addAction(m_timeline->selectAllAction());
    m_editMenu->addSeparator();
    m_shortcutsAct = m_editMenu->addAction(QString(), this, [this] {
        ShortcutsDialog dlg(this, this);
        dlg.exec();
    });
    m_shortcutsAct->setObjectName(QStringLiteral("actShortcuts"));

    m_clipMenu = mb->addMenu(QString());
    m_clipMenu->addAction(m_timeline->splitAction());
    m_audioGainAct = m_clipMenu->addAction(QString(), this, [this] {
        m_inspectorDock->show();
        m_rightTabs->setCurrentWidget(m_clipInspector);
        m_clipInspector->focusGain();
    });
    m_audioGainAct->setObjectName(QStringLiteral("actAudioGain"));
    m_audioGainAct->setShortcut(QKeySequence(QStringLiteral("G")));
    m_audioGainAct->setEnabled(false);
    connect(m_timeline->view(), &TimelineView::selectionChanged, this,
            [this] { m_audioGainAct->setEnabled(!m_clipInspector->audioClip().isNull()); });
    connect(m_ctl, &ProjectController::timelineChanged, this,
            [this] { m_audioGainAct->setEnabled(!m_clipInspector->audioClip().isNull()); });
    m_clipMenu->addAction(m_timeline->speedAction());
    m_clipMenu->addAction(m_timeline->loudnessAction());
    m_clipMenu->addAction(m_timeline->transitionAction());
    m_clipMenu->addAction(m_timeline->linkAction());
    m_toolsMenu = m_clipMenu->addMenu(QString());
    m_toolsMenu->setObjectName(QStringLiteral("menuTools"));
    m_toolsMenu->addActions(m_timeline->toolActions());
    m_clipMenu->addAction(m_timeline->nestAction());

    m_sequenceMenu = mb->addMenu(QString());
    m_sequenceAct = m_sequenceMenu->addAction(QString(), this, &MainWindow::editSequence);
    m_sequenceAct->setObjectName(QStringLiteral("actSequenceSettings"));
    m_sequenceMenu->addAction(m_timeline->addVideoTrackAction());
    m_sequenceMenu->addAction(m_timeline->addAudioTrackAction());
    m_sequenceMenu->addAction(m_timeline->snappingAction());
    m_sequenceMenu->addSeparator();
    m_playAct = m_sequenceMenu->addAction(QString(), this, [this] {
        m_sourceActive ? m_ctl->toggleSourcePlay() : m_ctl->togglePlay();
    });
    m_playAct->setObjectName(QStringLiteral("actPlayPause"));
    m_playAct->setShortcut(QKeySequence(Qt::Key_Space));
    m_playAct->setEnabled(m_ctl->canPlay());
    m_useProxiesAct = m_sequenceMenu->addAction(QString());
    m_useProxiesAct->setObjectName(QStringLiteral("actUseProxies"));
    m_useProxiesAct->setCheckable(true);
    m_useProxiesAct->setChecked(m_ctl->useProxies());
    connect(m_useProxiesAct, &QAction::toggled, m_ctl, &ProjectController::setUseProxies);
    addPlanned(m_sequenceMenu, QT_TR_NOOP("Render preview"), 13, QStringLiteral("actRenderPreview"));

    // J / K / L act on the active monitor (the one clicked last).
    m_sequenceMenu->addSeparator();
    auto shuttleAct = [this](const char *name, const char *key, int dir) {
        QAction *a = m_sequenceMenu->addAction(QString(), this, [this, dir] {
            m_ctl->shuttle(m_sourceActive ? ProjectController::Monitor::Source : ProjectController::Monitor::Program,
                           dir);
        });
        a->setObjectName(QString::fromLatin1(name));
        a->setShortcut(QKeySequence(QString::fromLatin1(key)));
        a->setEnabled(m_ctl->canPlay());
        return a;
    };
    m_shuttleBackAct = shuttleAct("actShuttleBack", "J", -1);
    m_shuttleStopAct = shuttleAct("actShuttleStop", "K", 0);
    m_shuttleFwdAct = shuttleAct("actShuttleForward", "L", 1);

    // Insert / Overwrite: the source range goes to the timeline at the playhead.
    m_clipMenu->addSeparator();
    m_insertAct = m_clipMenu->addAction(QString(), this, [this] {
        if (!m_ctl->insertEdit())
            statusBar()->showMessage(tr("Nothing to insert: open a clip in the Source monitor, and keep one video and one audio track unlocked."), 6000);
    });
    m_insertAct->setObjectName(QStringLiteral("actInsert"));
    m_insertAct->setShortcut(QKeySequence(Qt::Key_Comma));
    m_overwriteAct = m_clipMenu->addAction(QString(), this, [this] {
        if (!m_ctl->overwriteEdit())
            statusBar()->showMessage(tr("Nothing to insert: open a clip in the Source monitor, and keep one video and one audio track unlocked."), 6000);
    });
    m_overwriteAct->setObjectName(QStringLiteral("actOverwrite"));
    m_overwriteAct->setShortcut(QKeySequence(Qt::Key_Period));

    m_markersMenu = mb->addMenu(QString());
    m_markInAct = m_markersMenu->addAction(QString(), this, [this] { m_ctl->setSourceIn(m_ctl->sourcePosition()); });
    m_markInAct->setObjectName(QStringLiteral("actMarkIn"));
    m_markInAct->setShortcut(QKeySequence(QStringLiteral("I")));
    m_markOutAct = m_markersMenu->addAction(QString(), this, [this] { m_ctl->setSourceOut(m_ctl->sourcePosition()); });
    m_markOutAct->setObjectName(QStringLiteral("actMarkOut"));
    m_markOutAct->setShortcut(QKeySequence(QStringLiteral("O")));
    m_clearInOutAct = m_markersMenu->addAction(QString(), m_ctl, &ProjectController::clearSourceInOut);
    m_clearInOutAct->setObjectName(QStringLiteral("actClearInOut"));
    m_clearInOutAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+X")));
    m_markersMenu->addSeparator();
    m_addMarkerAct = m_markersMenu->addAction(QString(), this, [this] { m_ctl->addMarker(m_ctl->playhead()); });
    m_addMarkerAct->setObjectName(QStringLiteral("actAddMarker"));
    m_addMarkerAct->setShortcut(QKeySequence(QStringLiteral("M")));
    m_nextMarkerAct = m_markersMenu->addAction(QString(), m_ctl, &ProjectController::goToNextMarker);
    m_nextMarkerAct->setObjectName(QStringLiteral("actNextMarker"));
    m_nextMarkerAct->setShortcut(QKeySequence(QStringLiteral("Shift+M")));
    m_prevMarkerAct = m_markersMenu->addAction(QString(), m_ctl, &ProjectController::goToPreviousMarker);
    m_prevMarkerAct->setObjectName(QStringLiteral("actPrevMarker"));
    m_prevMarkerAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+M")));
    m_renameMarkerAct = m_markersMenu->addAction(QString(), this, &MainWindow::renameMarker);
    m_renameMarkerAct->setObjectName(QStringLiteral("actRenameMarker"));
    m_deleteMarkerAct = m_markersMenu->addAction(QString(), this, [this] {
        if (const TimelineMarker *m = m_ctl->timeline().markerAt(m_ctl->playhead()))
            m_ctl->removeMarker(m->id);
    });
    m_deleteMarkerAct->setObjectName(QStringLiteral("actDeleteMarker"));
    connect(m_ctl, &ProjectController::timelineChanged, this, &MainWindow::updateEditActions);
    connect(m_ctl, &ProjectController::playheadChanged, this, &MainWindow::updateEditActions);
    connect(m_ctl, &ProjectController::sourceChanged, this, &MainWindow::updateEditActions);

    m_graphicsMenu = mb->addMenu(QString());
    m_newTextAct = m_graphicsMenu->addAction(QString(), this, &MainWindow::addTitle);
    m_newTextAct->setObjectName(QStringLiteral("actNewText"));
    m_newTextAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
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
    m_scopesAct = m_scopesDock->toggleViewAction();
    m_scopesAct->setObjectName(QStringLiteral("actScopes"));
    m_windowMenu->addAction(m_scopesAct);
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

    m_tbSave = m_toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), QString(), this,
                                    &MainWindow::saveProject);
    m_tbSave->setObjectName(QStringLiteral("tbSave"));

    m_tbExport = m_toolbar->addAction(style()->standardIcon(QStyle::SP_ArrowUp), QString(), this,
                                      &MainWindow::openExport);
    m_tbExport->setObjectName(QStringLiteral("tbExport"));
    m_tbExport->setEnabled(m_exportAct->isEnabled());

    auto *spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
    m_toolbar->addAction(m_settingsAct);
}

void MainWindow::retranslate()
{
    updateTitle();

    m_fileMenu->setTitle(tr("&File"));
    m_editMenu->setTitle(tr("&Edit"));
    m_clipMenu->setTitle(tr("&Clip"));
    m_sequenceMenu->setTitle(tr("&Sequence"));
    m_markersMenu->setTitle(tr("&Markers"));
    m_toolsMenu->setTitle(tr("Timeline tools"));
    m_graphicsMenu->setTitle(tr("&Graphics"));
    m_viewMenu->setTitle(tr("&View"));
    m_windowMenu->setTitle(tr("&Window"));
    m_helpMenu->setTitle(tr("&Help"));

    // Planned items: the phase tag sits in the shortcut column.
    for (const PlannedAction &p : m_planned) {
        p.action->setText(tr(p.sourceText) + QLatin1Char('\t') + FeatureStatus::phaseTag(p.phase));
        p.action->setToolTip(tr("Planned for phase %1").arg(p.phase));
    }

    updateUndoActions();
    m_playAct->setText(m_ctl->isPlaying() ? tr("Pause") : tr("Play"));
    m_newTextAct->setText(tr("New text"));
    m_audioGainAct->setText(tr("Audio gain..."));
    m_newAct->setText(tr("New project"));
    m_openAct->setText(tr("Open project..."));
    m_recentMenu->setTitle(tr("Recent projects"));
    m_saveAct->setText(tr("Save"));
    m_saveAsAct->setText(tr("Save as..."));
    m_versionsAct->setText(tr("Previous versions..."));
    m_collectAct->setText(tr("Collect project..."));
    m_collectAct->setToolTip(tr("Copy the project and its media into one folder"));
    m_cacheAct->setText(tr("Media cache..."));
    m_useProxiesAct->setText(tr("Use proxies for playback"));
    m_useProxiesAct->setToolTip(tr("Play big videos from their light proxy copies. Export always uses the originals."));
    m_proxyCancel->setToolTip(tr("Stop making proxies"));
    m_shortcutsAct->setText(tr("Keyboard shortcuts..."));
    m_shuttleBackAct->setText(tr("Play backwards (J, again: faster)"));
    m_shuttleStopAct->setText(tr("Stop (K)"));
    m_shuttleFwdAct->setText(tr("Play forwards (L, again: faster)"));
    m_insertAct->setText(tr("Insert from Source"));
    m_insertAct->setToolTip(tr("Insert the source In-Out part at the playhead; later clips move right (,)"));
    m_overwriteAct->setText(tr("Overwrite from Source"));
    m_overwriteAct->setToolTip(tr("Put the source In-Out part at the playhead over what is there (.)"));
    m_markInAct->setText(tr("Mark In (Source)"));
    m_markInAct->setToolTip(tr("Start of the part to use (I)"));
    m_markOutAct->setText(tr("Mark Out (Source)"));
    m_markOutAct->setToolTip(tr("End of the part to use (O)"));
    m_clearInOutAct->setText(tr("Clear In and Out (Source)"));
    m_addMarkerAct->setText(tr("Add marker"));
    m_nextMarkerAct->setText(tr("Go to next marker"));
    m_prevMarkerAct->setText(tr("Go to previous marker"));
    m_renameMarkerAct->setText(tr("Rename marker..."));
    m_deleteMarkerAct->setText(tr("Delete marker"));
    m_importAct->setText(tr("Import media..."));
    m_sequenceAct->setText(tr("Sequence settings..."));
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
    m_tbSave->setToolTip(tr("Save project (Ctrl+S)"));
    m_tbExport->setText(tr("Export"));
    m_tbExport->setToolTip(tr("Export to MP4 (Ctrl+M)"));
    m_exportAct->setText(tr("Export..."));

    m_mediaDock->setWindowTitle(tr("Project"));
    m_inspectorDock->setWindowTitle(tr("Properties"));
    m_timelineDock->setWindowTitle(tr("Timeline"));
    m_scopesDock->setWindowTitle(tr("Scopes"));

    m_leftTabs->setTabText(0, tr("Media bin"));
    m_leftTabs->setTabText(1, tr("Project files"));
    m_rightTabs->setTabText(0, tr("Clip"));
    m_rightTabs->setTabText(1, tr("Effects"));
    m_rightTabs->setTabText(2, tr("Color"));
    m_rightTabs->setTabText(3, tr("Audio"));

    m_statusLabel->setText(m_ctl->canPlay() ? tr("Phase 10 done: loudness normalisation works (Clip > Normalize loudness).")
                                     : tr("The video engine (MLT) could not be loaded: playback is off. See the log file."));
}

// ---------------------------------------------------------------------------
// Monitors

void MainWindow::wireMonitors()
{
    // Program monitor position bar: the whole sequence with its markers.
    auto updateProgramBar = [this] {
        m_programMonitor->setDuration(m_ctl->timeline().duration());
        m_programMonitor->setPosition(m_ctl->playhead());
        QList<qint64> marks;
        for (const TimelineMarker &m : m_ctl->timeline().markers)
            marks << m.frame;
        m_programMonitor->setMarkers(marks);
    };
    connect(m_ctl, &ProjectController::timelineChanged, this, updateProgramBar);
    connect(m_ctl, &ProjectController::projectReset, this, updateProgramBar);
    connect(m_ctl, &ProjectController::playheadChanged, m_programMonitor, &MonitorView::setPosition);
    connect(m_programMonitor, &MonitorView::seekRequested, m_ctl, &ProjectController::setPlayhead);
    connect(m_programMonitor, &MonitorView::activated, this, [this] { setActiveMonitor(false); });
    // Working in the timeline makes the Program monitor active too.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (now && (m_timeline->isAncestorOf(now) || m_timeline == now))
            setActiveMonitor(false);
    });

    // Source monitor
    m_sourceMonitor->setDisabledReason([] {
        return tr("The video engine (MLT) could not be loaded: playback is off. See the log file.");
    });
    m_sourceMonitor->setTransportEnabled(false);
    m_sourceMonitor->addButton(m_markInAct, [] { return QStringLiteral("{ In"); });
    m_sourceMonitor->addButton(m_markOutAct, [] { return QStringLiteral("Out }"); });
    m_sourceMonitor->addButton(m_insertAct, [] { return tr("Insert"); });
    m_sourceMonitor->addButton(m_overwriteAct, [] { return tr("Overwrite"); });
    connect(m_ctl, &ProjectController::sourceFrameReady, m_sourceMonitor, &MonitorView::setImage);
    connect(m_ctl, &ProjectController::sourceChanged, this, [this] {
        updateSourceMonitor();
        if (!m_ctl->sourceMedia().isNull())
            setActiveMonitor(true);
        else
            m_sourceMonitor->setImage({});
    });
    connect(m_ctl, &ProjectController::sourcePositionChanged, this, [this](qint64 f) {
        m_sourceMonitor->setPosition(f);
        m_sourceMonitor->setTimecode(Fmt::frameTimecode(f, m_ctl->project().sequence.fps()));
    });
    connect(m_ctl, &ProjectController::sourceInOutChanged, this, &MainWindow::updateSourceMonitor);
    connect(m_ctl, &ProjectController::sequenceChanged, this, &MainWindow::updateSourceMonitor);
    connect(m_ctl, &ProjectController::sourcePlayingChanged, m_sourceMonitor, &MonitorView::setPlaying);
    connect(m_sourceMonitor, &MonitorView::activated, this, [this] { setActiveMonitor(true); });
    connect(m_sourceMonitor, &MonitorView::seekRequested, m_ctl, &ProjectController::setSourcePosition);
    connect(m_sourceMonitor, &MonitorView::playPause, m_ctl, &ProjectController::toggleSourcePlay);
    connect(m_sourceMonitor, &MonitorView::goToStart, this, [this] { m_ctl->setSourcePosition(0); });
    connect(m_sourceMonitor, &MonitorView::goToEnd, this,
            [this] { m_ctl->setSourcePosition(m_ctl->sourceDuration() - 1); });
    connect(m_sourceMonitor, &MonitorView::stepBack, this, [this] {
        m_ctl->sourcePause();
        m_ctl->setSourcePosition(m_ctl->sourcePosition() - 1);
    });
    connect(m_sourceMonitor, &MonitorView::stepForward, this, [this] {
        m_ctl->sourcePause();
        m_ctl->setSourcePosition(m_ctl->sourcePosition() + 1);
    });
    setActiveMonitor(false);
    updateSourceMonitor();
    updateEditActions();
}

void MainWindow::setActiveMonitor(bool source)
{
    m_sourceActive = source && !m_ctl->sourceMedia().isNull();
    m_sourceMonitor->setActive(m_sourceActive);
    m_programMonitor->setActive(!m_sourceActive);
}

void MainWindow::updateSourceMonitor()
{
    const bool open = !m_ctl->sourceMedia().isNull();
    m_sourceMonitor->setTransportEnabled(open && m_ctl->canPlay());
    m_sourceMonitor->setDuration(open ? m_ctl->sourceDuration() : 0);
    m_sourceMonitor->setPosition(m_ctl->sourcePosition());
    qint64 in = 0, len = 0;
    if (open && (m_ctl->hasSourceIn() || m_ctl->hasSourceOut()) && m_ctl->sourceRange(m_ctl->sourceMedia(), &in, &len))
        m_sourceMonitor->setInOut(in, in + len - 1);
    else
        m_sourceMonitor->setInOut(-1, -1);
    m_sourceMonitor->setTimecode(Fmt::frameTimecode(m_ctl->sourcePosition(), m_ctl->project().sequence.fps()));
    if (!m_ctl->sourceMedia().isNull()) {
        const MediaItem *m = m_ctl->project().item(m_ctl->sourceMedia());
        m_sourceMonitor->setToolTip(m ? m->name : QString());
    } else {
        m_sourceMonitor->setToolTip(QString());
    }
    updateEditActions();
}

void MainWindow::updateEditActions()
{
    const bool open = !m_ctl->sourceMedia().isNull();
    for (QAction *a : {m_markInAct, m_markOutAct, m_clearInOutAct, m_insertAct, m_overwriteAct})
        a->setEnabled(open);
    const bool onMarker = m_ctl->timeline().markerAt(m_ctl->playhead()) != nullptr;
    m_renameMarkerAct->setEnabled(onMarker);
    m_deleteMarkerAct->setEnabled(onMarker);
    const bool any = !m_ctl->timeline().markers.isEmpty();
    m_nextMarkerAct->setEnabled(any);
    m_prevMarkerAct->setEnabled(any);
}

void MainWindow::renameMarker()
{
    const TimelineMarker *m = m_ctl->timeline().markerAt(m_ctl->playhead());
    if (!m)
        return;
    const QUuid id = m->id;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename marker"), tr("Marker name:"), QLineEdit::Normal,
                                               m->name, &ok);
    if (ok)
        m_ctl->renameMarker(id, name.trimmed());
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
    m_scopesDock->hide();
    resizeDocks({m_mediaDock, m_inspectorDock}, {300, 320}, Qt::Horizontal);
    resizeDocks({m_timelineDock}, {300}, Qt::Vertical);
    const int half = m_monitorSplit->width() / 2;
    m_monitorSplit->setSizes({half, half});
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    saveLayout();
    QMainWindow::closeEvent(event);
}

// ---------------------------------------------------------------------------
// Project files

void MainWindow::updateTitle()
{
    setWindowTitle(QStringLiteral("%1%2 — %3 %4")
                       .arg(m_ctl->displayName(), m_ctl->isModified() ? QStringLiteral("*") : QString(),
                            appName(), appVersion()));
}

bool MainWindow::maybeSave()
{
    if (!m_ctl->isModified())
        return true;
    QMessageBox box(QMessageBox::Warning, tr("Unsaved changes"),
                    tr("Project \"%1\" has unsaved changes. Save them?").arg(m_ctl->displayName()),
                    QMessageBox::NoButton, this);
    box.setObjectName(QStringLiteral("unsavedBox"));
    QPushButton *save = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    QPushButton *discard = box.addButton(tr("Don't save"), QMessageBox::DestructiveRole);
    box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(save);
    box.exec();
    if (box.clickedButton() == save)
        return saveProject();
    return box.clickedButton() == discard;
}

void MainWindow::newProject()
{
    if (!maybeSave())
        return;
    m_ctl->newProject();
}

void MainWindow::openProject()
{
    if (!maybeSave())
        return;
    QSettings s;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open project"), s.value(QStringLiteral("project/lastDir")).toString(),
        tr("BUKHARA CINEMA projects (*.%1)").arg(Project::fileExtension()));
    if (!path.isEmpty())
        openProjectFile(path);
}

bool MainWindow::openProjectFile(const QString &path)
{
    QString err;
    if (!m_ctl->open(path, &err)) {
        QMessageBox::warning(this, tr("Open project"), tr("Cannot open \"%1\":\n%2").arg(path, err));
        return false;
    }
    QSettings().setValue(QStringLiteral("project/lastDir"), QFileInfo(path).absolutePath());
    if (const int missing = m_ctl->offlineCount(); missing > 0) {
        QMessageBox box(QMessageBox::Warning, tr("Missing files"),
                        tr("Media files not found: %1. They may have been moved or renamed.").arg(missing),
                        QMessageBox::NoButton, this);
        QPushButton *find = box.addButton(tr("Search in folder..."), QMessageBox::AcceptRole);
        box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == find)
            m_bin->findMissingInFolder();
    }
    return true;
}

bool MainWindow::saveProject()
{
    if (m_ctl->filePath().isEmpty())
        return saveProjectAs();
    QString err;
    if (!m_ctl->save(&err)) {
        QMessageBox::warning(this, tr("Save"), tr("Cannot save the project:\n%1").arg(err));
        return false;
    }
    statusBar()->showMessage(tr("Saved: %1").arg(QDir::toNativeSeparators(m_ctl->filePath())), 4000);
    return true;
}

bool MainWindow::saveProjectAs()
{
    QSettings s;
    QString dir = s.value(QStringLiteral("project/lastDir")).toString();
    const QString suggested = QDir(dir).filePath(m_ctl->displayName() + QLatin1Char('.') + Project::fileExtension());
    const QString path = QFileDialog::getSaveFileName(this, tr("Save project as"), suggested,
                                                      tr("BUKHARA CINEMA projects (*.%1)").arg(Project::fileExtension()));
    if (path.isEmpty())
        return false;
    QString err;
    if (!m_ctl->saveAs(path, &err)) {
        QMessageBox::warning(this, tr("Save"), tr("Cannot save the project:\n%1").arg(err));
        return false;
    }
    s.setValue(QStringLiteral("project/lastDir"), QFileInfo(m_ctl->filePath()).absolutePath());
    statusBar()->showMessage(tr("Saved: %1").arg(QDir::toNativeSeparators(m_ctl->filePath())), 4000);
    return true;
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList recent = ProjectController::recentProjects();
    if (recent.isEmpty()) {
        m_recentMenu->addAction(tr("(empty)"))->setEnabled(false);
        return;
    }
    for (const QString &p : recent) {
        m_recentMenu->addAction(QDir::toNativeSeparators(p), this, [this, p] {
            if (maybeSave())
                openProjectFile(p);
        });
    }
}

void MainWindow::updateUndoActions()
{
    const QUndoStack *st = m_ctl->undoStack();
    m_undoAct->setEnabled(st->canUndo());
    m_redoAct->setEnabled(st->canRedo());
    m_undoAct->setText(st->canUndo() ? tr("Undo: %1").arg(st->undoText()) : tr("Undo"));
    m_redoAct->setText(st->canRedo() ? tr("Redo: %1").arg(st->redoText()) : tr("Redo"));
}

void MainWindow::openExport()
{
    if (!m_exportAct->isEnabled())
        return;
    ExportDialog dialog(m_ctl, this);
    dialog.exec();
}

void MainWindow::addTitle()
{
    const QUuid id = m_ctl->addTitle(m_ctl->playhead());
    if (id.isNull())
        return;
    m_timeline->view()->setSelection({id});
    m_inspectorDock->show();
    m_rightTabs->setCurrentWidget(m_clipInspector);
    m_clipInspector->focusTitleText();
}

void MainWindow::openVersions()
{
    if (m_ctl->filePath().isEmpty())
        return;
    VersionsDialog dlg(m_ctl->filePath(), this);
    if (dlg.exec() != QDialog::Accepted || dlg.chosenPath().isEmpty())
        return;
    if (!maybeSave())
        return;
    const QString original = m_ctl->filePath();
    QString err;
    if (!m_ctl->openCopy(dlg.chosenPath(), original, &err)) {
        QMessageBox::warning(this, tr("Previous versions"), tr("Cannot open \"%1\":\n%2").arg(dlg.chosenPath(), err));
        return;
    }
    statusBar()->showMessage(tr("An earlier version is open. Save to make it the current project."), 8000);
}

void MainWindow::openCollect()
{
    CollectDialog dlg(m_ctl, this);
    if (dlg.exec() == QDialog::Accepted && !dlg.collectedProject().isEmpty() && maybeSave())
        openProjectFile(dlg.collectedProject());
}

void MainWindow::openCache()
{
    CacheDialog dlg(m_ctl, this);
    dlg.exec();
}

void MainWindow::updateProxyStatus()
{
    if (m_ctl->proxyJobsLeft() == 0) {
        m_proxyLabel->hide();
        m_proxyCancel->hide();
    }
}

bool MainWindow::checkRecovery()
{
    const QList<Autosave::Entry> entries = Autosave::orphaned();
    if (entries.isEmpty())
        return false;
    RecoveryDialog dlg(entries, this);
    if (dlg.exec() != QDialog::Accepted || !dlg.chosen())
        return false;
    const Autosave::Entry e = *dlg.chosen();
    if (!maybeSave())
        return false;
    QString err;
    if (!m_ctl->openCopy(e.dataPath, e.originalPath, &err)) {
        QMessageBox::warning(this, tr("Recover unsaved work"), tr("Cannot open \"%1\":\n%2").arg(e.dataPath, err));
        return false;
    }
    Autosave::discard(e); // this program autosaves it again from now on
    statusBar()->showMessage(tr("Unsaved work recovered. Save the project to keep it."), 8000);
    return true;
}

void MainWindow::editSequence()
{
    SequenceSettingsDialog dlg(m_ctl->project().sequence, this);
    if (dlg.exec() == QDialog::Accepted)
        m_ctl->setSequence(dlg.settings());
}

// ---------------------------------------------------------------------------
// Dialogs

void MainWindow::openSettings()
{
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
        m_autosave->setIntervalMinutes(Autosave::intervalSetting());
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
