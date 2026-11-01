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
    const Timeline &tl = m_project.timeline;
    const bool anyLinked = std::any_of(clipIds.begin(), clipIds.end(), [&](const QUuid &id) { return tl.isLinked(id); });
    if (anyLinked)
        return editTimeline(tr("Unlink"), [&](Timeline &t) { return t.unlink(clipIds); });
    return editTimeline(tr("Link"), [&](Timeline &t) { return t.link(clipIds); });
}

bool ProjectController::copyClips(const QList<QUuid> &clipIds)
{
    const Timeline &tl = m_project.timeline;
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
