#include "app/ProjectController.h"

#include "core/MediaProbe.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QUndoStack>
#include <QtConcurrent>

namespace bc {

namespace {
constexpr int kThumbW = 160;
constexpr int kThumbH = 90;
const char *kRecentKey = "project/recent";
constexpr int kProgramW = 960; // preview size: enough for the monitor, light for the CPU
constexpr int kProgramH = 540;
}

// Undo step that swaps whole timeline states. The timeline is small (a few
// hundred clips at most), so snapshots are simpler and safer than diffs.
class TimelineCommand : public QUndoCommand
{
public:
    TimelineCommand(ProjectController *c, const QString &text, Timeline before, Timeline after)
        : QUndoCommand(text), m_c(c), m_before(std::move(before)), m_after(std::move(after))
    {
    }
    void undo() override { m_c->applyTimeline(m_before); }
    void redo() override { m_c->applyTimeline(m_after); }

private:
    ProjectController *m_c;
    Timeline m_before;
    Timeline m_after;
};

ProjectController::ProjectController(QObject *parent)
    : QObject(parent)
{
    // The target PC has 2 cores: keep background decoding modest.
    m_pool.setMaxThreadCount(2);
    m_undo = new QUndoStack(this);
    m_undo->setUndoLimit(200);
}

ProjectController::~ProjectController()
{
    // The undo stack emits index changes while it is destroyed; nobody may
    // react to them any more (the main window is being destroyed too).
    m_undo->blockSignals(true);
    ++m_generation;
    m_pool.clear();
    m_pool.waitForDone();
}

QString ProjectController::displayName() const
{
    if (!m_filePath.isEmpty())
        return QFileInfo(m_filePath).completeBaseName();
    return tr("Untitled");
}

// ---------------------------------------------------------------------------
// File operations

void ProjectController::newProject()
{
    ++m_generation;
    m_pool.clear();
    m_project = Project();
    m_thumbs.clear();
    m_pendingProbe.clear();
    m_online.clear();
    m_playhead = 0;
    m_lastFrameKey.clear();
    resetUndo();
    setFilePath({});
    setModified(false);
    emit projectReset();
    emit timelineChanged();
    requestProgramFrame();
}

bool ProjectController::open(const QString &path, QString *error)
{
    Project p;
    if (!Project::load(path, p, error))
        return false;
    ++m_generation;
    m_pool.clear();
    m_project = std::move(p);
    m_project.name = QFileInfo(path).completeBaseName();
    m_thumbs.clear();
    m_pendingProbe.clear();
    setFilePath(QFileInfo(path).absoluteFilePath());
    refreshOnlineState();
    m_playhead = 0;
    m_lastFrameKey.clear();
    resetUndo();
    setModified(false);
    addRecent(m_filePath);
    emit projectReset();
    emit timelineChanged();
    requestProgramFrame();

    for (const MediaItem &m : m_project.items) {
        if (!m_online.contains(m.id))
            continue;
        if (!m.probed)
            startProbe(m);
        else
            startThumbnail(m);
    }
    return true;
}

bool ProjectController::save(QString *error)
{
    if (m_filePath.isEmpty()) {
        if (error)
            *error = QStringLiteral("no file name");
        return false;
    }
    return saveAs(m_filePath, error);
}

bool ProjectController::saveAs(const QString &path, QString *error)
{
    QString target = path;
    if (QFileInfo(target).suffix().compare(Project::fileExtension(), Qt::CaseInsensitive) != 0)
        target += QLatin1Char('.') + Project::fileExtension();
    m_project.name = QFileInfo(target).completeBaseName();
    if (!m_project.save(target, error))
        return false;
    setFilePath(QFileInfo(target).absoluteFilePath());
    setModified(false);
    addRecent(m_filePath);
    return true;
}

QStringList ProjectController::recentProjects()
{
    QStringList list = QSettings().value(kRecentKey).toStringList();
    list.erase(std::remove_if(list.begin(), list.end(), [](const QString &p) { return !QFileInfo::exists(p); }),
               list.end());
    return list;
}

void ProjectController::addRecent(const QString &path)
{
    QSettings s;
    QStringList list = s.value(kRecentKey).toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 8)
        list.removeLast();
    s.setValue(kRecentKey, list);
}

void ProjectController::setModified(bool m)
{
    if (m_modified == m)
        return;
    m_modified = m;
    emit modifiedChanged(m);
}

void ProjectController::setFilePath(const QString &path)
{
    if (m_filePath == path)
        return;
    m_filePath = path;
    emit fileNameChanged(path);
}

// ---------------------------------------------------------------------------
// Bin

int ProjectController::importFiles(const QStringList &paths, const QUuid &folder)
{
    QStringList files;
    for (const QString &p : paths) {
        QFileInfo fi(p);
        if (fi.isDir()) {
            QDirIterator it(fi.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString f = it.next();
                if (MediaProbe::isSupportedFile(f))
                    files << f;
            }
        } else if (fi.isFile() && MediaProbe::isSupportedFile(p)) {
            files << fi.absoluteFilePath();
        }
    }
    files.sort(Qt::CaseInsensitive);

    int added = 0;
    for (const QString &f : files) {
        if (m_project.containsPath(f))
            continue;
        const QUuid id = m_project.addItem(f, folder).id;
        m_online.insert(id);
        startProbe(*m_project.item(id));
        ++added;
    }
    if (added) {
        setModified(true);
        emit binChanged();
    }
    return added;
}

QUuid ProjectController::addFolder(const QString &name, const QUuid &parent)
{
    const QUuid id = m_project.addFolder(name, parent).id;
    setModified(true);
    emit binChanged();
    return id;
}

void ProjectController::renameFolder(const QUuid &id, const QString &name)
{
    if (name.trimmed().isEmpty())
        return;
    if (m_project.renameFolder(id, name.trimmed())) {
        setModified(true);
        emit binChanged();
    }
}

void ProjectController::renameItem(const QUuid &id, const QString &name)
{
    MediaItem *m = m_project.item(id);
    if (!m || name.trimmed().isEmpty() || m->name == name.trimmed())
        return;
    m->name = name.trimmed();
    setModified(true);
    emit binChanged();
}

void ProjectController::removeFolder(const QUuid &id)
{
    const Timeline before = m_project.timeline;
    m_project.removeFolder(id);
    setModified(true);
    emit binChanged();
    if (m_project.timeline != before) {
        resetUndo(); // undo steps could bring back clips of removed media
        emit timelineChanged();
        requestProgramFrame();
    }
}

void ProjectController::removeItems(const QList<QUuid> &ids)
{
    const Timeline before = m_project.timeline;
    bool any = false;
    for (const QUuid &id : ids) {
        any |= m_project.removeItem(id);
        m_thumbs.remove(id);
        m_online.remove(id);
    }
    if (any) {
        setModified(true);
        emit binChanged();
    }
    if (m_project.timeline != before) {
        resetUndo(); // undo steps could bring back clips of removed media
        emit timelineChanged();
        requestProgramFrame();
    }
}

void ProjectController::moveItems(const QList<QUuid> &ids, const QUuid &folder)
{
    bool any = false;
    for (const QUuid &id : ids) {
        if (MediaItem *m = m_project.item(id); m && m->folderId != folder) {
            m->folderId = folder;
            any = true;
        }
    }
    if (any) {
        setModified(true);
        emit binChanged();
    }
}

void ProjectController::moveFolder(const QUuid &id, const QUuid &newParent)
{
    if (id == newParent)
        return;
    // Refuse to move a folder into its own sub-folder.
    for (QUuid p = newParent; !p.isNull();) {
        if (p == id)
            return;
        const BinFolder *f = m_project.folder(p);
        p = f ? f->parentId : QUuid();
    }
    for (auto &f : m_project.folders) {
        if (f.id == id && f.parentId != newParent) {
            f.parentId = newParent;
            setModified(true);
            emit binChanged();
            return;
        }
    }
}

bool ProjectController::isOnline(const QUuid &id) const
{
    return m_online.contains(id);
}

int ProjectController::offlineCount() const
{
    return int(m_project.items.size()) - int(m_online.size());
}

void ProjectController::refreshOnlineState()
{
    m_online.clear();
    for (const MediaItem &m : m_project.items)
        if (QFileInfo(m.path).isFile())
            m_online.insert(m.id);
}

QString ProjectController::relinkMismatch(const QUuid &id, const QString &newPath) const
{
    const MediaItem *m = m_project.item(id);
    if (!m || !m->probed)
        return {};
    MediaInfo info;
    QString err;
    if (!MediaProbe::probe(newPath, info, &err))
        return err;
    if (info.kind != m->info.kind)
        return tr("different media type");
    if (m->info.durationUs > 0 && qAbs(info.durationUs - m->info.durationUs) > 1000000)
        return tr("different duration");
    return {};
}

bool ProjectController::relink(const QUuid &id, const QString &newPath, int *alsoRelinked, QString *error)
{
    MediaItem *m = m_project.item(id);
    if (!m)
        return false;
    MediaInfo info;
    if (!MediaProbe::probe(newPath, info, error))
        return false;
    const QString oldName = QFileInfo(m->path).fileName();
    m->path = QDir::cleanPath(QFileInfo(newPath).absoluteFilePath());
    m->info = info;
    m->probed = true;
    if (m->name == oldName)
        m->name = QFileInfo(newPath).fileName();
    m_online.insert(id);
    m_thumbs.remove(id);
    startThumbnail(*m);
    emit itemUpdated(id);

    // Other missing files are often in the same new folder.
    const int others = relinkFromFolder(QFileInfo(newPath).absolutePath());
    if (alsoRelinked)
        *alsoRelinked = others;
    setModified(true);
    return true;
}

int ProjectController::relinkFromFolder(const QString &folder)
{
    QHash<QString, QString> byName; // lower-case file name -> path
    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        byName.insert(QFileInfo(f).fileName().toLower(), f);
    }
    int count = 0;
    for (MediaItem &m : m_project.items) {
        if (m_online.contains(m.id))
            continue;
        const QString candidate = byName.value(QFileInfo(m.path).fileName().toLower());
        if (candidate.isEmpty())
            continue;
        m.path = QDir::cleanPath(candidate);
        m_online.insert(m.id);
        startProbe(m);
        emit itemUpdated(m.id);
        ++count;
    }
    if (count)
        setModified(true);
    return count;
}

void ProjectController::setSequence(const SequenceSettings &s)
{
    if (m_project.sequence == s)
        return;
    const double oldFps = m_project.sequence.fps();
    m_project.sequence = s;
    if (oldFps != s.fps() && !m_project.timeline.isEmpty()) {
        // Clip positions are stored in frames: convert them to the new rate.
        m_project.timeline.rescale(oldFps, s.fps());
        m_playhead = qint64(std::llround(m_playhead * s.fps() / oldFps));
        resetUndo();
        emit timelineChanged();
        emit playheadChanged(m_playhead);
    }
    setModified(true);
    emit sequenceChanged();
    requestProgramFrame();
}

// ---------------------------------------------------------------------------
// Timeline

void ProjectController::resetUndo()
{
    m_undo->clear();
}

void ProjectController::applyTimeline(const Timeline &t)
{
    m_project.timeline = t;
    setModified(true);
    emit timelineChanged();
    requestProgramFrame();
}

bool ProjectController::editTimeline(const QString &text, const std::function<bool(Timeline &)> &fn)
{
    Timeline after = m_project.timeline;
    if (!fn(after) || after == m_project.timeline)
        return false;
    m_undo->push(new TimelineCommand(this, text, m_project.timeline, after));
    return true;
}

qint64 ProjectController::mediaFrames(const QUuid &mediaId) const
{
    const MediaItem *m = m_project.item(mediaId);
    return m ? mediaLengthFrames(*m, m_project.sequence) : -1;
}

bool ProjectController::previewPlacement(Timeline &t, const QList<QUuid> &mediaIds, int track, qint64 start,
                                         QList<QUuid> *created) const
{
    if (track < 0 || track >= t.tracks.size())
        return false;
    const Timeline::Kind kind = t.tracks[track].kind;
    int n = 0;
    for (int i = 0; i < track; ++i)
        if (t.tracks[i].kind == kind)
            ++n;
    const int videoN = std::min(n, t.count(Timeline::Kind::Video) - 1);
    const int audioN = std::min(n, t.count(Timeline::Kind::Audio) - 1);

    QList<QUuid> made;
    qint64 pos = std::max<qint64>(0, start);
    for (const QUuid &id : mediaIds) {
        const MediaItem *m = m_project.item(id);
        if (!m || !m->probed || !isOnline(id))
            continue;
        const QList<QUuid> ids = t.place(*m, m_project.sequence, videoN, audioN, pos);
        if (ids.isEmpty())
            continue;
        made += ids;
        pos += defaultClipFrames(*m, m_project.sequence);
    }
    if (created)
        *created = made;
    return !made.isEmpty();
}

QList<QUuid> ProjectController::placeMedia(const QList<QUuid> &mediaIds, int track, qint64 start)
{
    QList<QUuid> created;
    editTimeline(tr("Place clips"),
                 [&](Timeline &t) { return previewPlacement(t, mediaIds, track, start, &created); });
    return created;
}

bool ProjectController::moveClips(const QList<QUuid> &clipIds, qint64 deltaFrames, Timeline::Kind kind,
                                  int deltaTracks)
{
    return editTimeline(tr("Move clips"), [&](Timeline &t) {
        return t.move(clipIds, deltaFrames, kind, deltaTracks);
    });
}

bool ProjectController::trimClip(const QUuid &clipId, bool head, qint64 frame)
{
    return editTimeline(tr("Trim clip"), [&](Timeline &t) {
        return t.trim(clipId, head, frame, [this](const QUuid &mediaId) { return mediaFrames(mediaId); });
    });
}

bool ProjectController::splitClips(qint64 frame, const QList<QUuid> &clipIds)
{
    return editTimeline(tr("Split"), [&](Timeline &t) { return !t.splitAt(frame, clipIds).isEmpty(); });
}

bool ProjectController::removeClips(const QList<QUuid> &clipIds, bool ripple)
{
    return editTimeline(ripple ? tr("Ripple delete") : tr("Delete"),
                        [&](Timeline &t) { return t.remove(clipIds, ripple); });
}

int ProjectController::addTrack(Timeline::Kind kind)
{
    int index = -1;
    editTimeline(kind == Timeline::Kind::Video ? tr("Add video track") : tr("Add audio track"), [&](Timeline &t) {
        index = t.addTrack(kind);
        return true;
    });
    return index;
}

void ProjectController::setTrackFlag(int track, const QString &flag, bool on)
{
    editTimeline(tr("Track setting"), [&](Timeline &t) {
        if (track < 0 || track >= t.tracks.size())
            return false;
        TimelineTrack &tr = t.tracks[track];
        bool *f = flag == QLatin1String("locked") ? &tr.locked
                  : flag == QLatin1String("muted") ? &tr.muted
                  : flag == QLatin1String("hidden") ? &tr.hidden
                                                    : nullptr;
        if (!f || *f == on)
            return false;
        *f = on;
        return true;
    });
}

void ProjectController::setPlayhead(qint64 frame)
{
    frame = std::max<qint64>(0, frame);
    if (frame == m_playhead)
        return;
    m_playhead = frame;
    emit playheadChanged(frame);
    requestProgramFrame();
}

// The Program monitor shows the frame of the top visible video clip at the
// playhead. Decoding runs in the background; while one frame is being
// decoded, newer requests collapse into one.
void ProjectController::requestProgramFrame()
{
    if (m_frameBusy) {
        m_framePending = true;
        return;
    }
    startProgramFrame();
}

void ProjectController::startProgramFrame()
{
    m_framePending = false;
    const Timeline &tl = m_project.timeline;
    QString path;
    qint64 timeUs = 0;
    for (const QUuid &cid : tl.clipsAt(m_playhead)) {
        const Timeline::ClipRef r = tl.find(cid);
        const TimelineTrack &track = tl.tracks[r.track];
        if (track.kind != Timeline::Kind::Video || track.hidden)
            continue;
        const TimelineClip &c = track.clips[r.index];
        const MediaItem *m = m_project.item(c.mediaId);
        if (!m || !isOnline(m->id))
            continue;
        path = m->path;
        const double fps = m_project.sequence.fps();
        timeUs = fps > 0 ? qint64(std::llround((c.in + (m_playhead - c.start)) * 1e6 / fps)) : 0;
        break;
    }
    const QString key = path + QLatin1Char('@') + QString::number(timeUs);
    if (key == m_lastFrameKey)
        return;
    m_lastFrameKey = key;
    if (path.isEmpty()) {
        emit programFrameReady({});
        return;
    }
    m_frameBusy = true;
    QPointer<ProjectController> self(this);
    (void)QtConcurrent::run(&m_pool, [self, path, timeUs] {
        const QImage img = MediaProbe::frameAt(path, timeUs, kProgramW, kProgramH);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, img] {
            if (!self)
                return;
            self->m_frameBusy = false;
            emit self->programFrameReady(img);
            if (self->m_framePending)
                self->startProgramFrame();
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// Background work

QString ProjectController::thumbCachePath(const QString &mediaPath)
{
    const QFileInfo fi(mediaPath);
    const QByteArray key = (fi.absoluteFilePath() + QString::number(fi.size())
                            + QString::number(fi.lastModified().toMSecsSinceEpoch()))
                               .toUtf8();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/thumbs/") + hash
           + QStringLiteral(".jpg");
}

void ProjectController::startProbe(const MediaItem &item)
{
    const QUuid id = item.id;
    const QString path = item.path;
    const quint64 gen = m_generation;
    m_pendingProbe.insert(id);

    QPointer<ProjectController> self(this);
    QThreadPool *pool = &m_pool;
    (void)QtConcurrent::run(pool, [self, id, path, gen] {
        MediaInfo info;
        QString err;
        const bool ok = MediaProbe::probe(path, info, &err);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, id, path, gen, ok, info] {
            if (!self || gen != self->m_generation)
                return;
            self->m_pendingProbe.remove(id);
            MediaItem *m = self->m_project.item(id);
            if (!m || m->path != path)
                return;
            if (ok) {
                m->info = info;
                m->probed = true;
                self->setModified(true);
                self->startThumbnail(*m);
            }
            emit self->itemUpdated(id);
        }, Qt::QueuedConnection);
    });
}

void ProjectController::startThumbnail(const MediaItem &item)
{
    if (item.info.kind == MediaInfo::Kind::Audio || item.info.kind == MediaInfo::Kind::Unknown)
        return;
    const QUuid id = item.id;
    const QString path = item.path;
    const quint64 gen = m_generation;
    QPointer<ProjectController> self(this);
    (void)QtConcurrent::run(&m_pool, [self, id, path, gen] {
        const QString cache = thumbCachePath(path);
        QImage img(cache);
        if (img.isNull()) {
            img = MediaProbe::thumbnail(path, kThumbW, kThumbH);
            if (!img.isNull()) {
                QDir().mkpath(QFileInfo(cache).absolutePath());
                img.save(cache, "JPG", 85);
            }
        }
        if (!self || img.isNull())
            return;
        QMetaObject::invokeMethod(self.data(), [self, id, path, gen, img] {
            if (!self || gen != self->m_generation)
                return;
            const MediaItem *m = self->m_project.item(id);
            if (!m || m->path != path)
                return;
            self->m_thumbs.insert(id, img);
            emit self->itemUpdated(id);
        }, Qt::QueuedConnection);
    });
}

void ProjectController::waitForBackgroundWork()
{
    // Thumbnails are queued by probe results, so loop until quiet.
    for (int i = 0; i < 50; ++i) {
        m_pool.waitForDone();
        QCoreApplication::processEvents();
        if (m_pool.activeThreadCount() == 0 && m_pendingProbe.isEmpty()) {
            m_pool.waitForDone();
            QCoreApplication::processEvents();
            if (m_pool.activeThreadCount() == 0)
                return;
        }
    }
}

} // namespace bc
