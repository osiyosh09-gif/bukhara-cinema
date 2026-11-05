#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

class QWidget;

namespace bc {

namespace vst3 { class Plugin; }
class ProjectController;

struct Vst3PluginEntry
{
    QString path;   // .vst3 bundle
    QString uid;    // class id
    QString name;
    QString vendor;
    QString category; // e.g. "Fx|Dynamics"
};

// The VST 3 audio effects on this computer (phase 15c). The list is kept in
// QSettings ("vst3/cache") and a bundle is read again only when it changed.
// A bundle without moduleinfo.json has to be loaded to be read; if the
// program dies meanwhile, that bundle is skipped from then on ("vst3/bad").
// Everything here runs on the GUI thread.
class Vst3Plugins
{
public:
    static bool available(); // this build hosts VST 3 plug-ins
    // Searched besides the standard VST 3 folders: QSettings "vst3/folders"
    // and the BC_VST3_PATH environment variable (list separated like PATH).
    static QStringList extraFolders();
    // Known plug-ins; the first call in a run looks at the folders.
    static QList<Vst3PluginEntry> plugins();
    // Looks at the folders again; returns the number of plug-ins.
    static int rescan();
    static QStringList badBundles();
    // A running instance for the interface (parameter names, value texts),
    // kept for the rest of the run; null with *error when it cannot start.
    static vst3::Plugin *uiInstance(const QString &path, const QString &uid, QString *error = nullptr);

    // --- phase 15c-2: the host program and crashes
    // Plug-ins run in BukharaPluginHost from `folder` when it is there.
    static void useHostProgram(const QString &folder);
    // Why the plug-in stopped in this run (crash, hang); empty when it did not.
    static QString crashMessage(const QString &path, const QString &uid);
    // Lets a crashed plug-in start again (playback starts it on the next frame).
    static void restart(const QString &path, const QString &uid);

    // --- the plug-in's own window (Windows, host program needed). It belongs
    // to one effect of one clip; what the user changes there goes through the
    // controller (one undo step per value), and undo moves its knobs back.
    static bool editorsAvailable();
    static bool openEditor(ProjectController *ctl, const QUuid &clip, int index, QWidget *owner, QString *error);
    static bool isEditorOpen(ProjectController *ctl, const QUuid &clip, int index);
    static void closeEditor(ProjectController *ctl, const QUuid &clip, int index);
    static void closeAllEditors();
    static int openEditorCount();
};

} // namespace bc
