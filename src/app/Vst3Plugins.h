#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace bc {

namespace vst3 { class Plugin; }

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
};

} // namespace bc
