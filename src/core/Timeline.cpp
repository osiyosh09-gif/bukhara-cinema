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
            right.in += to - c.start;
            right.start = to;
            right.length = c.end() - to;
            c.length = from - c.start;
            out << c << right;
        } else if (c.start < from) {
            c.length = from - c.start;
            out << c;
        } else if (c.end() > to) {
            const qint64 cut = to - c.start;
            c.in += cut;
            c.start = to;
            c.length -= cut;
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
            const qint64 sourceStart = media >= 0 ? c.start - c.in : 0;
            lo = std::max({lo, prevEnd, sourceStart});
            hi = std::min(hi, c.end() - 1);
        } else {
            const qint64 nextStart = r.index + 1 < clips.size() ? clips[r.index + 1].start : kInf;
            const qint64 sourceEnd = media >= 0 ? c.start + (media - c.in) : kInf;
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
            c.in = std::max<qint64>(0, c.in + delta);
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
            right.in += frame - c.start;
            right.start = frame;
            right.length = c.end() - frame;
            c.length = frame - c.start;
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
            prevEnd = c.end();
        }
    }
}

QList<qint64> Timeline::snapPoints(const QSet<QUuid> &exclude) const
{
    QList<qint64> pts{0};
    for (const auto &t : tracks)
        for (const auto &c : t.clips)
            if (!exclude.contains(c.id))
                pts << c.start << c.end();
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
        QJsonArray ca;
        for (const auto &c : t.clips) {
            QJsonObject co;
            co[QStringLiteral("id")] = idStr(c.id);
            co[QStringLiteral("media")] = idStr(c.mediaId);
            co[QStringLiteral("link")] = idStr(c.linkId);
            co[QStringLiteral("start")] = double(c.start);
            co[QStringLiteral("in")] = double(c.in);
            co[QStringLiteral("length")] = double(c.length);
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
        for (const auto &cv : to.value(QStringLiteral("clips")).toArray()) {
            const QJsonObject co = cv.toObject();
            TimelineClip c;
            c.id = QUuid(co.value(QStringLiteral("id")).toString());
            if (c.id.isNull())
                c.id = QUuid::createUuid();
            c.mediaId = QUuid(co.value(QStringLiteral("media")).toString());
            c.linkId = QUuid(co.value(QStringLiteral("link")).toString());
            c.start = std::max<qint64>(0, qint64(co.value(QStringLiteral("start")).toDouble()));
            c.in = std::max<qint64>(0, qint64(co.value(QStringLiteral("in")).toDouble()));
            c.length = qint64(co.value(QStringLiteral("length")).toDouble());
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

} // namespace bc
