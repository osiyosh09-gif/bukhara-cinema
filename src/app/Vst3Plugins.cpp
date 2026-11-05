#include "app/Vst3Plugins.h"

#include "app/ProjectController.h"
#include "core/Vst3Host.h"

#include <QCoreApplication>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>
#include <memory>

namespace bc {

namespace {

constexpr const char *kCacheKey = "vst3/cache";
constexpr const char *kScanningKey = "vst3/scanning";
constexpr const char *kBadKey = "vst3/bad";
constexpr const char *kFoldersKey = "vst3/folders";

bool s_scanned = false;
QList<Vst3PluginEntry> s_list;
QHash<QString, std::shared_ptr<vst3::Plugin>> s_ui;
QHash<QString, QString> s_uiErrors;

// When a bundle last changed: its newest file (a bundle is a folder).
qint64 stamp(const QString &path)
{
    QFileInfo fi(path);
    qint64 t = fi.lastModified().toMSecsSinceEpoch();
    if (fi.isDir()) {
        const QDir contents(QDir(path).filePath(QStringLiteral("Contents")));
        for (const QFileInfo &sub : contents.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            for (const QFileInfo &f : QDir(sub.filePath()).entryInfoList(QDir::Files))
                t = std::max(t, f.lastModified().toMSecsSinceEpoch());
    }
    return t;
}

// One spelling per bundle (separators, case of the drive, links).
QString normalized(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(QDir::fromNativeSeparators(path)) : canonical;
}

QStringList allBundles()
{
    QStringList out;
    for (const std::string &b : vst3::standardBundles())
        out << normalized(QString::fromStdString(b));
    for (const QString &folder : Vst3Plugins::extraFolders())
        for (const std::string &b : vst3::findBundles(folder.toStdString()))
            out << normalized(QString::fromStdString(b));
    out.removeDuplicates();
    out.sort(Qt::CaseInsensitive);
    return out;
}

void scan()
{
    s_scanned = true;
    s_list.clear();
    if (!vst3::available())
        return;
    QSettings settings;
    QStringList bad = settings.value(QString::fromLatin1(kBadKey)).toStringList();
    // A bundle being read when the program last stopped is not read again.
    const QString crashed = settings.value(QString::fromLatin1(kScanningKey)).toString();
    if (!crashed.isEmpty()) {
        if (!bad.contains(normalized(crashed)))
            bad << normalized(crashed);
        settings.setValue(QString::fromLatin1(kBadKey), bad);
        settings.remove(QString::fromLatin1(kScanningKey));
    }
    const QJsonObject oldCache =
        QJsonDocument::fromJson(settings.value(QString::fromLatin1(kCacheKey)).toByteArray()).object();
    QJsonObject cache;
    for (const QString &bundle : allBundles()) {
        if (bad.contains(bundle))
            continue;
        const qint64 t = stamp(bundle);
        QJsonObject entry = oldCache.value(bundle).toObject();
        if (entry.value(QStringLiteral("stamp")).toDouble() != double(t)) {
            settings.setValue(QString::fromLatin1(kScanningKey), bundle);
            settings.sync();
            bool crashedNow = false;
            std::string error;
            // In the host program when there is one: a crash there only marks the bundle.
            const std::vector<vst3::PluginInfo> infos = vst3::describeBundleIsolated(bundle.toStdString(), &crashedNow, &error);
            settings.remove(QString::fromLatin1(kScanningKey));
            settings.sync();
            if (crashedNow) {
                bad << bundle;
                settings.setValue(QString::fromLatin1(kBadKey), bad);
                continue;
            }
            QJsonArray classes;
            for (const vst3::PluginInfo &i : infos)
                classes.append(QJsonObject{{QStringLiteral("uid"), QString::fromStdString(i.uid)},
                                           {QStringLiteral("name"), QString::fromStdString(i.name)},
                                           {QStringLiteral("vendor"), QString::fromStdString(i.vendor)},
                                           {QStringLiteral("category"), QString::fromStdString(i.subCategories)}});
            entry = QJsonObject{{QStringLiteral("stamp"), double(t)}, {QStringLiteral("classes"), classes}};
        }
        cache.insert(bundle, entry);
        for (const QJsonValue &v : entry.value(QStringLiteral("classes")).toArray()) {
            const QJsonObject c = v.toObject();
            s_list.append({bundle, c.value(QStringLiteral("uid")).toString(), c.value(QStringLiteral("name")).toString(),
                           c.value(QStringLiteral("vendor")).toString(), c.value(QStringLiteral("category")).toString()});
        }
    }
    settings.setValue(QString::fromLatin1(kCacheKey), QJsonDocument(cache).toJson(QJsonDocument::Compact));
    std::sort(s_list.begin(), s_list.end(), [](const Vst3PluginEntry &a, const Vst3PluginEntry &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
}

} // namespace

bool Vst3Plugins::available()
{
    return vst3::available();
}

QStringList Vst3Plugins::extraFolders()
{
    QStringList out = QSettings().value(QString::fromLatin1(kFoldersKey)).toStringList();
    const QString env = qEnvironmentVariable("BC_VST3_PATH");
    if (!env.isEmpty())
        out << env.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    out.removeDuplicates();
    return out;
}

QList<Vst3PluginEntry> Vst3Plugins::plugins()
{
    if (!s_scanned)
        scan();
    return s_list;
}

int Vst3Plugins::rescan()
{
    scan();
    return int(s_list.size());
}

QStringList Vst3Plugins::badBundles()
{
    return QSettings().value(QString::fromLatin1(kBadKey)).toStringList();
}

vst3::Plugin *Vst3Plugins::uiInstance(const QString &path, const QString &uid, QString *error)
{
    const QString key = path + QLatin1Char('|') + uid;
    if (auto it = s_ui.constFind(key); it != s_ui.constEnd()) {
        if (!(*it)->crashed())
            return it->get();
        s_ui.remove(key); // its host program ended: start again (or tell why not)
        s_uiErrors.remove(key);
    }
    if (s_uiErrors.contains(key)) {
        if (error)
            *error = s_uiErrors.value(key);
        return nullptr;
    }
    std::string err;
    std::unique_ptr<vst3::Plugin> p = vst3::Plugin::create(path.toStdString(), uid.toStdString(), &err);
    if (!p) {
        s_uiErrors.insert(key, QString::fromStdString(err));
        if (error)
            *error = QString::fromStdString(err);
        return nullptr;
    }
    vst3::Plugin *raw = p.get();
    s_ui.insert(key, std::shared_ptr<vst3::Plugin>(std::move(p)));
    return raw;
}

QString Vst3Plugins::crashMessage(const QString &path, const QString &uid)
{
    return QString::fromStdString(vst3::crashMessage(path.toStdString(), uid.toStdString()));
}

void Vst3Plugins::restart(const QString &path, const QString &uid)
{
    vst3::forgetCrash(path.toStdString(), uid.toStdString());
    const QString key = path + QLatin1Char('|') + uid;
    s_ui.remove(key);
    s_uiErrors.remove(key);
}

void Vst3Plugins::useHostProgram(const QString &folder)
{
#ifdef Q_OS_WIN
    const QString exe = QDir(folder).filePath(QStringLiteral("BukharaPluginHost.exe"));
#else
    const QString exe = QDir(folder).filePath(QStringLiteral("BukharaPluginHost"));
#endif
    if (QFileInfo(exe).isExecutable())
        vst3::setHostProgram(QDir::toNativeSeparators(exe).toStdString());
}

// --- the plug-ins' own windows (phase 15c-2)

namespace {

struct EditorSession
{
    int serial = 0;
    QPointer<ProjectController> ctl;
    QUuid clip;
    int index = -1;
    QString path, uid;
    std::unique_ptr<vst3::Plugin> plugin;
};

std::vector<std::unique_ptr<EditorSession>> s_editors;
int s_serial = 0;
QObject *s_context = nullptr;
QSet<ProjectController *> s_watched;

EditorSession *session(int serial)
{
    for (const auto &e : s_editors)
        if (e->serial == serial)
            return e.get();
    return nullptr;
}

void closeSession(int serial)
{
    for (auto it = s_editors.begin(); it != s_editors.end(); ++it)
        if ((*it)->serial == serial) {
            std::unique_ptr<EditorSession> gone = std::move(*it);
            s_editors.erase(it);
            gone->plugin->closeEditor();
            return; // `gone` ends its host program
        }
}

std::map<uint32_t, double> pluginValues(const ClipEffect &e)
{
    std::map<uint32_t, double> values;
    for (auto it = e.params.cbegin(); it != e.params.cend(); ++it) {
        quint32 id = 0;
        if (ClipEffect::pluginParamId(it.key(), &id))
            values[id] = e.value(it.key());
    }
    return values;
}

// The effect a window belongs to, or null when it is gone or is another one now.
const ClipEffect *effectOf(const EditorSession &s, ClipEffects *holder)
{
    if (!s.ctl)
        return nullptr;
    *holder = s.ctl->clipEffects(s.clip);
    if (s.index < 0 || s.index >= holder->size())
        return nullptr;
    const ClipEffect &e = (*holder)[s.index];
    if (e.kind != ClipEffect::Kind::Plugin || e.pluginPath != s.path || e.pluginId != s.uid)
        return nullptr;
    return &e;
}

// Undo, redo or a slider moved: the open windows show the project's values.
void syncEditors(ProjectController *ctl)
{
    QList<int> stale;
    for (const auto &s : s_editors) {
        if (s->ctl != ctl)
            continue;
        ClipEffects holder;
        const ClipEffect *e = effectOf(*s, &holder);
        if (!e || s->plugin->crashed()) {
            stale << s->serial;
            continue;
        }
        s->plugin->setParameters(pluginValues(*e));
    }
    for (int serial : stale)
        closeSession(serial);
}

void watch(ProjectController *ctl)
{
    if (!s_context) {
        s_context = new QObject;
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, s_context, [] { Vst3Plugins::closeAllEditors(); });
    }
    if (s_watched.contains(ctl))
        return;
    s_watched.insert(ctl);
    QObject::connect(ctl, &ProjectController::timelineChanged, s_context, [ctl] { syncEditors(ctl); });
    QObject::connect(ctl, &ProjectController::projectReset, s_context, [ctl] {
        QList<int> all;
        for (const auto &s : s_editors)
            if (s->ctl == ctl)
                all << s->serial;
        for (int serial : all)
            closeSession(serial);
    });
    QObject::connect(ctl, &QObject::destroyed, s_context, [ctl] {
        s_watched.remove(ctl);
        QList<int> all;
        for (const auto &s : s_editors)
            if (!s->ctl || s->ctl == ctl)
                all << s->serial;
        for (int serial : all)
            closeSession(serial);
    });
}

} // namespace

bool Vst3Plugins::editorsAvailable()
{
#ifdef Q_OS_WIN
    return vst3::available() && !vst3::hostProgram().empty();
#else
    return false;
#endif
}

bool Vst3Plugins::openEditor(ProjectController *ctl, const QUuid &clip, int index, QWidget *owner, QString *error)
{
    for (const auto &s : s_editors)
        if (s->ctl == ctl && s->clip == clip && s->index == index && s->plugin->editorOpen())
            return s->plugin->openEditor(owner ? quint64(owner->window()->winId()) : 0, {}, nullptr); // to the front
    closeEditor(ctl, clip, index);
    if (!editorsAvailable()) // the panel does not offer it then
        return false;
    auto s = std::make_unique<EditorSession>();
    s->serial = ++s_serial;
    s->ctl = ctl;
    s->clip = clip;
    s->index = index;
    ClipEffects holder = ctl->clipEffects(clip);
    if (index < 0 || index >= holder.size() || holder[index].kind != ClipEffect::Kind::Plugin)
        return false;
    const ClipEffect &e = holder[index];
    s->path = e.pluginPath;
    s->uid = e.pluginId;
    std::string err;
    s->plugin = vst3::Plugin::create(e.pluginPath.toStdString(), e.pluginId.toStdString(), &err);
    if (!s->plugin) {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    s->plugin->setParameters(pluginValues(e));
    const int serial = s->serial;
    s->plugin->setEditorCallbacks(
        [serial](uint32_t id, double v) {
            // From the host program's thread: the change goes through the controller (undo).
            QMetaObject::invokeMethod(qApp, [serial, id, v] {
                EditorSession *s = session(serial);
                if (s && s->ctl)
                    s->ctl->setClipEffectValue(s->clip, s->index, ClipEffect::pluginParamKey(id), v);
            }, Qt::QueuedConnection);
        },
        [serial] { QMetaObject::invokeMethod(qApp, [serial] { closeSession(serial); }, Qt::QueuedConnection); });
    watch(ctl);
    const QString title = e.pluginName.isEmpty() ? e.pluginId : e.pluginName;
    if (!s->plugin->openEditor(owner ? quint64(owner->window()->winId()) : 0, title.toStdString(), &err)) {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    s_editors.push_back(std::move(s));
    return true;
}

bool Vst3Plugins::isEditorOpen(ProjectController *ctl, const QUuid &clip, int index)
{
    for (const auto &s : s_editors)
        if (s->ctl == ctl && s->clip == clip && s->index == index && s->plugin->editorOpen())
            return true;
    return false;
}

void Vst3Plugins::closeEditor(ProjectController *ctl, const QUuid &clip, int index)
{
    QList<int> found;
    for (const auto &s : s_editors)
        if (s->ctl == ctl && s->clip == clip && s->index == index)
            found << s->serial;
    for (int serial : found)
        closeSession(serial);
}

void Vst3Plugins::closeAllEditors()
{
    while (!s_editors.empty())
        closeSession(s_editors.front()->serial);
}

int Vst3Plugins::openEditorCount()
{
    return int(s_editors.size());
}

} // namespace bc
