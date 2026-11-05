#include "app/Vst3Plugins.h"

#include "core/Vst3Host.h"

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
            bool loaded = false;
            std::string error;
            const std::vector<vst3::PluginInfo> infos = vst3::describeBundle(bundle.toStdString(), true, &loaded, &error);
            settings.remove(QString::fromLatin1(kScanningKey));
            settings.sync();
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
    if (auto it = s_ui.constFind(key); it != s_ui.constEnd())
        return it->get();
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

} // namespace bc
