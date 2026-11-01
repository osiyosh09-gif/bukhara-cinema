#include "app/ProjectController.h"

#include "core/Backups.h"
#include "core/MediaCache.h"
#include "core/MediaProbe.h"
#include "core/Waveform.h"
#include "engine/TimelinePlayer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
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
constexpr int kDefaultBackups = 20;
constexpr int kProgramW = 960; // preview size: enough for the monitor, light for the CPU
constexpr int kProgramH = 540;
}

// Undo step that swaps whole timeline states. The timeline is small (a few
// hundred clips at most), so snapshots are simpler and safer than diffs.
class TimelineCommand : public QUndoCommand
{
public:
    TimelineCommand(ProjectController *c, const QString &text, Timeline before, Timeline after,
                    QString mergeKey = QString())
        : QUndoCommand(text), m_c(c), m_before(std::move(before)), m_after(std::move(after)),
          m_mergeKey(std::move(mergeKey))
    {
    }
    void undo() override { m_c->applyTimeline(m_before); }
    void redo() override { m_c->applyTimeline(m_after); }
    // Commands with a merge key (typing in a title, dragging a volume
    // slider) join the previous command with the same key.
    int id() const override { return m_mergeKey.isEmpty() ? -1 : 1; }
    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *o = static_cast<const TimelineCommand *>(other);
        if (o->m_mergeKey != m_mergeKey)
            return false;
        m_after = o->m_after;
        return true;
    }

private:
    ProjectController *m_c;
    Timeline m_before;
    Timeline m_after;
    QString m_mergeKey;
};

ProjectController::ProjectController(QObject *parent)
    : QObject(parent)
{
    // The target PC has 2 cores: keep background decoding modest.
    m_pool.setMaxThreadCount(2);
    m_proxyPool.setMaxThreadCount(1);
    m_useProxies = proxiesEnabledSetting();
    m_undo = new QUndoStack(this);
    m_undo->setUndoLimit(200);

    if (!Engine::init(&m_engineError))
        qWarning("Video engine not available: %s", qPrintable(m_engineError));
    if (Engine::isReady()) {
        m_engineError.clear();
        m_player = new TimelinePlayer(this);
        m_player->setPreviewLimit(QSize(kProgramW, kProgramH));
        connect(m_player, &TimelinePlayer::playingChanged, this, &ProjectController::playingChanged);
        connect(m_player, &TimelinePlayer::frameShown, this, [this](const QImage &img, qint64 frame) {
            m_playhead = frame;
            emit playheadChanged(frame);
            emit programFrameReady(img);
        });
        connect(m_player, &TimelinePlayer::reachedEnd, this, [this] {
            m_lastFrameKey.clear();
            requestProgramFrame();
        });
        connect(m_player, &TimelinePlayer::playingChanged, this, [this](bool on) {
            if (!on) {
                m_lastFrameKey.clear(); // show the exact still frame where playback stopped
                requestProgramFrame();
            }
        });
        m_player->setProject(m_project);

        m_sourcePlayer = new TimelinePlayer(this);
        m_sourcePlayer->setPreviewLimit(QSize(kProgramW, kProgramH));
        connect(m_sourcePlayer, &TimelinePlayer::playingChanged, this, &ProjectController::sourcePlayingChanged);
        connect(m_sourcePlayer, &TimelinePlayer::frameShown, this, [this](const QImage &img, qint64 frame) {
            m_sourcePos = frame;
            emit sourcePositionChanged(frame);
            emit sourceFrameReady(img);
        });
        connect(m_sourcePlayer, &TimelinePlayer::playingChanged, this, [this](bool on) {
            if (!on)
                requestSourceFrame();
        });
    }
}

ProjectController::~ProjectController()
{
    // The undo stack emits index changes while it is destroyed; nobody may
    // react to them any more (the main window is being destroyed too).
    m_undo->blockSignals(true);
    ++m_generation;
    m_proxyQueue.clear();
    if (m_proxyCancel)
        m_proxyCancel->store(true);
    m_pool.clear();
    m_pool.waitForDone();
    m_proxyPool.waitForDone();
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
    m_waves.clear();
    m_proxies.clear();
    cancelProxies();
    m_pendingProbe.clear();
    m_inPoints.clear();
    m_outPoints.clear();
    m_online.clear();
    m_playhead = 0;
    m_lastFrameKey.clear();
    resetUndo();
    syncPlayer();
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
    adopt(std::move(p), QFileInfo(path).absoluteFilePath(), false);
    return true;
}

bool ProjectController::openCopy(const QString &dataPath, const QString &originalPath, QString *error)
{
    QFile f(dataPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    // Media paths are resolved as if the file were still at its original place.
    Project p;
    if (!Project::fromJson(f.readAll(), originalPath.isEmpty() ? dataPath : originalPath, p, error))
        return false;
    adopt(std::move(p), originalPath.isEmpty() ? QString() : QFileInfo(originalPath).absoluteFilePath(), true);
    return true;
}

void ProjectController::adopt(Project &&p, const QString &filePath, bool modified)
{
    ++m_generation;
    m_pool.clear();
    m_project = std::move(p);
    if (!filePath.isEmpty())
        m_project.name = QFileInfo(filePath).completeBaseName();
    m_thumbs.clear();
    m_waves.clear();
    m_proxies.clear();
    cancelProxies();
    m_pendingProbe.clear();
    m_inPoints.clear();
    m_outPoints.clear();
    setFilePath(filePath);
    refreshOnlineState();
    // Proxies made earlier: known before the first graph is built.
    for (const MediaItem &m : m_project.items) {
        const QString proxy = MediaCache::pathFor(MediaCache::Kind::Proxy, m.path);
        if (m.info.kind == MediaInfo::Kind::Video && m_online.contains(m.id) && QFileInfo::exists(proxy))
            m_proxies.insert(m.id, proxy);
    }
    m_playhead = 0;
    m_lastFrameKey.clear();
    resetUndo();
    syncPlayer();
    m_modified = !modified; // make sure the signal is sent
    setModified(modified);
    if (!filePath.isEmpty() && !modified)
        addRecent(m_filePath);
    emit projectReset();
    emit timelineChanged();
    requestProgramFrame();

    for (const MediaItem &m : m_project.items) {
        if (!m_online.contains(m.id))
            continue;
        if (!m.probed) {
            startProbe(m);
        } else {
            startThumbnail(m);
            startWaveform(m);
            checkProxy(m);
        }
    }
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
    // The file about to be replaced becomes a previous version (unless the
    // save changes nothing).
    QFile existing(target);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() != m_project.toJson(target)) {
        QString backupError;
        if (!Backups::backup(target, QSettings().value(QStringLiteral("backup/keep"), kDefaultBackups).toInt(),
                             &backupError))
            qWarning("backup failed: %s", qPrintable(backupError));
    }
    existing.close();
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
    if (m)
        ++m_changeSerial;
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
        syncPlayer();
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
        syncSourcePlayer(); // closes the Source monitor if its item is gone
    }
    if (m_project.timeline != before) {
        resetUndo(); // undo steps could bring back clips of removed media
        syncPlayer();
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
        syncSourcePlayer(); // closes the Source monitor if its item is gone
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
    syncPlayer();
    m_lastFrameKey.clear();
    requestProgramFrame();
    emit timelineChanged(); // clips of this media are no longer shown as missing
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
    if (count) {
        setModified(true);
        syncPlayer();
        m_lastFrameKey.clear();
        requestProgramFrame();
        emit timelineChanged();
    }
    return count;
}

void ProjectController::setSequence(const SequenceSettings &s)
{
    if (m_project.sequence == s)
        return;
    const double oldFps = m_project.sequence.fps();
    m_project.sequence = s;
    if (oldFps > 0 && oldFps != s.fps()) {
        // Source In/Out points are frames too.
        for (auto *points : {&m_inPoints, &m_outPoints})
            for (auto it = points->begin(); it != points->end(); ++it)
                *it = qint64(std::llround(*it * s.fps() / oldFps));
        m_sourcePos = qint64(std::llround(m_sourcePos * s.fps() / oldFps));
    }
    if (oldFps != s.fps() && (!m_project.timeline.isEmpty() || !m_project.timeline.markers.isEmpty())) {
        // Clip positions are stored in frames: convert them to the new rate.
        m_project.timeline.rescale(oldFps, s.fps());
        m_playhead = qint64(std::llround(m_playhead * s.fps() / oldFps));

        resetUndo();
        emit timelineChanged();
        emit playheadChanged(m_playhead);
    }
    syncPlayer();
    m_lastFrameKey.clear();
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

void ProjectController::syncPlayer()
{
    ++m_graphRev;
    if (m_player) {
        m_player->setProxies(m_useProxies ? m_proxies : QHash<QUuid, QString>());
        m_player->setProject(m_project);
    } // stops playback; the graph follows the edit
    syncSourcePlayer();
}

void ProjectController::applyTimeline(const Timeline &t)
{
    m_project.timeline = t;
    syncPlayer();
    setModified(true);
    emit timelineChanged();
    requestProgramFrame();
}

bool ProjectController::editTimeline(const QString &text, const std::function<bool(Timeline &)> &fn,
                                     const QString &mergeKey)
{
    Timeline after = m_project.timeline;
    if (!fn(after) || after == m_project.timeline)
        return false;
    m_undo->push(new TimelineCommand(this, text, m_project.timeline, after, mergeKey));
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
        qint64 in = 0, len = 0;
        if (!m || !sourceRange(id, &in, &len)) // not read yet or offline
            continue;
        // The In/Out points set in the Source monitor choose the part.
        const QList<QUuid> ids = t.place(*m, videoN, audioN, pos, in, len);
        if (ids.isEmpty())
            continue;
        made += ids;
        pos += len;
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
                  : flag == QLatin1String("solo")   ? &tr.solo
                                                    : nullptr;
        if (f == &tr.solo && tr.kind != Timeline::Kind::Audio)
            return false;
        if (!f || *f == on)
            return false;
        *f = on;
        return true;
    });
}

QUuid ProjectController::addTitle(qint64 start)
{
    QUuid id;
    const double fps = m_project.sequence.fps();
    const qint64 length = std::max<qint64>(1, qint64(std::llround(5 * (fps > 0 ? fps : 25))));
    start = std::max<qint64>(0, start);
    editTimeline(tr("Add title"), [&](Timeline &t) {
        // Highest free unlocked video track, so the title lies over the picture.
        int n = -1;
        for (int i = t.count(Timeline::Kind::Video) - 1; i >= 0 && n < 0; --i) {
            const TimelineTrack &track = t.tracks[t.indexOf(Timeline::Kind::Video, i)];
            if (track.locked)
                continue;
            const bool busy = std::any_of(track.clips.begin(), track.clips.end(), [&](const TimelineClip &c) {
                return c.start < start + length && c.end() > start;
            });
            if (!busy)
                n = i;
        }
        if (n < 0) {
            t.addTrack(Timeline::Kind::Video);
            n = t.count(Timeline::Kind::Video) - 1;
        }
        TitleData title;
        title.text = tr("Title");
        id = t.placeTitle(title, n, start, length);
        return !id.isNull();
    });
    return id;
}

bool ProjectController::setClipTitle(const QUuid &clipId, const TitleData &title)
{
    return editTimeline(tr("Edit title"), [&](Timeline &t) {
        const Timeline::ClipRef r = t.find(clipId);
        if (!r.isValid() || t.tracks[r.track].locked)
            return false;
        TimelineClip &c = t.tracks[r.track].clips[r.index];
        if (!c.isTitle())
            return false;
        c.title = title;
        c.title.size = std::clamp(c.title.size, 8, 400);
        return true;
    }, QStringLiteral("title:") + clipId.toString());
}

bool ProjectController::setClipAudio(const QUuid &clipId, double gainDb, qint64 fadeIn, qint64 fadeOut)
{
    return editTimeline(tr("Clip volume"), [&](Timeline &t) {
        const Timeline::ClipRef r = t.find(clipId);
        if (!r.isValid() || t.tracks[r.track].locked || t.tracks[r.track].kind != Timeline::Kind::Audio)
            return false;
        TimelineClip &c = t.tracks[r.track].clips[r.index];
        c.gainDb = std::clamp(gainDb, -60.0, 12.0);
        c.fadeIn = std::clamp<qint64>(fadeIn, 0, c.length);
        c.fadeOut = std::clamp<qint64>(fadeOut, 0, c.length - c.fadeIn);
        return true;
    }, QStringLiteral("audio:") + clipId.toString());
}

void ProjectController::setPlayhead(qint64 frame)
{
    frame = std::max<qint64>(0, frame);
    if (frame == m_playhead)
        return;
    if (isPlaying()) {
        // Jumping while playing continues playback from the new place.
        m_player->pause();
        m_playhead = frame;
        emit playheadChanged(frame);
        m_player->play(frame);
        return;
    }
    m_playhead = frame;
    emit playheadChanged(frame);
    requestProgramFrame();
}

bool ProjectController::startExport(const ExportSettings &settings, QString *error)
{
    if (!m_exporter) {
        m_exporter = new Exporter(this);
        connect(m_exporter, &Exporter::progress, this, &ProjectController::exportProgress);
        connect(m_exporter, &Exporter::finished, this, &ProjectController::exportFinished);
    }
    pause();
    return m_exporter->start(m_project, settings, error);
}

void ProjectController::cancelExport()
{
    if (m_exporter)
        m_exporter->cancel();
}

bool ProjectController::isExporting() const
{
    return m_exporter && m_exporter->isRunning();
}

bool ProjectController::canPlay() const
{
    return m_player != nullptr && Engine::isReady();
}

bool ProjectController::isPlaying() const
{
    return m_player && m_player->isPlaying();
}

bool ProjectController::play()
{
    if (!m_player || m_project.timeline.isEmpty())
        return false;
    sourcePause(); // one monitor plays at a time (one sound device)
    if (m_playhead >= m_project.timeline.duration() - 1) {
        m_playhead = 0; // at the end: start again from the beginning
        emit playheadChanged(0);
    }
    return m_player->play(m_playhead);
}

void ProjectController::pause()
{
    if (m_player)
        m_player->pause();
}

void ProjectController::togglePlay()
{
    isPlaying() ? pause() : (void)play();
}

// The Program monitor shows the frame of the top visible video clip at the
// playhead. Decoding runs in the background; while one frame is being
// decoded, newer requests collapse into one.
void ProjectController::requestProgramFrame()
{
    if (isPlaying())
        return; // playback delivers its own frames
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
    QString path;          // top media clip, for decoding without the engine
    qint64 timeUs = 0;
    bool visible = false;  // anything to show at all
    for (const QUuid &cid : tl.clipsAt(m_playhead)) {
        const Timeline::ClipRef r = tl.find(cid);
        const TimelineTrack &track = tl.tracks[r.track];
        if (track.kind != Timeline::Kind::Video || track.hidden)
            continue;
        const TimelineClip &c = track.clips[r.index];
        if (c.isTitle()) {
            visible = true;
            continue;
        }
        const MediaItem *m = m_project.item(c.mediaId);
        if (!m || !isOnline(m->id))
            continue;
        visible = true;
        if (path.isEmpty()) {
            path = m->path;
            const double fps = m_project.sequence.fps();
            timeUs = fps > 0 ? qint64(std::llround((c.in + (m_playhead - c.start)) * 1e6 / fps)) : 0;
        }
    }
    if (!m_player && path.isEmpty())
        visible = false; // titles need the engine
    // With the engine every layer counts, so any edit (graph revision) or
    // playhead move asks for a new frame.
    const QString key = !visible   ? QStringLiteral("none")
                        : m_player ? QStringLiteral("engine:%1@%2").arg(m_graphRev).arg(m_playhead)
                                   : path + QLatin1Char('@') + QString::number(timeUs);
    if (key == m_lastFrameKey)
        return;
    m_lastFrameKey = key;
    if (!visible) {
        emit programFrameReady({});
        return;
    }
    m_frameBusy = true;
    QPointer<ProjectController> self(this);
    TimelinePlayer *player = m_player;
    const qint64 frame = m_playhead;
    (void)QtConcurrent::run(&m_pool, [self, player, path, timeUs, frame] {
        // The engine composes all tracks exactly like playback (and later
        // export); without it, decode the single top clip directly.
        QImage img = player ? player->renderFrame(frame) : QImage();
        if (img.isNull() && !path.isEmpty())
            img = MediaProbe::frameAt(path, timeUs, kProgramW, kProgramH);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, img] {
            if (!self)
                return;
            self->m_frameBusy = false;
            if (!self->isPlaying())
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
    return MediaCache::pathFor(MediaCache::Kind::Thumbnail, mediaPath);
}

void ProjectController::startProbe(const MediaItem &item)
{
    const QUuid id = item.id;
    const QString path = item.path;
    const quint64 gen = m_generation;
    m_pendingProbe.insert(id);
    m_waves.remove(id); // the file may have changed (relink)

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
                self->startWaveform(*m);
                self->checkProxy(*m);
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

QString ProjectController::waveCachePath(const QString &mediaPath)
{
    return MediaCache::pathFor(MediaCache::Kind::Waveform, mediaPath);
}

void ProjectController::startWaveform(const MediaItem &item)
{
    if (!item.info.hasAudio() || m_waves.contains(item.id))
        return;
    const QUuid id = item.id;
    const QString path = item.path;
    const quint64 gen = m_generation;
    QPointer<ProjectController> self(this);
    (void)QtConcurrent::run(&m_pool, [self, id, path, gen] {
        const QString cache = waveCachePath(path);
        QByteArray peaks;
        if (!Waveform::load(cache, peaks)) {
            QString err;
            if (!Waveform::compute(path, peaks, &err)) {
                qWarning("waveform of %s: %s", qPrintable(path), qPrintable(err));
                return;
            }
            Waveform::save(cache, peaks);
        }
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, id, path, gen, peaks] {
            if (!self || gen != self->m_generation)
                return;
            const MediaItem *m = self->m_project.item(id);
            if (!m || m->path != path)
                return;
            self->m_waves.insert(id, peaks);
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
