#include "core/Timeline.h"

#include "core/Project.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bc {

namespace {

constexpr qint64 kInf = std::numeric_limits<qint64>::max() / 4;

QString idStr(const QUuid &id)
{
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

} // namespace

qint64 mediaLengthFrames(const MediaItem &item, const SequenceSettings &seq)
{
    if (item.info.kind == MediaInfo::Kind::Image)
        return -1;
    const double frames = double(item.info.durationUs) / 1e6 * seq.fps();
    return std::max<qint64>(1, qint64(std::llround(frames)));
}

qint64 defaultClipFrames(const MediaItem &item, const SequenceSettings &seq)
{
    if (item.info.kind == MediaInfo::Kind::Image)
        return std::max<qint64>(1, qint64(std::llround(5.0 * seq.fps())));
    return mediaLengthFrames(item, seq);
}

// ---------------------------------------------------------------------------

Timeline Timeline::makeDefault()
{
    Timeline t;
    for (int i = 0; i < 3; ++i)
        t.addTrack(Kind::Video);
    for (int i = 0; i < 3; ++i)
        t.addTrack(Kind::Audio);
    return t;
}

int Timeline::count(Kind kind) const
{
    return int(std::count_if(tracks.begin(), tracks.end(), [kind](const TimelineTrack &t) { return t.kind == kind; }));
}

int Timeline::indexOf(Kind kind, int n) const
{
    if (n < 0)
        return -1;
    for (int i = 0; i < tracks.size(); ++i)
        if (tracks[i].kind == kind && n-- == 0)
            return i;
    return -1;
}

QString Timeline::trackName(int track) const
{
    if (track < 0 || track >= tracks.size())
        return {};
    const Kind kind = tracks[track].kind;
    int n = 0;
    for (int i = 0; i <= track; ++i)
        if (tracks[i].kind == kind)
            ++n;
    return (kind == Kind::Video ? QStringLiteral("V") : QStringLiteral("A")) + QString::number(n);
}

bool Timeline::isEmpty() const
{
    for (const auto &t : tracks)
        if (!t.clips.isEmpty())
            return false;
    return true;
}

qint64 Timeline::duration() const
{
    qint64 d = 0;
    for (const auto &t : tracks)
        if (!t.clips.isEmpty())
            d = std::max(d, t.clips.last().end());
    return d;
}

Timeline::ClipRef Timeline::find(const QUuid &clipId) const
{
    for (int t = 0; t < tracks.size(); ++t)
        for (int i = 0; i < tracks[t].clips.size(); ++i)
            if (tracks[t].clips[i].id == clipId)
                return {t, i};
    return {};
}

const TimelineClip *Timeline::clip(const QUuid &clipId) const
{
    const ClipRef r = find(clipId);
    return r.isValid() ? &tracks[r.track].clips[r.index] : nullptr;
}

Timeline::Kind Timeline::kindOf(const QUuid &clipId) const
{
    const ClipRef r = find(clipId);
    return r.isValid() ? tracks[r.track].kind : Kind::Video;
}

QList<QUuid> Timeline::withLinked(const QList<QUuid> &ids) const
{
    QSet<QUuid> links;
    QList<QUuid> out;
    for (const QUuid &id : ids) {
        if (const TimelineClip *c = clip(id)) {
            if (!out.contains(id))
                out << id;
            if (!c->linkId.isNull())
                links.insert(c->linkId);
        }
    }
    if (links.isEmpty())
        return out;
    for (const auto &t : tracks)
        for (const auto &c : t.clips)
            if (links.contains(c.linkId) && !out.contains(c.id))
                out << c.id;
    return out;
}

QList<QUuid> Timeline::clipsAt(qint64 frame) const
{
    QList<QUuid> out;
    for (int t = int(tracks.size()) - 1; t >= 0; --t) // video: top track first
        if (tracks[t].kind == Kind::Video)
            for (const auto &c : tracks[t].clips)
                if (c.covers(frame))
                    out << c.id;
    for (const auto &t : tracks)
        if (t.kind == Kind::Audio)
            for (const auto &c : t.clips)
                if (c.covers(frame))
                    out << c.id;
    return out;
}

// ---------------------------------------------------------------------------
// Editing

void Timeline::insertSorted(int track, const TimelineClip &c)
{
    auto &clips = tracks[track].clips;
    auto it = std::lower_bound(clips.begin(), clips.end(), c.start,
                               [](const TimelineClip &a, qint64 s) { return a.start < s; });
    clips.insert(it, c);
}

void Timeline::clearRange(int track, qint64 from, qint64 to)
{
    if (to <= from)
        return;
    auto &clips = tracks[track].clips;
    QList<TimelineClip> out;
    out.reserve(clips.size() + 1);
    for (TimelineClip c : clips) {
        if (c.end() <= from || c.start >= to) {
            out << c;
            continue;
        }
        if (c.start < from && c.end() > to) {
            // The range is inside the clip: keep both sides.
            TimelineClip right = c;
            right.id = QUuid::createUuid();
            right.linkId = QUuid();
            right.in += c.src(to - c.start);
            right.start = to;
            right.length = c.end() - to;
            right.fadeIn = 0;
            right.head = {};
            c.length = from - c.start;
            c.fadeOut = 0;
            c.tail = {};
            out << c << right;
        } else if (c.start < from) {
            c.length = from - c.start;
            c.fadeOut = 0;
            out << c;
        } else if (c.end() > to) {
            const qint64 cut = to - c.start;
            c.in += c.src(cut);
            c.start = to;
            c.length -= cut;
            c.fadeIn = 0;
            out << c;
        }
        // else: fully covered, dropped
    }
    clips = out;
}

QList<QUuid> Timeline::place(const MediaItem &item, const SequenceSettings &seq, int videoTrack, int audioTrack,
                             qint64 start)
{
    QList<QUuid> ids;
    const qint64 len = defaultClipFrames(item, seq);
    start = std::max<qint64>(0, start);
    const bool wantVideo = item.info.hasVideo() && videoTrack >= 0;
    const bool wantAudio = item.info.hasAudio() && audioTrack >= 0;
    const QUuid link = (wantVideo && wantAudio) ? QUuid::createUuid() : QUuid();

    auto put = [&](Kind kind, int n) {
        while (count(kind) <= n)
            addTrack(kind);
        const int t = indexOf(kind, n);
        if (tracks[t].locked)
            return;
        TimelineClip c;
        c.id = QUuid::createUuid();
        c.mediaId = item.id;
        c.linkId = link;
        c.start = start;
        c.in = 0;
        c.length = len;
        clearRange(t, c.start, c.end());
        insertSorted(t, c);
        ids << c.id;
    };
    if (wantVideo)
        put(Kind::Video, videoTrack);
    if (wantAudio)
        put(Kind::Audio, audioTrack);
    return ids;
}

QList<QUuid> Timeline::place(const MediaItem &item, int videoTrack, int audioTrack, qint64 start, qint64 in,
                             qint64 length)
{
    QList<QUuid> ids;
    if (length <= 0)
        return ids;
    start = std::max<qint64>(0, start);
    in = std::max<qint64>(0, in);
    const bool wantVideo = item.info.hasVideo() && videoTrack >= 0;
    const bool wantAudio = item.info.hasAudio() && audioTrack >= 0;
    const QUuid link = (wantVideo && wantAudio) ? QUuid::createUuid() : QUuid();
    auto put = [&](Kind kind, int n) {
        while (count(kind) <= n)
            addTrack(kind);
        const int t = indexOf(kind, n);
        if (tracks[t].locked)
            return;
        TimelineClip c;
        c.id = QUuid::createUuid();
        c.mediaId = item.id;
        c.linkId = link;
        c.start = start;
        c.in = item.info.kind == MediaInfo::Kind::Image ? 0 : in;
        c.length = length;
        clearRange(t, c.start, c.end());
        insertSorted(t, c);
        ids << c.id;
    };
    if (wantVideo)
        put(Kind::Video, videoTrack);
    if (wantAudio)
        put(Kind::Audio, audioTrack);
    if (ids.size() == 1 && wantVideo && wantAudio) { // one part hit a locked track: no link to nothing
        const ClipRef r = find(ids.first());
        tracks[r.track].clips[r.index].linkId = QUuid();
    }
    return ids;
}

bool Timeline::insertSpace(qint64 at, qint64 length, const QList<int> &onlyTracks)
{
    if (length <= 0 || at < 0)
        return false;
    bool changed = false;
    QHash<QUuid, QUuid> newLinks;
    for (int t = 0; t < tracks.size(); ++t) {
        if (tracks[t].locked || (!onlyTracks.isEmpty() && !onlyTracks.contains(t)))
            continue;
        QList<TimelineClip> out;
        for (TimelineClip c : tracks[t].clips) {
            if (c.end() <= at) {
                out << c;
            } else if (c.start >= at) {
                c.start += length;
                out << c;
                changed = true;
            } else {
                // Split: the right part moves.
                TimelineClip right = c;
                right.id = QUuid::createUuid();
                if (!c.linkId.isNull()) {
                    if (!newLinks.contains(c.linkId))
                        newLinks.insert(c.linkId, QUuid::createUuid());
                    right.linkId = newLinks.value(c.linkId);
                }
                right.in += c.src(at - c.start);
                right.length = c.end() - at;
                right.start = at + length;
                right.fadeIn = 0;
                right.head = {};
                c.length = at - c.start;
                c.fadeOut = 0;
                c.tail = {};
                out << c << right;
                changed = true;
            }
        }
        tracks[t].clips = out;
    }
    if (onlyTracks.isEmpty())
        for (auto &m : markers)
            if (m.frame >= at) {
                m.frame += length;
                changed = true;
            }
    return changed;
}

// ---------------------------------------------------------------------------
// Roll, slip, slide

namespace {

// The clip on the same track touching `c` before its head / after its tail.
int neighbour(const QList<TimelineClip> &clips, int index, bool before)
{
    const TimelineClip &c = clips[index];
    if (before)
        return index > 0 && clips[index - 1].end() == c.start ? index - 1 : -1;
    return index + 1 < clips.size() && clips[index + 1].start == c.end() ? index + 1 : -1;
}

} // namespace

bool Timeline::roll(const QUuid &id, bool head, qint64 frame, const MediaFrames &mediaFrames)
{
    const TimelineClip *orig = clip(id);
    if (!orig)
        return false;
    const qint64 edge = head ? orig->start : orig->end();

    struct Pair
    {
        int track;
        int left;  // clip ending at the edge, -1 = gap
        int right; // clip starting at the edge, -1 = gap
    };
    QList<Pair> pairs;
    for (const QUuid &cid : withLinked({id})) {
        const ClipRef r = find(cid);
        const TimelineClip &c = tracks[r.track].clips[r.index];
        if ((head ? c.start : c.end()) != edge)
            continue;
        if (tracks[r.track].locked)
            return false;
        const int other = neighbour(tracks[r.track].clips, r.index, head);
        pairs << (head ? Pair{r.track, other, r.index} : Pair{r.track, r.index, other});
    }
    if (pairs.isEmpty())
        return false;

    qint64 lo = 0, hi = kInf;
    for (const Pair &p : pairs) {
        const auto &clips = tracks[p.track].clips;
        if (p.left >= 0) {
            const TimelineClip &l = clips[p.left];
            const qint64 media = l.isTitle() ? -1 : mediaFrames(l.mediaId);
            lo = std::max(lo, l.start + 1);
            if (media >= 0)
                hi = std::min(hi, l.start + l.framesAvailable(media));
        } else {
            const int r = p.right;
            lo = std::max(lo, r > 0 ? clips[r - 1].end() : 0);
        }
        if (p.right >= 0) {
            const TimelineClip &rc = clips[p.right];
            const qint64 media = rc.isTitle() ? -1 : mediaFrames(rc.mediaId);
            hi = std::min(hi, rc.end() - 1);
            if (media >= 0)
                lo = std::max(lo, rc.start - rc.framesBefore());
        } else {
            const int l = p.left;
            hi = std::min(hi, l + 1 < clips.size() ? clips[l + 1].start : kInf);
        }
    }
    if (lo > hi)
        return false;
    frame = std::clamp(frame, lo, hi);
    if (frame == edge)
        return false;
    const qint64 d = frame - edge;
    for (const Pair &p : pairs) {
        auto &clips = tracks[p.track].clips;
        if (p.left >= 0) {
            TimelineClip &l = clips[p.left];
            l.length += d;
            l.fadeOut = std::min(l.fadeOut, l.length);
        }
        if (p.right >= 0) {
            TimelineClip &r = clips[p.right];
            r.start += d;
            r.length -= d;
            if (!r.isTitle() && mediaFrames(r.mediaId) >= 0)
                r.in = std::max<qint64>(0, r.in + r.src(d));
            r.fadeIn = std::min(r.fadeIn, r.length);
        }
    }
    return true;
}

bool Timeline::slip(const QUuid &id, qint64 deltaFrames, const MediaFrames &mediaFrames)
{
    if (deltaFrames == 0)
        return false;
    QList<ClipRef> refs;
    qint64 lo = std::numeric_limits<qint64>::min() / 4, hi = kInf;
    for (const QUuid &cid : withLinked({id})) {
        const ClipRef r = find(cid);
        const TimelineClip &c = tracks[r.track].clips[r.index];
        if (tracks[r.track].locked)
            return false;
        const qint64 media = c.isTitle() ? -1 : mediaFrames(c.mediaId);
        if (media < 0)
            continue; // a still picture looks the same everywhere
        lo = std::max(lo, -c.framesBefore());
        hi = std::min(hi, c.framesAvailable(media) - c.length);
        refs << r;
    }
    if (refs.isEmpty() || lo > hi)
        return false;
    const qint64 d = std::clamp(deltaFrames, lo, hi);
    if (d == 0)
        return false;
    for (const ClipRef &r : refs)
        {
            TimelineClip &c = tracks[r.track].clips[r.index];
            c.in = std::max<qint64>(0, c.in + c.src(d));
        }
    return true;
}

bool Timeline::slide(const QUuid &id, qint64 deltaFrames, const MediaFrames &mediaFrames)
{
    if (deltaFrames == 0)
        return false;
    QList<ClipRef> refs;
    qint64 lo = std::numeric_limits<qint64>::min() / 4, hi = kInf;
    for (const QUuid &cid : withLinked({id})) {
        const ClipRef r = find(cid);
        if (tracks[r.track].locked)
            return false;
        const auto &clips = tracks[r.track].clips;
        const TimelineClip &c = clips[r.index];
        const int prev = neighbour(clips, r.index, true);
        const int next = neighbour(clips, r.index, false);
        // Left: the previous clip gets shorter (keeps one frame) or the gap closes.
        if (prev >= 0)
            lo = std::max(lo, -(clips[prev].length - 1));
        else
            lo = std::max(lo, -(c.start - (r.index > 0 ? clips[r.index - 1].end() : 0)));
        // Moving left the next clip starts earlier in its source.
        if (next >= 0) {
            const TimelineClip &n = clips[next];
            if (!n.isTitle() && mediaFrames(n.mediaId) >= 0)
                lo = std::max(lo, -n.framesBefore());
            hi = std::min(hi, n.length - 1);
        } else {
            hi = std::min(hi, r.index + 1 < clips.size() ? clips[r.index + 1].start - c.end() : kInf);
        }
        // Right: the previous clip gets longer, as far as its source goes.
        if (prev >= 0) {
            const TimelineClip &p = clips[prev];
            const qint64 media = p.isTitle() ? -1 : mediaFrames(p.mediaId);
            if (media >= 0)
                hi = std::min(hi, p.framesAvailable(media) - p.length);
        }
        refs << r;
    }
    if (refs.isEmpty() || lo > hi)
        return false;
    const qint64 d = std::clamp(deltaFrames, lo, hi);
    if (d == 0)
        return false;
    for (const ClipRef &r : refs) {
        auto &clips = tracks[r.track].clips;
        const int prev = neighbour(clips, r.index, true);
        const int next = neighbour(clips, r.index, false);
        if (prev >= 0) {
            clips[prev].length += d;
            clips[prev].fadeOut = std::min(clips[prev].fadeOut, clips[prev].length);
        }
        if (next >= 0) {
            TimelineClip &n = clips[next];
            n.start += d;
            n.length -= d;
            if (!n.isTitle() && mediaFrames(n.mediaId) >= 0)
                n.in = std::max<qint64>(0, n.in + n.src(d));
            n.fadeIn = std::min(n.fadeIn, n.length);
        }
        clips[r.index].start += d;
    }
    return true;
}

bool Timeline::link(const QList<QUuid> &ids)
{
    QList<ClipRef> refs;
    for (const QUuid &id : ids) {
        const ClipRef r = find(id);
        if (r.isValid() && !tracks[r.track].locked)
            refs << r;
    }
    if (refs.size() < 2)
        return false;
    const QUuid link = QUuid::createUuid();
    for (const ClipRef &r : refs)
        tracks[r.track].clips[r.index].linkId = link;
    return true;
}

bool Timeline::unlink(const QList<QUuid> &ids)
{
    bool changed = false;
    for (const QUuid &id : withLinked(ids)) {
        const ClipRef r = find(id);
        TimelineClip &c = tracks[r.track].clips[r.index];
        if (!c.linkId.isNull() && !tracks[r.track].locked) {
            c.linkId = QUuid();
            changed = true;
        }
    }
    return changed;
}

bool Timeline::isLinked(const QUuid &id) const
{
    const TimelineClip *c = clip(id);
    return c && !c->linkId.isNull() && withLinked({id}).size() > 1;
}

QUuid Timeline::addMarker(qint64 frame, const QString &name)
{
    frame = std::max<qint64>(0, frame);
    if (const TimelineMarker *m = markerAt(frame))
        return m->id;
    TimelineMarker m;
    m.id = QUuid::createUuid();
    m.frame = frame;
    m.name = name;
    auto it = std::lower_bound(markers.begin(), markers.end(), frame,
                               [](const TimelineMarker &a, qint64 f) { return a.frame < f; });
    markers.insert(it, m);
    return m.id;
}

bool Timeline::removeMarker(const QUuid &id)
{
    const auto before = markers.size();
    markers.erase(std::remove_if(markers.begin(), markers.end(), [&](const TimelineMarker &m) { return m.id == id; }),
                  markers.end());
    return markers.size() != before;
}

bool Timeline::renameMarker(const QUuid &id, const QString &name)
{
    for (auto &m : markers)
        if (m.id == id) {
            if (m.name == name)
                return false;
            m.name = name;
            return true;
        }
    return false;
}

const TimelineMarker *Timeline::markerAt(qint64 frame) const
{
    for (const auto &m : markers)
        if (m.frame == frame)
            return &m;
    return nullptr;
}

qint64 Timeline::nextMarker(qint64 frame) const
{
    for (const auto &m : markers)
        if (m.frame > frame)
            return m.frame;
    return -1;
}

qint64 Timeline::prevMarker(qint64 frame) const
{
    for (auto it = markers.crbegin(); it != markers.crend(); ++it)
        if (it->frame < frame)
            return it->frame;
    return -1;
}

bool Timeline::move(const QList<QUuid> &ids, qint64 deltaFrames, Kind trackKind, int deltaTracks)
{
    if (ids.isEmpty() || (deltaFrames == 0 && deltaTracks == 0))
        return false;

    struct Move
    {
        TimelineClip clip;
        int from;
        int to;
    };
    QList<Move> moves;
    for (const QUuid &id : ids) {
        const ClipRef r = find(id);
        if (!r.isValid())
            continue;
        const TimelineTrack &src = tracks[r.track];
        if (src.locked)
            return false;
        int to = r.track;
        if (src.kind == trackKind && deltaTracks != 0) {
            int n = 0;
            for (int i = 0; i < r.track; ++i)
                if (tracks[i].kind == src.kind)
                    ++n;
            to = indexOf(src.kind, n + deltaTracks);
            if (to < 0)
                return false;
        }
        if (tracks[to].locked)
            return false;
        TimelineClip c = src.clips[r.index];
        c.start += deltaFrames;
        if (c.start < 0)
            return false;
        moves << Move{c, r.track, to};
    }
    if (moves.isEmpty())
        return false;

    for (const Move &m : moves) {
        auto &clips = tracks[m.from].clips;
        clips.erase(std::remove_if(clips.begin(), clips.end(),
                                   [&](const TimelineClip &c) { return c.id == m.clip.id; }),
                    clips.end());
    }
    for (const Move &m : moves) {
        clearRange(m.to, m.clip.start, m.clip.end());
        insertSorted(m.to, m.clip);
    }
    return true;
}

bool Timeline::trimImpl(const QUuid &id, bool head, qint64 frame, const QHash<QUuid, qint64> &mediaFrames)
{
    const TimelineClip *orig = clip(id);
    if (!orig)
        return false;
    const qint64 edge = head ? orig->start : orig->end();

    QList<ClipRef> members;
    for (const QUuid &cid : withLinked({id})) {
        const ClipRef r = find(cid);
        const TimelineClip &c = tracks[r.track].clips[r.index];
        if ((head ? c.start : c.end()) == edge)
            members << r;
    }

    qint64 lo = head ? 0 : 1;
    qint64 hi = kInf;
    for (const ClipRef &r : members) {
        if (tracks[r.track].locked)
            return false;
        const auto &clips = tracks[r.track].clips;
        const TimelineClip &c = clips[r.index];
        const qint64 media = mediaFrames.value(c.mediaId, -1);
        if (head) {
            const qint64 prevEnd = r.index > 0 ? clips[r.index - 1].end() : 0;
            const qint64 sourceStart = media >= 0 ? c.start - c.framesBefore() : 0;
            lo = std::max({lo, prevEnd, sourceStart});
            hi = std::min(hi, c.end() - 1);
        } else {
            const qint64 nextStart = r.index + 1 < clips.size() ? clips[r.index + 1].start : kInf;
            const qint64 sourceEnd = media >= 0 ? c.start + c.framesAvailable(media) : kInf;
            lo = std::max(lo, c.start + 1);
            hi = std::min({hi, nextStart, sourceEnd});
        }
    }
    if (lo > hi)
        return false;
    frame = std::clamp(frame, lo, hi);
    if (frame == edge)
        return false;

    for (const ClipRef &r : members) {
        TimelineClip &c = tracks[r.track].clips[r.index];
        if (head) {
            const qint64 delta = frame - c.start;
            c.in = std::max<qint64>(0, c.in + c.src(delta));
            c.start = frame;
            c.length -= delta;
        } else {
            c.length = frame - c.start;
        }
    }
    return true;
}

QList<QUuid> Timeline::splitAt(qint64 frame, const QList<QUuid> &only)
{
    QSet<QUuid> wanted;
    if (!only.isEmpty())
        for (const QUuid &id : withLinked(only))
            wanted.insert(id);

    QHash<QUuid, QUuid> newLinks;
    QList<QUuid> created;
    for (int t = 0; t < tracks.size(); ++t) {
        if (tracks[t].locked)
            continue;
        auto &clips = tracks[t].clips;
        for (int i = 0; i < clips.size(); ++i) {
            TimelineClip &c = clips[i];
            if (frame <= c.start || frame >= c.end())
                continue;
            if (!wanted.isEmpty() && !wanted.contains(c.id))
                continue;
            TimelineClip right = c;
            right.id = QUuid::createUuid();
            if (!c.linkId.isNull()) {
                if (!newLinks.contains(c.linkId))
                    newLinks.insert(c.linkId, QUuid::createUuid());
                right.linkId = newLinks.value(c.linkId);
            }
            right.in += c.src(frame - c.start);
            right.start = frame;
            right.length = c.end() - frame;
            right.fadeIn = 0;
            right.head = {};
            c.length = frame - c.start;
            c.fadeOut = 0;
            c.tail = {};
            clips.insert(i + 1, right);
            created << right.id;
            ++i;
        }
    }
    return created;
}

bool Timeline::remove(const QList<QUuid> &ids, bool ripple)
{
    const QSet<QUuid> doomed(ids.begin(), ids.end());
    bool changed = false;
    for (auto &t : tracks) {
        if (t.locked)
            continue;
        QList<TimelineClip> removed;
        QList<TimelineClip> kept;
        for (const auto &c : t.clips)
            (doomed.contains(c.id) ? removed : kept) << c;
        if (removed.isEmpty())
            continue;
        changed = true;
        if (ripple) {
            for (auto &c : kept) {
                qint64 shift = 0;
                for (const auto &r : removed)
                    if (r.end() <= c.start)
                        shift += r.length;
                c.start -= shift;
            }
        }
        t.clips = kept;
    }
    return changed;
}

QUuid Timeline::placeTitle(const TitleData &title, int videoTrack, qint64 start, qint64 length)
{
    const int t = indexOf(Kind::Video, videoTrack);
    if (t < 0 || tracks[t].locked || length <= 0)
        return {};
    TimelineClip c;
    c.id = QUuid::createUuid();
    c.title = title;
    c.start = std::max<qint64>(0, start);
    c.length = length;
    clearRange(t, c.start, c.end());
    insertSorted(t, c);
    return c.id;
}

bool Timeline::isAudible(int track) const
{
    if (track < 0 || track >= tracks.size() || tracks[track].kind != Kind::Audio || tracks[track].muted)
        return false;
    bool anySolo = false;
    for (const auto &t : tracks)
        anySolo = anySolo || (t.kind == Kind::Audio && t.solo);
    return !anySolo || tracks[track].solo;
}

int Timeline::addTrack(Kind kind)
{
    TimelineTrack t;
    t.id = QUuid::createUuid();
    t.kind = kind;
    if (kind == Kind::Video) {
        int pos = 0;
        while (pos < tracks.size() && tracks[pos].kind == Kind::Video)
            ++pos;
        tracks.insert(pos, t);
        return pos;
    }
    tracks.append(t);
    return int(tracks.size()) - 1;
}

bool Timeline::removeClipsOfMedia(const QUuid &mediaId)
{
    bool changed = false;
    for (auto &t : tracks) {
        const auto before = t.clips.size();
        t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(),
                                     [&](const TimelineClip &c) { return c.mediaId == mediaId; }),
                      t.clips.end());
        changed |= t.clips.size() != before;
    }
    return changed;
}

bool Timeline::removeClipsOfNest(const QUuid &nestId)
{
    bool changed = false;
    for (auto &t : tracks) {
        const auto before = t.clips.size();
        t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(),
                                     [&](const TimelineClip &c) { return c.nestId == nestId; }),
                      t.clips.end());
        changed |= t.clips.size() != before;
    }
    return changed;
}

void Timeline::rescale(double oldFps, double newFps)
{
    if (oldFps <= 0 || newFps <= 0 || oldFps == newFps)
        return;
    const double f = newFps / oldFps;
    for (auto &t : tracks) {
        qint64 prevEnd = 0;
        for (auto &c : t.clips) {
            c.start = std::max(prevEnd, qint64(std::llround(c.start * f)));
            c.in = qint64(std::llround(c.in * f));
            c.length = std::max<qint64>(1, qint64(std::llround(c.length * f)));
            c.fadeIn = qint64(std::llround(c.fadeIn * f));
            c.fadeOut = qint64(std::llround(c.fadeOut * f));
            c.motion.rescale(f);
            c.head.length = qint64(std::llround(c.head.length * f));
            c.tail.length = qint64(std::llround(c.tail.length * f));
            prevEnd = c.end();
        }
    }
    for (auto &m : markers)
        m.frame = qint64(std::llround(m.frame * f));
}

QList<qint64> Timeline::snapPoints(const QSet<QUuid> &exclude) const
{
    QList<qint64> pts{0};
    for (const auto &t : tracks)
        for (const auto &c : t.clips)
            if (!exclude.contains(c.id))
                pts << c.start << c.end();
    for (const auto &m : markers)
        pts << m.frame;
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    return pts;
}

// ---------------------------------------------------------------------------
// JSON

QJsonArray Timeline::toJson() const
{
    QJsonArray ta;
    for (const auto &t : tracks) {
        QJsonObject to;
        to[QStringLiteral("id")] = idStr(t.id);
        to[QStringLiteral("kind")] = t.kind == Kind::Video ? QStringLiteral("video") : QStringLiteral("audio");
        to[QStringLiteral("locked")] = t.locked;
        to[QStringLiteral("muted")] = t.muted;
        to[QStringLiteral("hidden")] = t.hidden;
        if (t.solo)
            to[QStringLiteral("solo")] = true;
        if (t.gainDb != 0)
            to[QStringLiteral("gain_db")] = t.gainDb;
        if (t.pan != 0)
            to[QStringLiteral("pan")] = t.pan;
        QJsonArray ca;
        for (const auto &c : t.clips) {
            QJsonObject co;
            co[QStringLiteral("id")] = idStr(c.id);
            co[QStringLiteral("media")] = idStr(c.mediaId);
            if (c.isNested())
                co[QStringLiteral("nest")] = idStr(c.nestId);
            co[QStringLiteral("link")] = idStr(c.linkId);
            co[QStringLiteral("start")] = double(c.start);
            co[QStringLiteral("in")] = double(c.in);
            co[QStringLiteral("length")] = double(c.length);
            if (c.isTitle()) {
                QJsonObject tt;
                tt[QStringLiteral("text")] = c.title.text;
                tt[QStringLiteral("size")] = c.title.size;
                tt[QStringLiteral("color")] = c.title.color;
                tt[QStringLiteral("position")] = c.title.position == TitleData::Position::Top      ? QStringLiteral("top")
                                                 : c.title.position == TitleData::Position::Center ? QStringLiteral("center")
                                                                                                    : QStringLiteral("bottom");
                tt[QStringLiteral("box")] = c.title.box;
                co[QStringLiteral("title")] = tt;
            }
            if (c.gainDb != 0)
                co[QStringLiteral("gainDb")] = c.gainDb;
            if (c.fadeIn > 0)
                co[QStringLiteral("fadeIn")] = double(c.fadeIn);
            if (c.fadeOut > 0)
                co[QStringLiteral("fadeOut")] = double(c.fadeOut);
            if (c.speed != 1.0)
                co[QStringLiteral("speed")] = c.speed;
            for (const auto &[key, tr] : {std::pair{QStringLiteral("head"), c.head}, std::pair{QStringLiteral("tail"), c.tail}}) {
                if (tr.isNone())
                    continue;
                QJsonObject to2;
                to2[QStringLiteral("kind")] = ClipTransition::kindToString(tr.kind);
                to2[QStringLiteral("length")] = double(tr.length);
                co[key] = to2;
            }
            const QJsonObject motion = c.motion.toJson();
            if (!motion.isEmpty())
                co[QStringLiteral("motion")] = motion;
            if (!c.effects.isEmpty())
                co[QStringLiteral("effects")] = effectsToJson(c.effects);
            if (!c.color.isNeutral())
                co[QStringLiteral("color")] = c.color.toJson();
            ca.append(co);
        }
        to[QStringLiteral("clips")] = ca;
        ta.append(to);
    }
    return ta;
}

Timeline Timeline::fromJson(const QJsonArray &a)
{
    Timeline tl;
    QList<TimelineTrack> video, audio;
    for (const auto &v : a) {
        const QJsonObject to = v.toObject();
        TimelineTrack t;
        t.id = QUuid(to.value(QStringLiteral("id")).toString());
        if (t.id.isNull())
            t.id = QUuid::createUuid();
        t.kind = to.value(QStringLiteral("kind")).toString() == QLatin1String("audio") ? Kind::Audio : Kind::Video;
        t.locked = to.value(QStringLiteral("locked")).toBool();
        t.muted = to.value(QStringLiteral("muted")).toBool();
        t.hidden = to.value(QStringLiteral("hidden")).toBool();
        t.solo = t.kind == Kind::Audio && to.value(QStringLiteral("solo")).toBool();
        if (t.kind == Kind::Audio) {
            t.gainDb = std::clamp(to.value(QStringLiteral("gain_db")).toDouble(), TimelineTrack::kMinGainDb,
                                  TimelineTrack::kMaxGainDb);
            t.pan = std::clamp(to.value(QStringLiteral("pan")).toDouble(), -1.0, 1.0);
        }
        for (const auto &cv : to.value(QStringLiteral("clips")).toArray()) {
            const QJsonObject co = cv.toObject();
            TimelineClip c;
            c.id = QUuid(co.value(QStringLiteral("id")).toString());
            if (c.id.isNull())
                c.id = QUuid::createUuid();
            c.mediaId = QUuid(co.value(QStringLiteral("media")).toString());
            if (c.mediaId.isNull())
                c.nestId = QUuid(co.value(QStringLiteral("nest")).toString());
            c.linkId = QUuid(co.value(QStringLiteral("link")).toString());
            c.start = std::max<qint64>(0, qint64(co.value(QStringLiteral("start")).toDouble()));
            c.in = std::max<qint64>(0, qint64(co.value(QStringLiteral("in")).toDouble()));
            c.length = qint64(co.value(QStringLiteral("length")).toDouble());
            if (c.isTitle()) {
                // Titles live on video tracks only.
                const QJsonObject tt = co.value(QStringLiteral("title")).toObject();
                if (t.kind != Kind::Video || tt.isEmpty())
                    continue;
                c.title.text = tt.value(QStringLiteral("text")).toString();
                c.title.size = std::clamp(tt.value(QStringLiteral("size")).toInt(80), 8, 400);
                const QString color = tt.value(QStringLiteral("color")).toString();
                if (!color.isEmpty())
                    c.title.color = color;
                const QString pos = tt.value(QStringLiteral("position")).toString();
                c.title.position = pos == QLatin1String("top")      ? TitleData::Position::Top
                                   : pos == QLatin1String("center") ? TitleData::Position::Center
                                                                    : TitleData::Position::Bottom;
                c.title.box = tt.value(QStringLiteral("box")).toBool();
                c.linkId = QUuid();
            }
            c.gainDb = std::clamp(co.value(QStringLiteral("gainDb")).toDouble(), -60.0, 12.0);
            c.fadeIn = std::max<qint64>(0, qint64(co.value(QStringLiteral("fadeIn")).toDouble()));
            c.fadeOut = std::max<qint64>(0, qint64(co.value(QStringLiteral("fadeOut")).toDouble()));
            for (auto *tr : {&c.head, &c.tail}) {
                const QJsonObject to2 = co.value(tr == &c.head ? QStringLiteral("head") : QStringLiteral("tail")).toObject();
                tr->kind = ClipTransition::kindFromString(to2.value(QStringLiteral("kind")).toString());
                tr->length = std::max<qint64>(0, qint64(to2.value(QStringLiteral("length")).toDouble()));
                if (t.kind == Kind::Audio && tr->kind != ClipTransition::Kind::None)
                    tr->kind = ClipTransition::Kind::Dissolve;
                if (tr->isNone())
                    *tr = {};
            }
            if (!c.mediaId.isNull())
                c.speed = std::clamp(co.value(QStringLiteral("speed")).toDouble(1.0), 0.1, 10.0);
            if (t.kind == Kind::Video) {
                c.motion = ClipMotion::fromJson(co.value(QStringLiteral("motion")).toObject());
                c.effects = effectsFromJson(co.value(QStringLiteral("effects")).toArray());
                c.color = ClipColor::fromJson(co.value(QStringLiteral("color")).toObject());
            }
            if (c.length > 0)
                t.clips << c;
        }
        // Repair order and overlaps so a hand-edited file cannot break the editor.
        std::sort(t.clips.begin(), t.clips.end(),
                  [](const TimelineClip &x, const TimelineClip &y) { return x.start < y.start; });
        QList<TimelineClip> clean;
        for (const auto &c : t.clips)
            if (clean.isEmpty() || c.start >= clean.last().end())
                clean << c;
        t.clips = clean;
        (t.kind == Kind::Video ? video : audio) << t;
    }
    tl.tracks = video + audio;
    if (tl.count(Kind::Video) == 0)
        tl.addTrack(Kind::Video);
    if (tl.count(Kind::Audio) == 0)
        tl.addTrack(Kind::Audio);
    return tl;
}

QJsonArray Timeline::markersToJson() const
{
    QJsonArray a;
    for (const auto &m : markers) {
        QJsonObject o;
        o[QStringLiteral("id")] = idStr(m.id);
        o[QStringLiteral("frame")] = double(m.frame);
        if (!m.name.isEmpty())
            o[QStringLiteral("name")] = m.name;
        a.append(o);
    }
    return a;
}

void Timeline::markersFromJson(const QJsonArray &a)
{
    markers.clear();
    for (const auto &v : a) {
        const QJsonObject o = v.toObject();
        const qint64 frame = std::max<qint64>(0, qint64(o.value(QStringLiteral("frame")).toDouble()));
        if (markerAt(frame))
            continue;
        const QUuid id = addMarker(frame, o.value(QStringLiteral("name")).toString());
        const QUuid saved(o.value(QStringLiteral("id")).toString());
        if (!saved.isNull())
            for (auto &m : markers)
                if (m.id == id)
                    m.id = saved;
    }
}

// ---------------------------------------------------------------------------
// Transitions (phase 8a)

QString ClipTransition::kindToString(Kind k)
{
    switch (k) {
    case Kind::Dissolve: return QStringLiteral("dissolve");
    case Kind::DipBlack: return QStringLiteral("dipBlack");
    case Kind::DipWhite: return QStringLiteral("dipWhite");
    case Kind::WipeLeft: return QStringLiteral("wipeLeft");
    case Kind::WipeRight: return QStringLiteral("wipeRight");
    case Kind::None: break;
    }
    return QStringLiteral("none");
}

ClipTransition::Kind ClipTransition::kindFromString(const QString &s)
{
    for (Kind k : {Kind::Dissolve, Kind::DipBlack, Kind::DipWhite, Kind::WipeLeft, Kind::WipeRight})
        if (s == kindToString(k))
            return k;
    return Kind::None;
}

const TimelineClip *Timeline::touchingAfter(const QUuid &id) const
{
    const ClipRef r = find(id);
    if (!r.isValid())
        return nullptr;
    const auto &clips = tracks[r.track].clips;
    if (r.index + 1 < clips.size() && clips[r.index + 1].start == clips[r.index].end())
        return &clips[r.index + 1];
    return nullptr;
}

const TimelineClip *Timeline::touchingBefore(const QUuid &id) const
{
    const ClipRef r = find(id);
    if (!r.isValid())
        return nullptr;
    const auto &clips = tracks[r.track].clips;
    if (r.index > 0 && clips[r.index - 1].end() == clips[r.index].start)
        return &clips[r.index - 1];
    return nullptr;
}

ClipTransition Timeline::transitionAt(const QUuid &id, bool head) const
{
    const TimelineClip *c = clip(id);
    if (!c)
        return {};
    if (head)
        return c->head;
    if (const TimelineClip *next = touchingAfter(id))
        return next->head;
    return c->tail;
}

bool Timeline::setTransition(const QUuid &id, bool head, const ClipTransition &t0)
{
    const ClipRef r = find(id);
    if (!r.isValid() || tracks[r.track].locked)
        return false;
    ClipTransition t = t0;
    if (tracks[r.track].kind == Kind::Audio && t.kind != ClipTransition::Kind::None)
        t.kind = ClipTransition::Kind::Dissolve;
    auto &clips = tracks[r.track].clips;
    int target = r.index;
    if (!head && touchingAfter(id)) {
        target = r.index + 1; // a cut: it belongs to the head of the next clip
        head = true;
    }
    TimelineClip &c = clips[target];
    qint64 limit = c.length;
    if (head) {
        if (target > 0 && clips[target - 1].end() == c.start)
            limit = std::min(limit, clips[target - 1].length);
    }
    t.length = std::clamp<qint64>(t.length, 0, limit);
    if (t.isNone())
        t = {};
    ClipTransition &slot = head ? c.head : c.tail;
    if (slot == t)
        return false;
    slot = t;
    return true;
}

} // namespace bc
