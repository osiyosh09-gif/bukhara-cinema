// Scene change detection (phase 14, part 1). Part of ProjectController.
//
// The media of the selected video clips is analysed once (SceneDetect,
// cached next to the waveforms); the clips can then be split at every new
// shot, or the shots marked on the timeline.

#include "app/ProjectController.h"
#include "core/MediaCache.h"

#include <QPointer>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace bc {

QList<QUuid> ProjectController::sceneClips(const QList<QUuid> &clipIds) const
{
    const Timeline &t = timeline();
    QList<QUuid> out;
    for (const QUuid &id : t.withLinked(clipIds)) {
        const TimelineClip *c = t.clip(id);
        const MediaItem *m = c ? m_project.item(c->mediaId) : nullptr;
        const Timeline::ClipRef ref = t.find(id);
        if (!m || m->info.kind != MediaInfo::Kind::Video || t.kindOf(id) != Timeline::Kind::Video
            || !ref.isValid() || t.tracks[ref.track].locked || out.contains(id))
            continue;
        out << id;
    }
    return out;
}

void ProjectController::analyzeScenes(const QList<QUuid> &clipIds)
{
    for (const QUuid &id : sceneClips(clipIds)) {
        const QUuid media = timeline().clip(id)->mediaId;
        if (m_scenes.contains(media) || m_scenesFailed.contains(media) || m_scenesQueue.contains(media)
            || m_scenesRunning == media)
            continue;
        m_scenesQueue << media;
    }
    startNextScenes();
}

void ProjectController::startNextScenes()
{
    if (!m_scenesRunning.isNull() || m_scenesQueue.isEmpty())
        return;
    const QUuid media = m_scenesQueue.takeFirst();
    const MediaItem *m = m_project.item(media);
    if (!m) {
        startNextScenes();
        return;
    }
    m_scenesRunning = media;
    m_scenesCancel = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = m_scenesCancel;
    const QString path = m->path;
    const quint64 gen = m_generation;
    QPointer<ProjectController> self(this);
    emit sceneProgress(media, 0);
    (void)QtConcurrent::run(&m_scenePool, [self, media, path, gen, cancel] {
        const QString cache = MediaCache::pathFor(MediaCache::Kind::Scenes, path);
        auto profile = std::make_shared<SceneDetect::Profile>();
        bool ok = SceneDetect::load(cache, *profile);
        if (!ok) {
            QString err;
            ok = SceneDetect::compute(path, *profile, &err, [&](double f) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [self, media, gen, f] {
                    if (self && gen == self->m_generation && self->m_scenesRunning == media)
                        emit self->sceneProgress(media, f);
                }, Qt::QueuedConnection);
            }, cancel.get());
            if (ok)
                SceneDetect::save(cache, *profile);
            else if (!cancel->load())
                qWarning("scenes of %s: %s", qPrintable(path), qPrintable(err));
        }
        const bool cancelled = cancel->load();
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, media, gen, ok, cancelled, profile] {
            if (!self || gen != self->m_generation)
                return;
            self->m_scenesRunning = QUuid();
            if (ok)
                self->m_scenes.insert(media, profile);
            else if (!cancelled)
                self->m_scenesFailed.insert(media);
            emit self->scenesAnalyzed(media, ok);
            self->startNextScenes();
        }, Qt::QueuedConnection);
    });
}

void ProjectController::cancelScenes()
{
    m_scenesQueue.clear();
    if (m_scenesCancel)
        m_scenesCancel->store(true);
}

bool ProjectController::scenesKnown(const QList<QUuid> &clipIds) const
{
    for (const QUuid &id : sceneClips(clipIds)) {
        const QUuid media = timeline().clip(id)->mediaId;
        if (!m_scenes.contains(media) && !m_scenesFailed.contains(media))
            return false;
    }
    return true;
}

QList<qint64> ProjectController::sceneCuts(const QUuid &clipId, double sensitivity) const
{
    const TimelineClip *c = timeline().clip(clipId);
    if (!c)
        return {};
    const auto it = m_scenes.constFind(c->mediaId);
    if (it == m_scenes.constEnd())
        return {};
    const double fps = m_project.sequence.fps();
    if (fps <= 0)
        return {};
    QList<qint64> out;
    for (qint64 us : SceneDetect::cuts(**it, sensitivity)) {
        // Source frame of the cut (frames at the sequence rate, as `in`),
        // then its place on the timeline. Rounded down like the engine: when
        // the file's sound starts before its picture, a picture frame falls
        // between two positions and shows from the earlier one.
        const qint64 s = qint64(std::floor(us * fps / 1e6 + 1e-3));
        const qint64 f = c->start + qint64(std::llround((s - c->in) / c->speed));
        if (f > c->start && f < c->end() && (out.isEmpty() || out.last() != f))
            out << f;
    }
    return out;
}

int ProjectController::sceneCutCount(const QList<QUuid> &clipIds, double sensitivity) const
{
    int n = 0;
    for (const QUuid &id : sceneClips(clipIds))
        n += int(sceneCuts(id, sensitivity).size());
    return n;
}

int ProjectController::splitAtScenes(const QList<QUuid> &clipIds, double sensitivity)
{
    QList<QPair<QUuid, QList<qint64>>> work;
    int count = 0;
    for (const QUuid &id : sceneClips(clipIds)) {
        const QList<qint64> cuts = sceneCuts(id, sensitivity);
        if (!cuts.isEmpty())
            work.append({id, cuts});
        count += int(cuts.size());
    }
    if (count == 0)
        return 0;
    const bool changed = editTimeline(tr("Split at scene changes"), [&](Timeline &t) {
        bool any = false;
        for (const auto &w : work) {
            // From the last cut back: the clip keeps its id for the part
            // before each cut, so the next (earlier) cut still finds it.
            for (auto f = w.second.crbegin(); f != w.second.crend(); ++f)
                any |= !t.splitAt(*f, {w.first}).isEmpty();
        }
        return any;
    });
    return changed ? count : 0;
}

int ProjectController::markScenes(const QList<QUuid> &clipIds, double sensitivity)
{
    QList<qint64> frames;
    for (const QUuid &id : sceneClips(clipIds))
        for (qint64 f : sceneCuts(id, sensitivity))
            if (!frames.contains(f) && !timeline().markerAt(f))
                frames << f;
    if (frames.isEmpty())
        return 0;
    const bool changed = editTimeline(tr("Mark scene changes"), [&](Timeline &t) {
        for (qint64 f : frames)
            t.addMarker(f);
        return true;
    });
    return changed ? int(frames.size()) : 0;
}

} // namespace bc
