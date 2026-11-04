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
#include "app/ClipInspector.h"
#include "app/ExportDialog.h"
#include "app/PlaceholderPanel.h"
#include "app/SettingsDialog.h"
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
    program->setEmptyText([this] {
        return m_ctl->timeline().isEmpty() ? MainWindow::tr("Drag media from the bin to the timeline.")
                                           : MainWindow::tr("No video at the playhead.");
    });
    m_programMonitor = program;
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
    m_rightTabs->addTab(new PlaceholderPanel([] { return MainWindow::tr("Effects"); }, 8), QString());
    m_rightTabs->addTab(new PlaceholderPanel([] { return MainWindow::tr("Color"); }, 9), QString());
    m_rightTabs->addTab(new PlaceholderPanel([] { return MainWindow::tr("Audio mixer"); }, 10), QString());
    m_inspectorDock = new QDockWidget(this);
    m_inspectorDock->setObjectName(QStringLiteral("inspectorDock"));
    m_inspectorDock->setWidget(m_rightTabs);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

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
    addPlanned(m_editMenu, QT_TR_NOOP("Cut"), 6, QStringLiteral("actCut"));
    addPlanned(m_editMenu, QT_TR_NOOP("Copy"), 6, QStringLiteral("actCopy"));
    addPlanned(m_editMenu, QT_TR_NOOP("Paste"), 6, QStringLiteral("actPaste"));
    m_editMenu->addAction(m_timeline->deleteAction());
    m_editMenu->addAction(m_timeline->rippleDeleteAction());
    m_editMenu->addAction(m_timeline->selectAllAction());
    m_editMenu->addSeparator();
    addPlanned(m_editMenu, QT_TR_NOOP("Keyboard shortcuts..."), 6, QStringLiteral("actShortcuts"));

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
    addPlanned(m_clipMenu, QT_TR_NOOP("Speed / duration..."), 7, QStringLiteral("actSpeed"));
    addPlanned(m_clipMenu, QT_TR_NOOP("Link / unlink audio and video"), 6, QStringLiteral("actLink"));
    addPlanned(m_clipMenu, QT_TR_NOOP("Nest..."), 6, QStringLiteral("actNest"));

    m_sequenceMenu = mb->addMenu(QString());
    m_sequenceAct = m_sequenceMenu->addAction(QString(), this, &MainWindow::editSequence);
    m_sequenceAct->setObjectName(QStringLiteral("actSequenceSettings"));
    m_sequenceMenu->addAction(m_timeline->addVideoTrackAction());
    m_sequenceMenu->addAction(m_timeline->addAudioTrackAction());
    m_sequenceMenu->addAction(m_timeline->snappingAction());
    m_sequenceMenu->addSeparator();
    m_playAct = m_sequenceMenu->addAction(QString(), m_ctl, &ProjectController::togglePlay);
    m_playAct->setObjectName(QStringLiteral("actPlayPause"));
    m_playAct->setShortcut(QKeySequence(Qt::Key_Space));
    m_playAct->setEnabled(m_ctl->canPlay());
    addPlanned(m_sequenceMenu, QT_TR_NOOP("Render preview"), 13, QStringLiteral("actRenderPreview"));

    m_markersMenu = mb->addMenu(QString());
    addPlanned(m_markersMenu, QT_TR_NOOP("Add marker"), 6, QStringLiteral("actAddMarker"));
    addPlanned(m_markersMenu, QT_TR_NOOP("Go to next marker"), 6, QStringLiteral("actNextMarker"));

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

    m_leftTabs->setTabText(0, tr("Media bin"));
    m_leftTabs->setTabText(1, tr("Project files"));
    m_rightTabs->setTabText(0, tr("Clip"));
    m_rightTabs->setTabText(1, tr("Effects"));
    m_rightTabs->setTabText(2, tr("Color"));
    m_rightTabs->setTabText(3, tr("Audio"));

    m_statusLabel->setText(m_ctl->canPlay() ? tr("Phase 5: editing, playback, titles, MP4 export, autosave and waveforms work.")
                                     : tr("The video engine (MLT) could not be loaded: playback is off. See the log file."));
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
