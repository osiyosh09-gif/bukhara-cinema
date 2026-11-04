// Roll, slip and slide, link/unlink and copy/paste of clips (phase 6, part 2).
// Part of ProjectController.

#include "app/ProjectController.h"

#include <QHash>

#include <algorithm>
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

} // namespace bc
