#include "core/Project.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace bc {

QString MediaInfo::kindToString(Kind k)
{
    switch (k) {
    case Kind::Video: return QStringLiteral("video");
    case Kind::Audio: return QStringLiteral("audio");
    case Kind::Image: return QStringLiteral("image");
    case Kind::Unknown: break;
    }
    return QStringLiteral("unknown");
}

MediaInfo::Kind MediaInfo::kindFromString(const QString &s)
{
    if (s == QLatin1String("video")) return Kind::Video;
    if (s == QLatin1String("audio")) return Kind::Audio;
    if (s == QLatin1String("image")) return Kind::Image;
    return Kind::Unknown;
}

// ---------------------------------------------------------------------------

BinFolder &Project::addFolder(const QString &folderName, const QUuid &parent)
{
    folders.append({QUuid::createUuid(), folderName, parent});
    return folders.last();
}

const BinFolder *Project::folder(const QUuid &id) const
{
    for (const auto &f : folders)
        if (f.id == id)
            return &f;
    return nullptr;
}

bool Project::renameFolder(const QUuid &id, const QString &newName)
{
    for (auto &f : folders) {
        if (f.id == id) {
            f.name = newName;
            return true;
        }
    }
    return false;
}

void Project::removeFolder(const QUuid &id)
{
    QSet<QUuid> doomed{id};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto &f : folders) {
            if (!doomed.contains(f.id) && doomed.contains(f.parentId)) {
                doomed.insert(f.id);
                grew = true;
            }
        }
    }
    folders.erase(std::remove_if(folders.begin(), folders.end(),
                                 [&](const BinFolder &f) { return doomed.contains(f.id); }),
                  folders.end());
    for (const auto &m : items)
        if (doomed.contains(m.folderId))
            for (Timeline *t : allTimelines())
                t->removeClipsOfMedia(m.id);
    items.erase(std::remove_if(items.begin(), items.end(),
                               [&](const MediaItem &m) { return doomed.contains(m.folderId); }),
                items.end());
}

MediaItem &Project::addItem(const QString &path, const QUuid &folderId)
{
    MediaItem m;
    m.id = QUuid::createUuid();
    m.path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    m.name = QFileInfo(path).fileName();
    m.folderId = folderId;
    items.append(m);
    return items.last();
}

NestedSequence *Project::nest(const QUuid &id)
{
    for (auto &n : nested)
        if (n.id == id)
            return &n;
    return nullptr;
}

const NestedSequence *Project::nest(const QUuid &id) const
{
    return const_cast<Project *>(this)->nest(id);
}

QList<Timeline *> Project::allTimelines()
{
    QList<Timeline *> out{&timeline};
    for (auto &n : nested)
        out << &n.timeline;
    return out;
}

MediaItem *Project::item(const QUuid &id)
{
    for (auto &m : items)
        if (m.id == id)
            return &m;
    return nullptr;
}

const MediaItem *Project::item(const QUuid &id) const
{
    return const_cast<Project *>(this)->item(id);
}

bool Project::removeItem(const QUuid &id)
{
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].id == id) {
            for (Timeline *t : allTimelines())
                t->removeClipsOfMedia(id);
            items.removeAt(i);
            return true;
        }
    }
    return false;
}

bool Project::containsPath(const QString &path) const
{
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    for (const auto &m : items)
        if (QString::compare(m.path, clean, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// JSON

namespace {

QString idStr(const QUuid &id)
{
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

QJsonObject infoToJson(const MediaInfo &i)
{
    QJsonObject o;
    o[QStringLiteral("kind")] = MediaInfo::kindToString(i.kind);
    o[QStringLiteral("durationUs")] = double(i.durationUs);
    o[QStringLiteral("width")] = i.width;
    o[QStringLiteral("height")] = i.height;
    o[QStringLiteral("fpsNum")] = i.fpsNum;
    o[QStringLiteral("fpsDen")] = i.fpsDen;
    o[QStringLiteral("videoCodec")] = i.videoCodec;
    o[QStringLiteral("audioCodec")] = i.audioCodec;
    o[QStringLiteral("sampleRate")] = i.sampleRate;
    o[QStringLiteral("channels")] = i.channels;
    o[QStringLiteral("fileSize")] = double(i.fileSize);
    o[QStringLiteral("fileModified")] = i.fileModified.toString(Qt::ISODate);
    return o;
}

MediaInfo infoFromJson(const QJsonObject &o)
{
    MediaInfo i;
    i.kind = MediaInfo::kindFromString(o.value(QStringLiteral("kind")).toString());
    i.durationUs = qint64(o.value(QStringLiteral("durationUs")).toDouble());
    i.width = o.value(QStringLiteral("width")).toInt();
    i.height = o.value(QStringLiteral("height")).toInt();
    i.fpsNum = o.value(QStringLiteral("fpsNum")).toInt();
    i.fpsDen = o.value(QStringLiteral("fpsDen")).toInt(1);
    i.videoCodec = o.value(QStringLiteral("videoCodec")).toString();
    i.audioCodec = o.value(QStringLiteral("audioCodec")).toString();
    i.sampleRate = o.value(QStringLiteral("sampleRate")).toInt();
    i.channels = o.value(QStringLiteral("channels")).toInt();
    i.fileSize = qint64(o.value(QStringLiteral("fileSize")).toDouble());
    i.fileModified = QDateTime::fromString(o.value(QStringLiteral("fileModified")).toString(), Qt::ISODate);
    return i;
}

} // namespace

QByteArray Project::toJson(const QString &projectFilePath) const
{
    const QDir projDir = QFileInfo(projectFilePath).absoluteDir();

    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("BUKHARA CINEMA project");
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("name")] = name;

    QJsonObject seq;
    seq[QStringLiteral("width")] = sequence.width;
    seq[QStringLiteral("height")] = sequence.height;
    seq[QStringLiteral("fpsNum")] = sequence.fpsNum;
    seq[QStringLiteral("fpsDen")] = sequence.fpsDen;
    seq[QStringLiteral("sampleRate")] = sequence.sampleRate;
    seq[QStringLiteral("channels")] = sequence.channels;
    root[QStringLiteral("sequence")] = seq;

    QJsonArray fa;
    for (const auto &f : folders) {
        QJsonObject o;
        o[QStringLiteral("id")] = idStr(f.id);
        o[QStringLiteral("name")] = f.name;
        o[QStringLiteral("parent")] = idStr(f.parentId);
        fa.append(o);
    }
    root[QStringLiteral("folders")] = fa;

    QJsonArray ia;
    for (const auto &m : items) {
        QJsonObject o;
        o[QStringLiteral("id")] = idStr(m.id);
        o[QStringLiteral("name")] = m.name;
        o[QStringLiteral("path")] = QDir::fromNativeSeparators(m.path);
        o[QStringLiteral("relativePath")] = projectFilePath.isEmpty()
                                                ? QString()
                                                : projDir.relativeFilePath(m.path);
        o[QStringLiteral("folder")] = idStr(m.folderId);
        o[QStringLiteral("probed")] = m.probed;
        o[QStringLiteral("info")] = infoToJson(m.info);
        ia.append(o);
    }
    root[QStringLiteral("media")] = ia;

    QJsonObject tl;
    tl[QStringLiteral("tracks")] = timeline.toJson();
    if (!timeline.markers.isEmpty())
        tl[QStringLiteral("markers")] = timeline.markersToJson();
    root[QStringLiteral("timeline")] = tl;

    if (!nested.isEmpty()) {
        QJsonArray na;
        for (const NestedSequence &n : nested) {
            QJsonObject no;
            no[QStringLiteral("id")] = idStr(n.id);
            no[QStringLiteral("name")] = n.name;
            no[QStringLiteral("tracks")] = n.timeline.toJson();
            if (!n.timeline.markers.isEmpty())
                no[QStringLiteral("markers")] = n.timeline.markersToJson();
            na.append(no);
        }
        root[QStringLiteral("nested")] = na;
    }

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool Project::fromJson(const QByteArray &json, const QString &projectFilePath, Project &out, QString *error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = pe.errorString();
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("BUKHARA CINEMA project")) {
        if (error)
            *error = QStringLiteral("not a BUKHARA CINEMA project");
        return false;
    }
    const int version = root.value(QStringLiteral("version")).toInt();
    if (version < 1 || version > kFormatVersion) {
        if (error)
            *error = QStringLiteral("unsupported project version %1").arg(version);
        return false;
    }

    Project p;
    p.name = root.value(QStringLiteral("name")).toString(p.name);
    const QJsonObject seq = root.value(QStringLiteral("sequence")).toObject();
    p.sequence.width = seq.value(QStringLiteral("width")).toInt(p.sequence.width);
    p.sequence.height = seq.value(QStringLiteral("height")).toInt(p.sequence.height);
    p.sequence.fpsNum = seq.value(QStringLiteral("fpsNum")).toInt(p.sequence.fpsNum);
    p.sequence.fpsDen = seq.value(QStringLiteral("fpsDen")).toInt(p.sequence.fpsDen);
    p.sequence.sampleRate = seq.value(QStringLiteral("sampleRate")).toInt(p.sequence.sampleRate);
    p.sequence.channels = seq.value(QStringLiteral("channels")).toInt(p.sequence.channels);

    for (const auto &v : root.value(QStringLiteral("folders")).toArray()) {
        const QJsonObject o = v.toObject();
        p.folders.append({QUuid(o.value(QStringLiteral("id")).toString()),
                          o.value(QStringLiteral("name")).toString(),
                          QUuid(o.value(QStringLiteral("parent")).toString())});
    }

    const QDir projDir = QFileInfo(projectFilePath).absoluteDir();
    for (const auto &v : root.value(QStringLiteral("media")).toArray()) {
        const QJsonObject o = v.toObject();
        MediaItem m;
        m.id = QUuid(o.value(QStringLiteral("id")).toString());
        if (m.id.isNull())
            m.id = QUuid::createUuid();
        m.name = o.value(QStringLiteral("name")).toString();
        m.path = QDir::cleanPath(o.value(QStringLiteral("path")).toString());
        // If the absolute path is gone but the relative one exists (the
        // project folder was moved or copied), use the relative one.
        const QString rel = o.value(QStringLiteral("relativePath")).toString();
        if (!QFileInfo::exists(m.path) && !rel.isEmpty() && !projectFilePath.isEmpty()) {
            const QString candidate = QDir::cleanPath(projDir.absoluteFilePath(rel));
            if (QFileInfo::exists(candidate))
                m.path = candidate;
        }
        m.folderId = QUuid(o.value(QStringLiteral("folder")).toString());
        m.probed = o.value(QStringLiteral("probed")).toBool();
        m.info = infoFromJson(o.value(QStringLiteral("info")).toObject());
        p.items.append(m);
    }

    for (const auto &v : root.value(QStringLiteral("nested")).toArray()) {
        const QJsonObject o = v.toObject();
        NestedSequence n;
        n.id = QUuid(o.value(QStringLiteral("id")).toString());
        if (n.id.isNull() || p.nest(n.id))
            continue;
        n.name = o.value(QStringLiteral("name")).toString();
        n.timeline = Timeline::fromJson(o.value(QStringLiteral("tracks")).toArray());
        n.timeline.markersFromJson(o.value(QStringLiteral("markers")).toArray());
        p.nested << n;
    }
    if (root.contains(QStringLiteral("timeline"))) {
        p.timeline = Timeline::fromJson(
            root.value(QStringLiteral("timeline")).toObject().value(QStringLiteral("tracks")).toArray());
        p.timeline.markersFromJson(
            root.value(QStringLiteral("timeline")).toObject().value(QStringLiteral("markers")).toArray());
    }
    // Drop clips whose media item or nested sequence is not in the project.
    QSet<QUuid> known;
    for (const auto &m : p.items)
        known.insert(m.id);
    for (const auto &n : p.nested)
        known.insert(n.id);
    for (Timeline *tl : p.allTimelines())
        for (auto &t : tl->tracks)
            t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(),
                                         [&](const TimelineClip &c) {
                                             return !c.isTitle() && !known.contains(c.isNested() ? c.nestId : c.mediaId);
                                         }),
                          t.clips.end());

    out = std::move(p);
    return true;
}

bool Project::save(const QString &filePath, QString *error) const
{
    QSaveFile f(filePath); // writes to a temp file, then replaces atomically
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(toJson(filePath));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool Project::load(const QString &filePath, Project &out, QString *error)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return fromJson(f.readAll(), filePath, out, error);
}

} // namespace bc
