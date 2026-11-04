// Loudness measuring and clip loudness normalisation (phase 10, part 4).
// Part of ProjectController.

#include "app/ProjectController.h"
#include "core/MediaCache.h"

#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cmath>

namespace bc {

QList<QUuid> ProjectController::loudnessClips(const QList<QUuid> &clipIds) const
{
    const Timeline &t = timeline();
    QList<QUuid> out;
    for (const QUuid &id : t.withLinked(clipIds)) {
        const TimelineClip *c = t.clip(id);
        if (c && !c->mediaId.isNull() && t.kindOf(id) == Timeline::Kind::Audio && !out.contains(id))
            out << id;
    }
    return out;
}

void ProjectController::analyzeLoudness(const QList<QUuid> &clipIds)
{
    for (const QUuid &id : loudnessClips(clipIds)) {
        const TimelineClip *c = timeline().clip(id);
        const MediaItem *m = c ? m_project.item(c->mediaId) : nullptr;
        if (!m || m_loudness.contains(m->id) || m_loudnessPending.contains(m->id))
            continue;
        const QUuid mediaId = m->id;
        const QString path = m->path;
        const quint64 gen = m_generation;
        m_loudnessPending.insert(mediaId);
        QPointer<ProjectController> self(this);
        (void)QtConcurrent::run(&m_pool, [self, mediaId, path, gen] {
            const QString cache = MediaCache::pathFor(MediaCache::Kind::Loudness, path);
            auto profile = std::make_shared<Loudness::Profile>();
            bool ok = Loudness::load(cache, *profile);
            if (!ok) {
                QString err;
                ok = Loudness::compute(path, *profile, &err);
                if (ok)
                    Loudness::save(cache, *profile);
                else
                    qWarning("loudness of %s: %s", qPrintable(path), qPrintable(err));
            }
            if (!self)
                return;
            QMetaObject::invokeMethod(self.data(), [self, mediaId, path, gen, ok, profile] {
                if (!self || gen != self->m_generation)
                    return;
                self->m_loudnessPending.remove(mediaId);
                const MediaItem *m = self->m_project.item(mediaId);
                if (!m || m->path != path)
                    return;
                if (ok)
                    self->m_loudness.insert(mediaId, profile);
                emit self->loudnessAnalyzed(mediaId, ok);
            }, Qt::QueuedConnection);
        });
    }
}

bool ProjectController::loudnessKnown(const QUuid &clipId) const
{
    const TimelineClip *c = timeline().clip(clipId);
    return c && m_loudness.contains(c->mediaId);
}

Loudness::Result ProjectController::clipLoudness(const QUuid &clipId) const
{
    return clipsLoudness({clipId});
}

Loudness::Result ProjectController::clipsLoudness(const QList<QUuid> &clipIds) const
{
    const double fps = m_project.sequence.fps() > 0 ? m_project.sequence.fps() : 25.0;
    std::vector<double> blocks;
    double peak = -120;
    for (const QUuid &id : clipIds) {
        const TimelineClip *c = timeline().clip(id);
        if (!c)
            continue;
        const auto it = m_loudness.constFind(c->mediaId);
        if (it == m_loudness.constEnd())
            continue;
        // The clip's part of the source (speed changes how much of it is used).
        const double from = c->in / fps, to = (c->in + c->src(c->length)) / fps;
        const std::vector<double> b = Loudness::blocks(**it, from, to);
        blocks.insert(blocks.end(), b.begin(), b.end());
        peak = std::max(peak, Loudness::peakDb(**it, from, to));
    }
    Loudness::Result r;
    r.lufs = Loudness::integrate(blocks);
    r.peakDb = peak;
    r.valid = !blocks.empty() && r.lufs > Loudness::kSilence;
    return r;
}

QHash<QUuid, double> ProjectController::loudnessGains(const QList<QUuid> &clipIds, double targetLufs,
                                                       double ceilingDb, bool together) const
{
    QHash<QUuid, double> out;
    auto gainFor = [&](const Loudness::Result &r) {
        const double g = std::min(targetLufs - r.lufs, ceilingDb - r.peakDb);
        return std::clamp(std::round(g * 10.0) / 10.0, -60.0, 12.0);
    };
    QList<QUuid> known;
    for (const QUuid &id : clipIds)
        if (loudnessKnown(id))
            known << id;
    if (together) {
        const Loudness::Result r = clipsLoudness(known);
        if (r.valid)
            for (const QUuid &id : known)
                out.insert(id, gainFor(r));
        return out;
    }
    for (const QUuid &id : known) {
        const Loudness::Result r = clipLoudness(id);
        if (r.valid)
            out.insert(id, gainFor(r));
    }
    return out;
}

int ProjectController::normalizeLoudness(const QList<QUuid> &clipIds, double targetLufs, double ceilingDb,
                                         bool together)
{
    const QHash<QUuid, double> gains = loudnessGains(loudnessClips(clipIds), targetLufs, ceilingDb, together);
    int changed = 0;
    editTimeline(tr("Normalize loudness"), [&](Timeline &t) {
        for (auto it = gains.constBegin(); it != gains.constEnd(); ++it) {
            const Timeline::ClipRef r = t.find(it.key());
            if (!r.isValid() || t.tracks[r.track].locked)
                continue;
            TimelineClip &c = t.tracks[r.track].clips[r.index];
            if (c.gainDb != it.value()) {
                c.gainDb = it.value();
                ++changed;
            }
        }
        return changed > 0;
    });
    return changed;
}

} // namespace bc
