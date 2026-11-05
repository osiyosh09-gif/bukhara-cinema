// Automatic multicamera edit (phase 16a). Part of ProjectController.
//
// The sound of every camera of a multicamera sequence is read once (an RMS
// envelope, 100 values a second, cached next to the waveforms); core/AutoMulticam
// decides which camera is shown when; the clip is then cut like the live
// switching of phase 12b does it, all in one undo step.

#include "app/ProjectController.h"
#include "core/AudioSync.h"
#include "core/MediaCache.h"

#include <QPointer>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace bc {

namespace {

// The cameras' sound (audio track n) and picture (video track n) of a
// multicamera sequence, on its own time axis at AutoMulticam::kRate.
void cameraTracks(const NestedSequence &nest, QList<const TimelineTrack *> *video, QList<const TimelineTrack *> *audio)
{
    for (const TimelineTrack &t : nest.timeline.tracks)
        (t.kind == Timeline::Kind::Video ? video : audio)->append(&t);
}

} // namespace

QList<QUuid> ProjectController::autoMulticamClips(const QList<QUuid> &selection) const
{
    QList<QUuid> out;
    const Timeline &tl = timeline();
    for (const QUuid &id : selection) {
        const TimelineClip *c = tl.clip(id);
        if (c && c->isNested() && isMulticam(c->nestId) && tl.kindOf(id) == Timeline::Kind::Video && !out.contains(id))
            out << id;
    }
    if (out.isEmpty()) {
        const QUuid at = multicamClipAt(m_playhead, Timeline::Kind::Video);
        if (!at.isNull())
            out << at;
    }
    return out;
}

QStringList ProjectController::multicamClipAngles(const QUuid &clipId) const
{
    const TimelineClip *c = timeline().clip(clipId);
    return c ? multicamAngles(c->nestId, Timeline::Kind::Video) : QStringList();
}

bool ProjectController::multicamSoundKnown(const QList<QUuid> &clipIds) const
{
    for (const QUuid &id : clipIds) {
        const TimelineClip *c = timeline().clip(id);
        const NestedSequence *nest = c ? m_project.nest(c->nestId) : nullptr;
        if (!nest)
            continue;
        for (const TimelineTrack &t : nest->timeline.tracks)
            if (t.kind == Timeline::Kind::Audio)
                for (const TimelineClip &a : t.clips)
                    if (!a.mediaId.isNull() && !m_envelopes.contains(a.mediaId) && !m_envelopesFailed.contains(a.mediaId))
                        return false;
    }
    return true;
}

void ProjectController::analyzeMulticamSound(const QList<QUuid> &clipIds)
{
    if (m_soundRunning)
        return;
    QList<QPair<QUuid, QString>> todo;
    for (const QUuid &id : clipIds) {
        const TimelineClip *c = timeline().clip(id);
        const NestedSequence *nest = c ? m_project.nest(c->nestId) : nullptr;
        if (!nest)
            continue;
        for (const TimelineTrack &t : nest->timeline.tracks)
            if (t.kind == Timeline::Kind::Audio)
                for (const TimelineClip &a : t.clips) {
                    const MediaItem *m = m_project.item(a.mediaId);
                    if (!m || m_envelopes.contains(m->id) || m_envelopesFailed.contains(m->id))
                        continue;
                    bool listed = false;
                    for (const auto &p : todo)
                        listed |= p.first == m->id;
                    if (!listed)
                        todo << qMakePair(m->id, m->path);
                }
    }
    if (todo.isEmpty()) {
        emit multicamSoundReady(true);
        return;
    }
    m_soundRunning = true;
    m_soundCancel = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = m_soundCancel;
    const quint64 gen = m_generation;
    QPointer<ProjectController> self(this);
    emit multicamSoundProgress(0, int(todo.size()));
    (void)QtConcurrent::run(&m_soundPool, [self, todo, gen, cancel] {
        QHash<QUuid, std::shared_ptr<const std::vector<float>>> done;
        QSet<QUuid> failed;
        for (int i = 0; i < todo.size() && !cancel->load(); ++i) {
            const QString cache = MediaCache::pathFor(MediaCache::Kind::Envelope, todo[i].second);
            auto env = std::make_shared<std::vector<float>>();
            bool ok = AutoMulticam::load(cache, *env);
            if (!ok) {
                QString err;
                ok = AudioSync::envelope(todo[i].second, *env, &err, cancel.get());
                if (ok)
                    AutoMulticam::save(cache, *env);
                else if (!cancel->load())
                    qWarning("sound of %s: %s", qPrintable(todo[i].second), qPrintable(err));
            }
            if (ok)
                done.insert(todo[i].first, env);
            else if (!cancel->load())
                failed.insert(todo[i].first);
            const int count = i + 1, total = int(todo.size());
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, gen, count, total] {
                if (self && gen == self->m_generation)
                    emit self->multicamSoundProgress(count, total);
            }, Qt::QueuedConnection);
        }
        const bool cancelled = cancel->load();
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, gen, done, failed, cancelled] {
            if (!self)
                return;
            self->m_soundRunning = false;
            if (gen != self->m_generation)
                return;
            for (auto it = done.cbegin(); it != done.cend(); ++it)
                self->m_envelopes.insert(it.key(), it.value());
            for (const QUuid &id : failed)
                self->m_envelopesFailed.insert(id);
            emit self->multicamSoundReady(!cancelled);
        }, Qt::QueuedConnection);
    });
}

AutoMulticam::Plan ProjectController::autoMulticamPlan(const QUuid &clipId, const AutoMulticam::Settings &settings) const
{
    AutoMulticam::Plan none;
    const TimelineClip *c = timeline().clip(clipId);
    const NestedSequence *nest = c ? m_project.nest(c->nestId) : nullptr;
    const double fps = m_project.sequence.fps();
    if (!nest || !nest->multicam || fps <= 0 || c->length <= 0)
        return none;
    QList<const TimelineTrack *> video, audio;
    cameraTracks(*nest, &video, &audio);
    const int n = int(video.size());
    if (n < 1)
        return none;
    const qint64 steps = qint64(std::ceil(double(nest->timeline.duration()) / fps * AutoMulticam::kRate)) + 1;
    QList<std::vector<float>> levels;
    QList<std::vector<bool>> picture;
    for (int a = 0; a < n; ++a) {
        std::vector<bool> has(size_t(steps), false);
        for (const TimelineClip &v : video[a]->clips) {
            const qint64 s0 = qint64(std::floor(double(v.start) / fps * AutoMulticam::kRate));
            const qint64 s1 = std::min(steps, qint64(std::ceil(double(v.end()) / fps * AutoMulticam::kRate)));
            for (qint64 k = std::max<qint64>(0, s0); k < s1; ++k)
                has[size_t(k)] = true;
        }
        picture << has;
        std::vector<float> level;
        if (a < audio.size()) {
            for (const TimelineClip &s : audio[a]->clips) {
                const auto it = m_envelopes.constFind(s.mediaId);
                if (it == m_envelopes.constEnd())
                    continue;
                const std::vector<float> &env = **it;
                if (level.empty())
                    level.assign(size_t(steps), 0.0f);
                const qint64 s0 = std::max<qint64>(0, qint64(std::floor(double(s.start) / fps * AutoMulticam::kRate)));
                const qint64 s1 = std::min(steps, qint64(std::ceil(double(s.end()) / fps * AutoMulticam::kRate)));
                for (qint64 k = s0; k < s1; ++k) {
                    // Time in the file: after the clip's start, from its In point, at its speed.
                    const double t = (double(k) / AutoMulticam::kRate - double(s.start) / fps) * s.speed + double(s.in) / fps;
                    const qint64 e = qint64(std::llround(t * AutoMulticam::kRate));
                    if (e >= 0 && e < qint64(env.size()))
                        level[size_t(k)] = env[size_t(e)];
                }
            }
        }
        levels << level;
    }
    // The part of the multicamera sequence the clip shows, then back to sequence frames.
    const qint64 from = c->in;
    const qint64 to = c->in + c->src(c->length);
    AutoMulticam::Plan plan = AutoMulticam::plan(levels, picture, fps, from, to, settings);
    if (plan.cuts.isEmpty())
        return plan;
    for (AutoMulticam::Cut &cut : plan.cuts) {
        const double rel = double(cut.frame - c->in) / (c->speed > 0 ? c->speed : 1.0);
        cut.frame = std::clamp(c->start + qint64(std::llround(rel)), c->start, c->end() - 1);
    }
    plan.cuts.first().frame = c->start;
    // Two cuts in one frame after the speed change: keep the later.
    QList<AutoMulticam::Cut> clean;
    for (const AutoMulticam::Cut &cut : plan.cuts) {
        if (!clean.isEmpty() && clean.last().frame == cut.frame)
            clean.last() = cut;
        else if (clean.isEmpty() || clean.last().angle != cut.angle)
            clean << cut;
    }
    plan.cuts = clean;
    return plan;
}

int ProjectController::applyAutoMulticam(const QList<QUuid> &clipIds, const AutoMulticam::Settings &settings)
{
    // Plans first: they read this timeline, which the edit then changes.
    QList<QPair<QUuid, QList<AutoMulticam::Cut>>> plans;
    for (const QUuid &id : clipIds) {
        const AutoMulticam::Plan p = autoMulticamPlan(id, settings);
        if (!p.cuts.isEmpty())
            plans << qMakePair(id, p.cuts);
    }
    if (plans.isEmpty())
        return 0;
    int made = 0;
    editTimeline(tr("Automatic multicamera edit"), [&](Timeline &t) {
        made = 0;
        bool changed = false;
        for (const auto &plan : plans) {
            const Timeline::ClipRef r = t.find(plan.first);
            if (r.track < 0 || t.tracks[r.track].locked)
                continue;
            const TimelineClip original = t.tracks[r.track].clips[r.index];
            // From the last cut back: the clip keeps its id as the first part.
            for (int i = int(plan.second.size()) - 1; i >= 1; --i) {
                const qint64 at = plan.second[i].frame;
                if (at > original.start && at < original.end()) {
                    t.splitAt(at, {plan.first}); // with its linked sound, which keeps its camera
                    ++made;
                }
            }
            for (TimelineClip &part : t.tracks[r.track].clips) {
                if (part.nestId != original.nestId || part.start < original.start || part.start >= original.end())
                    continue;
                int angle = plan.second.first().angle;
                for (const AutoMulticam::Cut &cut : plan.second)
                    if (cut.frame <= part.start)
                        angle = cut.angle;
                if (part.angle != angle) {
                    part.angle = angle;
                    changed = true;
                }
            }
        }
        return made > 0 || changed;
    });
    return made;
}

} // namespace bc
