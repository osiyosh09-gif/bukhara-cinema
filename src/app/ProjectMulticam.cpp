// Multicamera sequences (phase 12, part 1). Part of ProjectController.
//
// A multicamera sequence is a nested sequence with `multicam` set: angle n
// is video track n and audio track n, the recordings lined up in time. A
// clip of it on another sequence shows one angle (TimelineClip::angle); the
// engine builds only that angle's track.

#include "app/ProjectController.h"

#include <algorithm>

namespace bc {

QUuid ProjectController::createMulticam(const QList<QUuid> &mediaIds, const QList<qint64> &starts, int audioAngle)
{
    const int n = int(mediaIds.size());
    if (n < 1 || starts.size() != mediaIds.size())
        return {};
    for (const QUuid &id : mediaIds) {
        const MediaItem *m = m_project.item(id);
        if (!m || m->info.kind == MediaInfo::Kind::Image || (!m->info.hasVideo() && !m->info.hasAudio()))
            return {};
    }
    const qint64 first = *std::min_element(starts.begin(), starts.end());

    NestedSequence nest;
    nest.id = QUuid::createUuid();
    nest.multicam = true;
    for (Timeline::Kind kind : {Timeline::Kind::Video, Timeline::Kind::Audio})
        for (int i = 0; i < n; ++i)
            nest.timeline.addTrack(kind);
    int firstVideo = -1, firstAudio = -1;
    for (int i = 0; i < n; ++i) {
        const MediaItem &m = *m_project.item(mediaIds[i]);
        TimelineClip c;
        c.mediaId = m.id;
        c.start = starts[i] - first;
        c.length = mediaLengthFrames(m, m_project.sequence);
        const QUuid link = (m.info.hasVideo() && m.info.hasAudio()) ? QUuid::createUuid() : QUuid();
        c.linkId = link;
        if (m.info.hasVideo()) {
            c.id = QUuid::createUuid();
            nest.timeline.insertSorted(nest.timeline.indexOf(Timeline::Kind::Video, i), c);
            if (firstVideo < 0)
                firstVideo = i;
        }
        if (m.info.hasAudio()) {
            c.id = QUuid::createUuid();
            nest.timeline.insertSorted(nest.timeline.indexOf(Timeline::Kind::Audio, i), c);
            if (firstAudio < 0)
                firstAudio = i;
        }
    }
    const qint64 length = nest.timeline.duration();
    if (length <= 0)
        return {};
    const bool soundOk = audioAngle >= 0 && audioAngle < n && m_project.item(mediaIds[audioAngle])->info.hasAudio();
    const int soundAngle = soundOk ? audioAngle : firstAudio;
    const qint64 at = m_playhead;

    const bool ok = editProject(tr("Create multicamera sequence"), [&](EditState &s) {
        int count = 1;
        for (const NestedSequence &other : s.nested)
            if (other.multicam)
                ++count;
        nest.name = tr("Multicam %1").arg(count);
        s.nested << nest;
        Timeline *cur = &s.main;
        for (NestedSequence &other : s.nested)
            if (other.id == s.open)
                cur = &other.timeline;
        const QUuid link = (firstVideo >= 0 && firstAudio >= 0) ? QUuid::createUuid() : QUuid();
        auto put = [&](Timeline::Kind kind, int angle) {
            const int track = cur->indexOf(kind, 0);
            if (angle < 0 || track < 0 || cur->tracks[track].locked)
                return;
            TimelineClip c;
            c.id = QUuid::createUuid();
            c.nestId = nest.id;
            c.linkId = link;
            c.angle = angle;
            c.start = at;
            c.length = length;
            cur->clearRange(track, c.start, c.end());
            cur->insertSorted(track, c);
        };
        put(Timeline::Kind::Video, firstVideo);
        put(Timeline::Kind::Audio, soundAngle);
        return true;
    });
    return ok ? nest.id : QUuid();
}

bool ProjectController::isMulticam(const QUuid &nestId) const
{
    const NestedSequence *n = m_project.nest(nestId);
    return n && n->multicam;
}

QStringList ProjectController::multicamAngles(const QUuid &nestId, Timeline::Kind kind) const
{
    QStringList names;
    const NestedSequence *n = m_project.nest(nestId);
    if (!n || !n->multicam)
        return names;
    for (const TimelineTrack &t : n->timeline.tracks) {
        if (t.kind != kind)
            continue;
        QString name;
        for (const TimelineClip &c : t.clips)
            if (const MediaItem *m = m_project.item(c.mediaId)) {
                name = m->name;
                break;
            }
        names << (name.isEmpty() ? tr("Angle %1").arg(names.size() + 1) : name);
    }
    return names;
}

bool ProjectController::setClipAngle(const QUuid &clipId, int angle)
{
    const Timeline &tl = timeline();
    const TimelineClip *c = tl.clip(clipId);
    if (!c || !isMulticam(c->nestId) || angle < 0
        || angle >= multicamAngles(c->nestId, tl.kindOf(clipId)).size())
        return false;
    return editTimeline(tr("Change angle"), [&](Timeline &t) {
        const Timeline::ClipRef r = t.find(clipId);
        if (r.track < 0 || t.tracks[r.track].locked)
            return false;
        TimelineClip &clip = t.tracks[r.track].clips[r.index];
        if (clip.angle == angle)
            return false;
        clip.angle = angle;
        return true;
    });
}

} // namespace bc
