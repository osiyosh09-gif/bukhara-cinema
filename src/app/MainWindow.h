#pragma once

#include <QByteArray>
#include <QMainWindow>

class QAction;
class QDockWidget;
class QMenu;
class QTabWidget;
class QToolBar;
class QSplitter;
class QLabel;
class QToolButton;

namespace bc {

class MonitorView;
class ProjectController;
class MediaBinPanel;
class ProjectInfoPanel;
class TimelinePanel;
class ClipInspector;
class EffectsPanel;
class SubtitlesPanel;
class ScopesPanel;
class ExportQueue;
class ExportQueueDialog;
class MulticamMonitor;
class Autosave;

// Main window layout:
//   top     - menus and toolbar (save, export)
//   left    - media bin and project files
//   centre  - Source and Program monitors
//   right   - clip parameters, effects, color, audio
//   bottom  - multitrack timeline
// All panels are QDockWidgets: resizable, movable, closable; the layout is
// stored in QSettings and can be reset from Window > Reset layout.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void resetLayout();

    ProjectController *controller() const { return m_ctl; }
    // Opens a project file (used for the command line / file association).
    bool openProjectFile(const QString &path);
    // Offers autosaved copies left by a crash. Returns true if one was opened.
    bool checkRecovery();
    Autosave *autosave() const { return m_autosave; }

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildCentral();
    void buildDocks();
    void buildMenus();
    void buildToolbar();
    void retranslate();
    void saveLayout();
    void restoreLayout();

    // A menu entry for a feature that is not implemented yet: it is disabled
    // and shows the phase in which it will arrive.
    QAction *addPlanned(QMenu *menu, const char *sourceText, int phase, const QString &objectName);

    void openSettings();
    void openAbout();
    void openLicenses();
    void openStatus();
    void openLogFolder();

    // Project file handling
    void newProject();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    bool maybeSave(); // false = user cancelled
    void updateTitle();
    void rebuildRecentMenu();
    void editSequence();
    void updateUndoActions();
    void addTitle(); // Graphics > New text: title at the playhead, ready to type
    void openExport(); // File > Export
    void renderPreview();
    void openExportQueue(); // File > Export queue (phase 13a)
    void openInterchange(); // File > Export for other editors (phase 15a)
    void openImportInterchange(); // File > Import from other editors (phase 15b)
    void openVersions(); // File > Previous versions
    void openCollect();  // File > Collect project
    void openCache();    // File > Media cache
    void updateProxyStatus();
    void wireMonitors();      // Source and Program monitors, J/K/L, In/Out
    void setActiveMonitor(bool source);
    void updateSourceMonitor();
    void updateEditActions();
    void renameMarker();      // Markers > Rename marker (at the playhead)

    struct PlannedAction
    {
        QAction *action;
        const char *sourceText;
        int phase;
    };
    QList<PlannedAction> m_planned;

    QMenu *m_fileMenu = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_clipMenu = nullptr;
    QMenu *m_sequenceMenu = nullptr;
    QMenu *m_markersMenu = nullptr;
    QMenu *m_toolsMenu = nullptr;
    QMenu *m_graphicsMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_windowMenu = nullptr;
    QMenu *m_helpMenu = nullptr;

    QAction *m_newAct = nullptr;
    QAction *m_openAct = nullptr;
    QMenu *m_recentMenu = nullptr;
    QAction *m_saveAct = nullptr;
    QAction *m_saveAsAct = nullptr;
    QAction *m_versionsAct = nullptr;
    QAction *m_collectAct = nullptr;
    QAction *m_cacheAct = nullptr;
    QAction *m_useProxiesAct = nullptr;
    QAction *m_renderPreviewAct = nullptr;
    QAction *m_deletePreviewsAct = nullptr;
    QAction *m_mixLoudnessAct = nullptr;
    QAction *m_autoMulticamAct = nullptr;
    QAction *m_importAct = nullptr;
    QAction *m_sequenceAct = nullptr;
    QAction *m_undoAct = nullptr;
    QAction *m_redoAct = nullptr;
    QAction *m_playAct = nullptr;
    QAction *m_newTextAct = nullptr;
    QAction *m_exportAct = nullptr;
    QAction *m_exportQueueAct = nullptr;
    QAction *m_interchangeAct = nullptr;
    QAction *m_importInterchangeAct = nullptr;
    ExportQueue *m_exportQueue = nullptr;
    ExportQueueDialog *m_exportQueueDialog = nullptr;
public:
    ExportQueue *exportQueue() const { return m_exportQueue; }
private:
    QAction *m_audioGainAct = nullptr;
    QAction *m_settingsAct = nullptr;
    QAction *m_exitAct = nullptr;
    QAction *m_fullScreenAct = nullptr;
    QAction *m_resetLayoutAct = nullptr;
    QAction *m_statusAct = nullptr;
    QAction *m_licensesAct = nullptr;
    QAction *m_logFolderAct = nullptr;
    QAction *m_aboutAct = nullptr;
    QAction *m_shortcutsAct = nullptr;
    QAction *m_shuttleBackAct = nullptr;
    QAction *m_shuttleStopAct = nullptr;
    QAction *m_shuttleFwdAct = nullptr;
    QAction *m_markInAct = nullptr;
    QAction *m_markOutAct = nullptr;
    QAction *m_clearInOutAct = nullptr;
    QAction *m_insertAct = nullptr;
    QAction *m_overwriteAct = nullptr;
    QAction *m_addMarkerAct = nullptr;
    QAction *m_nextMarkerAct = nullptr;
    QAction *m_prevMarkerAct = nullptr;
    QAction *m_renameMarkerAct = nullptr;
    QAction *m_deleteMarkerAct = nullptr;

    QToolBar *m_toolbar = nullptr;
    QAction *m_tbSave = nullptr;
    QAction *m_tbExport = nullptr;

    QDockWidget *m_mediaDock = nullptr;
    QDockWidget *m_inspectorDock = nullptr;
    QDockWidget *m_timelineDock = nullptr;
    QTabWidget *m_leftTabs = nullptr;
    QTabWidget *m_rightTabs = nullptr;
    EffectsPanel *m_effectsPanel = nullptr;
    SubtitlesPanel *m_subtitlesPanel = nullptr;
    ScopesPanel *m_scopes = nullptr;
    QDockWidget *m_scopesDock = nullptr;
    QAction *m_scopesAct = nullptr;
    MulticamMonitor *m_multicam = nullptr;   // phase 12b
    QDockWidget *m_multicamDock = nullptr;
    QAction *m_multicamAct = nullptr;
    QMenu *m_cameraMenu = nullptr;
    QMenu *m_soundMenu = nullptr;
    QList<QAction *> m_cameraActs;           // 1..9
    QList<QAction *> m_soundActs;            // Alt+1..9
    void updateMulticamActions();
    ClipInspector *m_clipInspector = nullptr;
    QSplitter *m_monitorSplit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_proxyLabel = nullptr;
    QToolButton *m_proxyCancel = nullptr;
    QLabel *m_previewLabel = nullptr;
    QToolButton *m_previewCancel = nullptr;

    ProjectController *m_ctl = nullptr;
    Autosave *m_autosave = nullptr;
    MediaBinPanel *m_bin = nullptr;
    ProjectInfoPanel *m_info = nullptr;
    TimelinePanel *m_timeline = nullptr;
    MonitorView *m_programMonitor = nullptr;
    MonitorView *m_sourceMonitor = nullptr;
    bool m_sourceActive = false; // J/K/L and Space act on the Source monitor

    QByteArray m_defaultState;
    QByteArray m_defaultSplit;
};

} // namespace bc
