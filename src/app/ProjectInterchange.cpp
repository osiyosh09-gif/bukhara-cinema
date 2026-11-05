// Export for other editors (phase 15a) and import from them (phase 15b):
// part of ProjectController.
#include "app/ProjectController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace bc {

namespace {

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

bool samePath(const QString &a, const QString &b)
{
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), kPathCase) == 0;
}

QString fileNameOf(const Interchange::ImportClip &c)
{
    const QString n = QFileInfo(c.path).fileName();
    return n.isEmpty() ? c.name : n;
}

} // namespace

Interchange::Result ProjectController::interchangeResult(Interchange::Format format) const
{
    return Interchange::write(m_project, format);
}

bool ProjectController::exportInterchange(Interchange::Format format, const QString &path, QString *error,
                                          Interchange::Result *result) const
{
    const Interchange::Result r = Interchange::write(m_project, format);
    if (result)
        *result = r;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(r.data) != r.data.size() || !f.commit()) {
        if (error)
            *error = tr("Could not write the file %1: %2").arg(QFileInfo(path).fileName(), f.errorString());
        return false;
    }
    return true;
}

bool ProjectController::readInterchange(const QString &path, Interchange::ReadResult *result, QString *error) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Could not open the file %1: %2").arg(QFileInfo(path).fileName(), f.errorString());
        return false;
    }
    const QByteArray data = f.readAll();
    Interchange::Format format;
    if (!Interchange::formatFromPath(path, &format)) {
        // By content when the name does not tell.
        const QByteArray head = data.left(4096).trimmed();
        format = head.startsWith('{') ? Interchange::Format::Otio
                 : head.contains("<xmeml") ? Interchange::Format::FcpXml
                                           : Interchange::Format::Edl;
    }
    *result = Interchange::read(data, format, m_project.sequence, QFileInfo(path).absolutePath());
    if (result->error == Interchange::ReadResult::Error::NotRecognized) {
        if (error)
            *error = tr("This file could not be read as a timeline.");
        return false;
    }
    if (result->error == Interchange::ReadResult::Error::Empty) {
        if (error)
            *error = tr("The file holds no clips.");
        return false;
    }
    return true;
}

QString ProjectController::resolveInterchangePath(const Interchange::ImportClip &c, const QString &baseDir,
                                                  bool *exists, QUuid *binItem) const
{
    *exists = false;
    *binItem = QUuid();
    const QString name = fileNameOf(c);
    QStringList candidates;
    if (!c.path.isEmpty())
        candidates << c.path;
    if (!name.isEmpty())
        candidates << QDir(baseDir).filePath(name); // moved together with the timeline file
    for (const QString &p : candidates)
        for (const MediaItem &m : m_project.items)
            if (samePath(m.path, p)) {
                *binItem = m.id;
                *exists = QFileInfo::exists(m.path);
                return m.path;
            }
    for (const QString &p : candidates)
        if (QFileInfo(p).isFile()) {
            *exists = true;
            return QDir::cleanPath(QFileInfo(p).absoluteFilePath());
        }
    if (c.path.isEmpty()) {
        // Only a name (EDL): a bin item with that file name.
        for (const MediaItem &m : m_project.items)
            if (QFileInfo(m.path).fileName().compare(name, kPathCase) == 0) {
                *binItem = m.id;
                *exists = QFileInfo::exists(m.path);
                return m.path;
            }
    }
    return candidates.isEmpty() ? QString() : candidates.first();
}

void ProjectController::interchangeMediaCount(const Interchange::ReadResult &r, const QString &baseDir, int *found,
                                              int *missing) const
{
    QSet<QString> seen;
    *found = *missing = 0;
    for (const Interchange::ImportClip &c : r.clips) {
        bool exists = false;
        QUuid item;
        const QString p = resolveInterchangePath(c, baseDir, &exists, &item);
        const QString key = QDir::cleanPath(p);
        if (p.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        ++(exists ? *found : *missing);
    }
}

int ProjectController::importInterchange(const QString &path, bool nested, QString *error)
{
    Interchange::ReadResult r;
    if (!readInterchange(path, &r, error))
        return 0;
    const QString baseDir = QFileInfo(path).absolutePath();
    const QString title = r.name.isEmpty() ? QFileInfo(path).completeBaseName() : r.name;

    // Media: one bin item per file. New files go into a folder named after
    // the timeline; files that do not exist become offline items.
    QHash<QString, QUuid> itemOf;
    QUuid folder;
    bool binChangedNow = false;
    QList<QUuid> mediaOf;
    for (const Interchange::ImportClip &c : r.clips) {
        bool exists = false;
        QUuid id;
        const QString p = resolveInterchangePath(c, baseDir, &exists, &id);
        const QString key = QDir::cleanPath(p);
        if (id.isNull())
            id = itemOf.value(key);
        if (id.isNull() && !p.isEmpty()) {
            if (folder.isNull())
                folder = m_project.addFolder(title).id;
            MediaItem &m = m_project.addItem(p, folder);
            id = m.id;
            if (exists) {
                m_online.insert(id);
                startProbe(m);
            } else {
                m.info.kind = c.kind == Timeline::Kind::Video ? MediaInfo::Kind::Video : MediaInfo::Kind::Audio;
            }
            binChangedNow = true;
        }
        itemOf.insert(key, id);
        mediaOf.append(id);
    }
    if (binChangedNow) {
        setModified(true);
        emit binChanged();
    }

    // The imported edit as its own sequence first.
    Timeline tl;
    for (int i = 0; i < std::max(1, r.videoTracks); ++i)
        tl.addTrack(Timeline::Kind::Video);
    for (int i = 0; i < std::max(1, r.audioTracks); ++i)
        tl.addTrack(Timeline::Kind::Audio);
    QHash<int, QUuid> links;
    QList<QPair<QUuid, const Interchange::ImportClip *>> placed;
    for (int i = 0; i < r.clips.size(); ++i) {
        const Interchange::ImportClip &ic = r.clips[i];
        if (mediaOf.value(i).isNull())
            continue;
        const int track = tl.indexOf(ic.kind, ic.track);
        if (track < 0)
            continue;
        TimelineClip c;
        c.id = QUuid::createUuid();
        c.mediaId = mediaOf[i];
        c.start = ic.start;
        c.in = ic.in;
        c.length = ic.length;
        if (ic.kind == Timeline::Kind::Audio)
            c.gainDb = std::clamp(ic.gainDb, TimelineTrack::kMinGainDb, TimelineTrack::kMaxGainDb);
        if (ic.link >= 0) {
            if (!links.contains(ic.link))
                links.insert(ic.link, QUuid::createUuid());
            c.linkId = links.value(ic.link);
        }
        tl.clearRange(track, c.start, c.end());
        tl.insertSorted(track, c);
        placed.append({c.id, &ic});
    }
    if (placed.isEmpty()) {
        if (error)
            *error = tr("The file holds no clips.");
        return 0;
    }
    // A clip linked to nothing left (its partner was not imported) is unlinked.
    QHash<QUuid, int> linkCount;
    for (const TimelineTrack &t : tl.tracks)
        for (const TimelineClip &c : t.clips)
            if (!c.linkId.isNull())
                ++linkCount[c.linkId];
    for (TimelineTrack &t : tl.tracks)
        for (TimelineClip &c : t.clips)
            if (!c.linkId.isNull() && linkCount.value(c.linkId) < 2)
                c.linkId = QUuid();
    for (const auto &p : placed) {
        if (!p.second->head.isNone())
            tl.setTransition(p.first, true, p.second->head);
        if (!p.second->tail.isNone())
            tl.setTransition(p.first, false, p.second->tail);
    }
    for (const Interchange::ImportMarker &m : r.markers)
        tl.addMarker(m.frame, m.name);

    const qint64 at = m_playhead;
    const int count = int(placed.size());
    const bool ok = editProject(tr("Import from other editors"), [&](EditState &s) {
        Timeline *cur = &s.main;
        for (NestedSequence &n : s.nested)
            if (n.id == s.open)
                cur = &n.timeline;
        if (nested) {
            NestedSequence nest;
            nest.id = QUuid::createUuid();
            nest.name = title;
            nest.timeline = tl;
            const qint64 length = tl.duration();
            bool hasVideo = false, hasAudio = false;
            for (const TimelineTrack &t : tl.tracks)
                if (!t.clips.isEmpty())
                    (t.kind == Timeline::Kind::Video ? hasVideo : hasAudio) = true;
            s.nested << nest;
            const QUuid link = hasVideo && hasAudio ? QUuid::createUuid() : QUuid();
            bool any = false;
            for (Timeline::Kind kind : {Timeline::Kind::Video, Timeline::Kind::Audio}) {
                if (!(kind == Timeline::Kind::Video ? hasVideo : hasAudio))
                    continue;
                const int track = cur->indexOf(kind, 0);
                if (track < 0 || cur->tracks[track].locked)
                    continue;
                TimelineClip c;
                c.id = QUuid::createUuid();
                c.nestId = nest.id;
                c.linkId = link;
                c.start = at;
                c.length = length;
                cur->clearRange(track, c.start, c.end());
                cur->insertSorted(track, c);
                any = true;
            }
            return any;
        }
        // Straight onto the open sequence: track n of the file -> track n here.
        bool any = false;
        for (Timeline::Kind kind : {Timeline::Kind::Video, Timeline::Kind::Audio}) {
            for (int j = 0; j < tl.count(kind); ++j) {
                const TimelineTrack &src = tl.tracks[tl.indexOf(kind, j)];
                if (src.clips.isEmpty())
                    continue;
                while (cur->indexOf(kind, j) < 0)
                    cur->addTrack(kind);
                const int track = cur->indexOf(kind, j);
                if (cur->tracks[track].locked)
                    continue;
                for (TimelineClip c : src.clips) {
                    c.start += at;
                    cur->clearRange(track, c.start, c.end());
                    cur->insertSorted(track, c);
                    any = true;
                }
            }
        }
        for (const TimelineMarker &m : tl.markers)
            cur->addMarker(m.frame + at, m.name);
        return any;
    });
    if (!ok) {
        if (error)
            *error = tr("The clips could not be placed: the first video and audio tracks are locked.");
        return 0;
    }
    return count;
}

} // namespace bc
