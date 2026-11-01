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

namespace bc {

class MonitorView;
class ProjectController;
class MediaBinPanel;
class ProjectInfoPanel;
class TimelinePanel;

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
    QMenu *m_graphicsMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_windowMenu = nullptr;
    QMenu *m_helpMenu = nullptr;

    QAction *m_newAct = nullptr;
    QAction *m_openAct = nullptr;
    QMenu *m_recentMenu = nullptr;
    QAction *m_saveAct = nullptr;
    QAction *m_saveAsAct = nullptr;
    QAction *m_importAct = nullptr;
    QAction *m_sequenceAct = nullptr;
    QAction *m_undoAct = nullptr;
    QAction *m_redoAct = nullptr;
    QAction *m_settingsAct = nullptr;
    QAction *m_exitAct = nullptr;
    QAction *m_fullScreenAct = nullptr;
    QAction *m_resetLayoutAct = nullptr;
    QAction *m_statusAct = nullptr;
    QAction *m_licensesAct = nullptr;
    QAction *m_logFolderAct = nullptr;
    QAction *m_aboutAct = nullptr;

    QToolBar *m_toolbar = nullptr;
    QAction *m_tbSave = nullptr;
    QAction *m_tbExport = nullptr;

    QDockWidget *m_mediaDock = nullptr;
    QDockWidget *m_inspectorDock = nullptr;
    QDockWidget *m_timelineDock = nullptr;
    QTabWidget *m_leftTabs = nullptr;
    QTabWidget *m_rightTabs = nullptr;
    QSplitter *m_monitorSplit = nullptr;
    QLabel *m_statusLabel = nullptr;

    ProjectController *m_ctl = nullptr;
    MediaBinPanel *m_bin = nullptr;
    ProjectInfoPanel *m_info = nullptr;
    TimelinePanel *m_timeline = nullptr;
    MonitorView *m_programMonitor = nullptr;

    QByteArray m_defaultState;
    QByteArray m_defaultSplit;
};

} // namespace bc
