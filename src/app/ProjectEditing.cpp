// Roll, slip and slide, link/unlink and copy/paste of clips (phase 6, part 2).
// Part of ProjectController.

#include "app/ProjectController.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bc {

bool ProjectController::rollClip(const QUuid &clipId, bool head, qint64 frame)
{
    return editTimeline(tr("Rolling edit"), [&](Timeline &t) {
        return t.roll(clipId, head, frame, [this](const QUuid &m) { return mediaFrames(m); });
    });
}

bool ProjectController::slipClip(const QUuid &clipId, qint64 deltaFrames)
{
    return editTimeline(tr("Slip clip"), [&](Timeline &t) {
        return t.slip(clipId, deltaFrames, [this](const QUuid &m) { return mediaFrames(m); });
    });
}

bool ProjectController::slideClip(const QUuid &clipId, qint64 deltaFrames)
{
    return editTimeline(tr("Slide clip"), [&](Timeline &t) {
        return t.slide(clipId, deltaFrames, [this](const QUuid &m) { return mediaFrames(m); });
    });
}

bool ProjectController::toggleLink(const QList<QUuid> &clipIds)
{
    const Timeline &tl = timeline();
    const bool anyLinked = std::any_of(clipIds.begin(), clipIds.end(), [&](const QUuid &id) { return tl.isLinked(id); });
    if (anyLinked)
        return editTimeline(tr("Unlink"), [&](Timeline &t) { return t.unlink(clipIds); });
    return editTimeline(tr("Link"), [&](Timeline &t) { return t.link(clipIds); });
}

bool ProjectController::copyClips(const QList<QUuid> &clipIds)
{
    const Timeline &tl = timeline();
    QList<CopiedClip> copied;
    qint64 first = std::numeric_limits<qint64>::max();
    for (const QUuid &id : clipIds) {
        const Timeline::ClipRef r = tl.find(id);
        if (!r.isValid())
            continue;
        const TimelineTrack &track = tl.tracks[r.track];
        int n = 0;
        for (int i = 0; i < r.track; ++i)
            if (tl.tracks[i].kind == track.kind)
                ++n;
        copied << CopiedClip{track.clips[r.index], track.kind, n};
        first = std::min(first, track.clips[r.index].start);
    }
    if (copied.isEmpty())
        return false;
    for (CopiedClip &c : copied)
        c.clip.start -= first;
    m_clipboard = copied;
    emit clipboardChanged();
    return true;
}

bool ProjectController::cutClips(const QList<QUuid> &clipIds)
{
    if (!copyClips(clipIds))
        return false;
    return editTimeline(tr("Cut"), [&](Timeline &t) { return t.remove(clipIds, false); });
}

QList<QUuid> ProjectController::pasteClips(bool insert)
{
    QList<QUuid> created;
    if (m_clipboard.isEmpty())
        return created;
    const qint64 at = m_playhead;
    qint64 span = 0;
    for (const CopiedClip &c : m_clipboard)
        span = std::max(span, c.clip.end());
    const bool ok = editTimeline(insert ? tr("Paste insert") : tr("Paste"), [&](Timeline &t) {
        if (insert)
            t.insertSpace(at, span);
        QHash<QUuid, QUuid> links; // copied link -> new link
        for (const CopiedClip &c : m_clipboard) {
            if (!c.clip.isTitle() && !m_project.item(c.clip.mediaId))
                continue; // its media was removed from the project
            while (t.count(c.kind) <= c.n)
                t.addTrack(c.kind);
            const int track = t.indexOf(c.kind, c.n);
            if (t.tracks[track].locked)
                continue;
            TimelineClip clip = c.clip;
            clip.id = QUuid::createUuid();
            clip.start += at;
            if (!clip.linkId.isNull()) {
                if (!links.contains(clip.linkId))
                    links.insert(clip.linkId, QUuid::createUuid());
                clip.linkId = links.value(clip.linkId);
            }
            t.clearRange(track, clip.start, clip.end());
            t.insertSorted(track, clip);
            created << clip.id;
        }
        return !created.isEmpty();
    });
    if (ok)
        setPlayhead(at + span);
    return ok ? created : QList<QUuid>();
}

} // namespace bc

// ---------------------------------------------------------------------------
// Nested sequences (phase 6, part 3)

namespace bc {

QUuid ProjectController::nestClips(const QList<QUuid> &clipIds)
{
    const Timeline &tl = timeline();
    const QList<QUuid> sel = tl.withLinked(clipIds);
    if (sel.isEmpty())
        return {};
    qint64 first = std::numeric_limits<qint64>::max(), last = 0;
    int videoN = -1, audioN = -1;
    for (const QUuid &id : sel) {
        const Timeline::ClipRef r = tl.find(id);
        const TimelineTrack &track = tl.tracks[r.track];
        if (track.locked)
            return {};
        const TimelineClip &c = track.clips[r.index];
        first = std::min(first, c.start);
        last = std::max(last, c.end());
        int n = 0;
        for (int i = 0; i < r.track; ++i)
            if (tl.tracks[i].kind == track.kind)
                ++n;
        int &lowest = track.kind == Timeline::Kind::Video ? videoN : audioN;
        lowest = lowest < 0 ? n : std::min(lowest, n);
    }

    NestedSequence nest;
    nest.id = QUuid::createUuid();
    const bool ok = editProject(tr("Nest"), [&](EditState &s) {
        Timeline *cur = &s.main;
        for (NestedSequence &n : s.nested)
            if (n.id == s.open)
                cur = &n.timeline;
        nest.name = tr("Nested sequence %1").arg(s.nested.size() + 1);
        // Same tracks as here, so every clip keeps its track.
        for (const TimelineTrack &t : cur->tracks) {
            TimelineTrack copy;
            copy.id = QUuid::createUuid();
            copy.kind = t.kind;
            nest.timeline.tracks << copy;
        }
        for (const QUuid &id : sel) {
            const Timeline::ClipRef r = cur->find(id);
            TimelineClip c = cur->tracks[r.track].clips[r.index];
            c.start -= first;
            nest.timeline.insertSorted(r.track, c);
        }
        cur->remove(sel, false);
        const QUuid link = (videoN >= 0 && audioN >= 0) ? QUuid::createUuid() : QUuid();
        auto put = [&](Timeline::Kind kind, int n) {
            const int track = cur->indexOf(kind, n);
            TimelineClip c;
            c.id = QUuid::createUuid();
            c.nestId = nest.id;
            c.linkId = link;
            c.start = first;
            c.length = last - first;
            cur->clearRange(track, c.start, c.end());
            cur->insertSorted(track, c);
        };
        if (videoN >= 0)
            put(Timeline::Kind::Video, videoN);
        if (audioN >= 0)
            put(Timeline::Kind::Audio, audioN);
        s.nested << nest;
        return true;
    });
    return ok ? nest.id : QUuid();
}

void ProjectController::switchSequence(const QUuid &nestId)
{
    m_nestPlayheads.insert(m_openNest, m_playhead);
    m_openNest = nestId;
    m_playhead = m_nestPlayheads.value(nestId, 0);
    emit playheadChanged(m_playhead);
    emit openSequenceChanged();
}

bool ProjectController::openSequence(const QUuid &nestId)
{
    if (!nestId.isNull() && !m_project.nest(nestId))
        return false;
    if (nestId == m_openNest)
        return true;
    pause();
    switchSequence(nestId);
    syncPlayer();
    m_lastFrameKey.clear();
    emit timelineChanged();
    requestProgramFrame();
    return true;
}

QString ProjectController::sequenceName(const QUuid &nestId) const
{
    if (const NestedSequence *n = m_project.nest(nestId))
        return n->name;
    return tr("Main sequence");
}

bool ProjectController::renameSequence(const QUuid &nestId, const QString &name)
{
    const QString clean = name.trimmed();
    if (clean.isEmpty() || !m_project.nest(nestId))
        return false;
    return editProject(tr("Rename sequence"), [&](EditState &s) {
        for (NestedSequence &n : s.nested)
            if (n.id == nestId && n.name != clean) {
                n.name = clean;
                return true;
            }
        return false;
    });
}

// ---------------------------------------------------------------------------
// Motion (phase 7a)

namespace {

// Source frame of a timeline frame inside the clip (clamped to the clip).
qint64 sourceFrame(const TimelineClip &c, qint64 frame)
{
    return c.in + c.src(std::clamp<qint64>(frame - c.start, 0, std::max<qint64>(0, c.length - 1)));
}

TimelineClip *videoClip(Timeline &t, const QUuid &id)
{
    const Timeline::ClipRef r = t.find(id);
    if (!r.isValid() || t.tracks[r.track].locked || t.tracks[r.track].kind != Timeline::Kind::Video)
        return nullptr;
    return &t.tracks[r.track].clips[r.index];
}

} // namespace

ClipTransform ProjectController::clipTransformAt(const QUuid &clipId, qint64 frame) const
{
    const TimelineClip *c = timeline().clip(clipId);
    return c ? c->motion.at(sourceFrame(*c, frame)) : ClipTransform();
}

bool ProjectController::setClipTransform(const QUuid &clipId, const ClipTransform &value, qint64 frame)
{
    return editTimeline(tr("Motion"), [&](Timeline &t) {
        TimelineClip *c = videoClip(t, clipId);
        if (!c)
            return false;
        c->motion.set(sourceFrame(*c, frame), value);
        return true;
    }, QStringLiteral("motion:") + clipId.toString());
}

bool ProjectController::setClipAnimated(const QUuid &clipId, bool on, qint64 frame)
{
    return editTimeline(on ? tr("Animate motion") : tr("Stop animating motion"), [&](Timeline &t) {
        TimelineClip *c = videoClip(t, clipId);
        if (!c || c->motion.animated() == on)
            return false;
        c->motion.setAnimated(on, sourceFrame(*c, frame));
        return true;
    });
}

bool ProjectController::hasClipKey(const QUuid &clipId, qint64 frame) const
{
    const TimelineClip *c = timeline().clip(clipId);
    return c && c->covers(frame) && c->motion.hasKey(sourceFrame(*c, frame));
}

bool ProjectController::toggleClipKey(const QUuid &clipId, qint64 frame)
{
    return editTimeline(tr("Keyframe"), [&](Timeline &t) {
        TimelineClip *c = videoClip(t, clipId);
        if (!c || !c->covers(frame))
            return false;
        const qint64 f = sourceFrame(*c, frame);
        if (c->motion.hasKey(f))
            return c->motion.removeKey(f);
        if (!c->motion.animated()) {
            c->motion.setAnimated(true, f);
            return true;
        }
        c->motion.set(f, c->motion.at(f));
        return true;
    });
}

bool ProjectController::resetClipMotion(const QUuid &clipId)
{
    return editTimeline(tr("Reset motion"), [&](Timeline &t) {
        TimelineClip *c = videoClip(t, clipId);
        if (!c || c->motion == ClipMotion())
            return false;
        c->motion = ClipMotion();
        return true;
    });
}

qint64 ProjectController::clipKeyNear(const QUuid &clipId, qint64 frame, bool next) const
{
    const TimelineClip *c = timeline().clip(clipId);
    if (!c)
        return -1;
    const qint64 f = c->in + c->src(frame - c->start); // may lie outside the clip
    const qint64 k = next ? c->motion.nextKey(f) : c->motion.prevKey(f);
    if (k < 0)
        return -1;
    const qint64 tf = c->start + qint64(std::llround((k - c->in) / c->speed));
    return c->covers(tf) ? tf : -1;
}

// ---------------------------------------------------------------------------
// Speed (phase 7b)

bool ProjectController::canChangeSpeed(const QUuid &clipId) const
{
    const TimelineClip *c = timeline().clip(clipId);
    if (!c || c->mediaId.isNull())
        return false;
    const MediaItem *m = m_project.item(c->mediaId);
    return m && m->info.kind != MediaInfo::Kind::Image && mediaFrames(c->mediaId) > 0;
}

bool ProjectController::setClipSpeed(const QUuid &clipId, double speed, qint64 length, bool ripple)
{
    if (!canChangeSpeed(clipId))
        return false;
    speed = std::clamp(speed, 0.1, 10.0);
    speed = std::round(speed * 1000) / 1000; // the engine keeps thousandths
    return editTimeline(tr("Speed"), [&](Timeline &t) {
        const QList<QUuid> group = t.withLinked({clipId});
        QList<Timeline::ClipRef> refs;
        qint64 newLen = length;
        qint64 room = std::numeric_limits<qint64>::max();
        qint64 oldEnd = -1;
        for (const QUuid &id : group) {
            const Timeline::ClipRef r = t.find(id);
            if (!r.isValid() || t.tracks[r.track].locked)
                return false;
            const TimelineClip &c = t.tracks[r.track].clips[r.index];
            if (c.mediaId.isNull())
                return false;
            if (newLen <= 0) // keep the same part of the source
                newLen = std::max<qint64>(1, qint64(std::llround(c.src(c.length) / speed)));
            // The source must last: in + length * speed <= media length.
            const qint64 media = mediaFrames(c.mediaId);
            if (media > 0)
                newLen = std::min<qint64>(newLen, qint64(std::floor((media - c.in) / speed + 1e-9)));
            const auto &clips = t.tracks[r.track].clips;
            const qint64 next = r.index + 1 < clips.size() ? clips[r.index + 1].start : std::numeric_limits<qint64>::max();
            room = std::min(room, next - c.start);
            oldEnd = std::max(oldEnd, c.end());
            refs << r;
        }
        if (refs.isEmpty() || newLen < 1)
            return false;
        const TimelineClip &first = t.tracks[refs.first().track].clips[refs.first().index];
        const qint64 oldLen = first.length;
        const qint64 delta = newLen - oldLen;
        if (!ripple && delta > 0)
            newLen = std::min(newLen, room); // later clips stay; the clip stops at the next one
        if (newLen == oldLen && first.speed == speed)
            return false;
        QList<int> members;
        for (const Timeline::ClipRef &r : refs)
            members << r.track;
        if (ripple && delta != 0) {
            // Later clips on the same tracks move with the new end.
            for (int tr : members)
                for (TimelineClip &c : t.tracks[tr].clips)
                    if (c.start >= oldEnd)
                        c.start += newLen - oldLen;
        }
        for (const Timeline::ClipRef &r : refs) {
            TimelineClip &c = t.tracks[r.track].clips[r.index];
            c.speed = speed;
            c.length = newLen;
            c.fadeIn = std::min(c.fadeIn, c.length);
            c.fadeOut = std::min(c.fadeOut, c.length - c.fadeIn);
        }
        return true;
    });
}

// ---------------------------------------------------------------------------
// Transitions (phase 8a)

ClipTransition ProjectController::clipTransition(const QUuid &clipId, bool head) const
{
    return timeline().transitionAt(clipId, head);
}

bool ProjectController::setClipTransition(const QUuid &clipId, bool head, const ClipTransition &t)
{
    return editTimeline(tr("Transition"), [&](Timeline &tl) { return tl.setTransition(clipId, head, t); },
                        QStringLiteral("transition:") + clipId.toString() + (head ? QStringLiteral(":h") : QStringLiteral(":t")));
}

int ProjectController::addDefaultTransitions(const QList<QUuid> &clipIds, qint64 frame)
{
    const double fps = m_project.sequence.fps() > 0 ? m_project.sequence.fps() : 25.0;
    const qint64 length = std::max<qint64>(2, qint64(std::llround(fps)));
    int added = 0;
    editTimeline(tr("Add transition"), [&](Timeline &t) {
        for (const QUuid &id : t.withLinked(clipIds)) {
            const TimelineClip *c = t.clip(id);
            if (!c)
                continue;
            const bool head = std::llabs(frame - c->start) <= std::llabs(frame - c->end());
            if (!t.transitionAt(id, head).isNone())
                continue;
            if (t.setTransition(id, head, ClipTransition{ClipTransition::Kind::Dissolve, length}))
                ++added;
        }
        return added > 0;
    });
    return added;
}

ClipEffects ProjectController::clipEffects(const QUuid &clipId) const
{
    const TimelineClip *c = timeline().clip(clipId);
    return c && timeline().kindOf(clipId) == Timeline::Kind::Video ? c->effects : ClipEffects();
}

int ProjectController::addClipEffect(const QUuid &clipId, ClipEffect::Kind kind)
{
    int index = -1;
    editTimeline(tr("Add effect"), [&](Timeline &t) {
        TimelineClip *c = videoClip(t, clipId);
        if (!c)
            return false;
        c->effects << ClipEffect::make(kind);
        index = int(c->effects.size()) - 1;
        return true;
    });
    return index;
}

bool ProjectController::removeClipEffect(const QUuid &clipId, int index)
{
    return editTimeline(tr("Remove effect"), [&](Timeline &t) {
        TimelineClip *c = videoClip(t, clipId);
        if (!c || index < 0 || index >= c->effects.size())
            return false;
        c->effects.removeAt(index);
        return true;
    });
}

bool ProjectController::moveClipEffect(const QUuid &clipId, int index, int delta)
{
    return editTimeline(tr("Move effect"), [&](Timeline &t) {
        TimelineClip *c = videoClip(t, clipId);
        const int to = index + delta;
        if (!c || index < 0 || index >= c->effects.size() || to < 0 || to >= c->effects.size() || to == index)
            return false;
        c->effects.move(index, to);
        return true;
    });
}

bool ProjectController::setClipEffectEnabled(const QUuid &clipId, int index, bool on)
{
    return editTimeline(on ? tr("Turn effect on") : tr("Turn effect off"), [&](Timeline &t) {
        TimelineClip *c = videoClip(t, clipId);
        if (!c || index < 0 || index >= c->effects.size() || c->effects[index].enabled == on)
            return false;
        c->effects[index].enabled = on;
        return true;
    });
}

bool ProjectController::setClipEffectValue(const QUuid &clipId, int index, const QString &key, double value)
{
    return editTimeline(tr("Effect"), [&](Timeline &t) {
        TimelineClip *c = videoClip(t, clipId);
        if (!c || index < 0 || index >= c->effects.size())
            return false;
        return c->effects[index].setValue(key, value);
    }, QStringLiteral("effect:%1:%2:%3").arg(clipId.toString()).arg(index).arg(key));
}

} // namespace bc
