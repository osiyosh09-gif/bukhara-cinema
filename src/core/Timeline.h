#pragma once

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QUuid>

#include "core/Effects.h"
#include "core/Motion.h"

#include <cmath>
#include <functional>

namespace bc {

struct MediaItem;
struct SequenceSettings;

// Text drawn by a title clip. Sizes are in pixels of a 1080-line frame and
// scale with the sequence height.
struct TitleData
{
    enum class Position { Top, Center, Bottom };

    QString text;
    int size = 80;
    QString color = QStringLiteral("#ffffff");
    Position position = Position::Bottom;
    bool box = false; // dark box behind the text

    bool operator==(const TitleData &o) const
    {
        return text == o.text && size == o.size && color == o.color && position == o.position && box == o.box;
    }
    bool operator!=(const TitleData &o) const { return !(*this == o); }
};

// A transition at one end of a clip (phase 8a). At a cut between two
// touching clips on one track the transition belongs to the head of the
// later clip and mixes both; at a free end the clip fades in or out
// (from transparent, black or white). Audio clips use Dissolve only
// (a crossfade, or a fade at a free end).
struct ClipTransition
{
    enum class Kind { None, Dissolve, DipBlack, DipWhite, WipeLeft, WipeRight };
    Kind kind = Kind::None;
    qint64 length = 0; // frames

    bool isNone() const { return kind == Kind::None || length <= 0; }
    bool operator==(const ClipTransition &o) const
    {
        return (isNone() && o.isNone()) || (kind == o.kind && length == o.length);
    }
    bool operator!=(const ClipTransition &o) const { return !(*this == o); }
    static QString kindToString(Kind k);
    static Kind kindFromString(const QString &s);
};

// One clip on a track. All positions are in frames of the sequence frame rate.
struct TimelineClip
{
    QUuid id;
    QUuid mediaId;      // null for a title or nested clip
    QUuid nestId;       // a nested sequence (Project::nested) shown as one clip
    QUuid linkId;       // video and audio parts of one placement share it; null = not linked
    qint64 start = 0;   // position on the timeline
    qint64 in = 0;      // first used frame of the source
    qint64 length = 0;  // number of frames
    TitleData title;    // used when isTitle()
    double gainDb = 0;  // audio clips: volume change in dB
    qint64 fadeIn = 0;  // audio clips: fade lengths in frames
    qint64 fadeOut = 0;
    ClipMotion motion;  // video clips: position, scale, rotation, opacity (phase 7)
    ClipTransition head; // transition at the start (with the clip before, when they touch)
    ClipTransition tail; // fade at the end; used only when no clip touches the end
    double speed = 1.0; // media clips: 2.0 plays twice as fast (each timeline frame uses 2 source frames)
    ClipEffects effects; // video clips: picture effects, applied in order before the motion (phase 8b)

    // Source frames used by `frames` timeline frames.
    qint64 src(qint64 frames) const { return speed == 1.0 ? frames : qint64(std::llround(frames * speed)); }
    // Timeline frames the source before `in` would fill (trimming the head out).
    qint64 framesBefore() const { return speed == 1.0 ? in : qint64(std::floor(in / speed + 1e-9)); }
    // Timeline frames the source from `in` to its end would fill.
    qint64 framesAvailable(qint64 mediaFrames) const
    {
        return speed == 1.0 ? mediaFrames - in : qint64(std::floor((mediaFrames - in) / speed + 1e-9));
    }

    bool isTitle() const { return mediaId.isNull() && nestId.isNull(); }
    bool isNested() const { return !nestId.isNull(); }
    qint64 end() const { return start + length; } // exclusive
    bool covers(qint64 frame) const { return frame >= start && frame < end(); }
    bool operator==(const TimelineClip &o) const
    {
        return id == o.id && mediaId == o.mediaId && nestId == o.nestId && linkId == o.linkId && start == o.start && in == o.in
               && length == o.length && title == o.title && gainDb == o.gainDb && fadeIn == o.fadeIn
               && fadeOut == o.fadeOut && motion == o.motion && speed == o.speed && head == o.head && tail == o.tail
               && effects == o.effects;
    }
};

struct TimelineTrack
{
    enum class Kind { Video, Audio };

    QUuid id;
    Kind kind = Kind::Video;
    bool locked = false;
    bool muted = false;    // audio tracks
    bool hidden = false;   // video tracks
    bool solo = false;     // audio tracks: when any track is solo, only solo tracks sound
    QList<TimelineClip> clips; // sorted by start, never overlapping

    bool operator==(const TimelineTrack &o) const
    {
        return id == o.id && kind == o.kind && locked == o.locked && muted == o.muted && hidden == o.hidden
               && solo == o.solo && clips == o.clips;
    }
};

// A named point on the timeline (M key). Markers are part of the edit, so
// adding or removing one can be undone.
struct TimelineMarker
{
    QUuid id;
    qint64 frame = 0;
    QString name;

    bool operator==(const TimelineMarker &o) const { return id == o.id && frame == o.frame && name == o.name; }
};

// The edit: video tracks first (V1, V2, ...), then audio tracks (A1, A2, ...).
// Every editing operation works in "overwrite" mode: whatever lies under the
// new position of a clip is trimmed, split or removed, so clips on one track
// never overlap. The same model will drive preview and export.
class Timeline
{
public:
    using Kind = TimelineTrack::Kind;

    struct ClipRef
    {
        int track = -1;
        int index = -1;
        bool isValid() const { return track >= 0 && index >= 0; }
    };

    QList<TimelineTrack> tracks;
    QList<TimelineMarker> markers; // sorted by frame

    static Timeline makeDefault(); // V1-V3, A1-A3

    bool operator==(const Timeline &o) const { return tracks == o.tracks && markers == o.markers; }
    bool operator!=(const Timeline &o) const { return !(*this == o); }

    int count(Kind kind) const;
    // Index into `tracks` of the n-th (0-based) track of a kind, or -1.
    int indexOf(Kind kind, int n) const;
    // "V1", "A2", ...
    QString trackName(int track) const;
    bool isEmpty() const;
    qint64 duration() const; // end of the last clip

    ClipRef find(const QUuid &clipId) const;
    const TimelineClip *clip(const QUuid &clipId) const;
    Kind kindOf(const QUuid &clipId) const;
    // The clip and every clip linked to it.
    QList<QUuid> withLinked(const QList<QUuid> &ids) const;
    // Clips under `frame`, top video track first.
    QList<QUuid> clipsAt(qint64 frame) const;

    // --- editing; every function returns false when nothing changed

    // Places a media item. Video goes to video track `videoTrack` (n-th video
    // track), audio to `audioTrack` (n-th audio track); a negative number
    // skips that part. Returns the ids of the new clips.
    QList<QUuid> place(const MediaItem &item, const SequenceSettings &seq, int videoTrack, int audioTrack,
                       qint64 start);
    // The same with a part of the source: `length` frames from source frame `in`.
    QList<QUuid> place(const MediaItem &item, int videoTrack, int audioTrack, qint64 start, qint64 in,
                       qint64 length);

    // Insert edit helper: splits clips under `at` on the given tracks (all
    // unlocked tracks when empty) and moves everything at or after `at` right
    // by `length`. Locked tracks never change.
    bool insertSpace(qint64 at, qint64 length, const QList<int> &onlyTracks = {});

    // --- trimming tools (phase 6b). `mediaFrames(mediaId)` < 0 = unlimited
    // (images, titles). Each works on the clip and its linked clips, and
    // returns false when nothing changed (limits, locked tracks).
    using MediaFrames = std::function<qint64(const QUuid &)>;

    // Rolling edit: moves the cut between this clip and the clip touching it
    // (before the head or after the tail) to `frame`; one gets longer, the
    // other shorter, the sequence length stays. Without a neighbour the
    // edge may move into the empty space.
    bool roll(const QUuid &id, bool head, qint64 frame, const MediaFrames &mediaFrames);
    // Slip: shows a different part of the source in the same place and length.
    bool slip(const QUuid &id, qint64 deltaFrames, const MediaFrames &mediaFrames);
    // Slide: moves the clip; the clip before it gets longer or shorter at
    // its tail, the clip after it at its head, so nothing else moves.
    bool slide(const QUuid &id, qint64 deltaFrames, const MediaFrames &mediaFrames);

    // Gives the clips one new link (they move and trim together); returns
    // false when fewer than two clips were given.
    bool link(const QList<QUuid> &ids);
    // Removes the link of the clips (and of the clips linked to them).
    bool unlink(const QList<QUuid> &ids);
    bool isLinked(const QUuid &id) const;

    // --- transitions (phase 8a)
    // The clip that starts where this one ends on the same track, or null.
    const TimelineClip *touchingAfter(const QUuid &id) const;
    const TimelineClip *touchingBefore(const QUuid &id) const;
    // The transition shown at an end of the clip: the tail of a clip with a
    // touching clip after it is that clip's head.
    ClipTransition transitionAt(const QUuid &id, bool head) const;
    // Sets it (None removes it). The length is limited to the clip (and to
    // the touching clip). Locked tracks do not change.
    bool setTransition(const QUuid &id, bool head, const ClipTransition &t);

    // --- markers
    QUuid addMarker(qint64 frame, const QString &name = {}); // one marker per frame: returns the existing one
    bool removeMarker(const QUuid &id);
    bool renameMarker(const QUuid &id, const QString &name);
    const TimelineMarker *markerAt(qint64 frame) const;
    // Frame of the next marker after `frame` (or previous before it); -1 if none.
    qint64 nextMarker(qint64 frame) const;
    qint64 prevMarker(qint64 frame) const;

    // Moves clips by `deltaFrames`. Clips of `trackKind` also move `deltaTracks`
    // tracks (within tracks of the same kind); others stay on their track.
    // Fails if a clip would leave the timeline, land on a missing or locked
    // track, or is on a locked track.
    bool move(const QList<QUuid> &ids, qint64 deltaFrames, Kind trackKind, int deltaTracks);

    // Moves the start (head = true) or end of a clip to `frame`, together with
    // the same edge of linked clips. Limited by the media length and by the
    // neighbouring clips. `mediaFrames(mediaId)` < 0 means unlimited (images).
    template<typename F>
    bool trim(const QUuid &id, bool head, qint64 frame, F mediaFrames);

    // Splits clips under `frame`. With an empty `only`, all unlocked tracks;
    // otherwise only those clips (and their linked clips). Returns new clip ids.
    QList<QUuid> splitAt(qint64 frame, const QList<QUuid> &only = {});

    // Removes clips. With ripple, later clips on the same tracks move left to
    // close the gap.
    bool remove(const QList<QUuid> &ids, bool ripple);

    // Places a title clip on the n-th video track. Returns its id (null if the
    // track is missing or locked).
    QUuid placeTitle(const TitleData &title, int videoTrack, qint64 start, qint64 length);

    // True when this audio track is heard (not muted, and solo rules allow it).
    bool isAudible(int track) const;

    int addTrack(Kind kind); // returns the index into `tracks`
    bool removeClipsOfMedia(const QUuid &mediaId);
    bool removeClipsOfNest(const QUuid &nestId);
    // Converts all positions when the sequence frame rate changes.
    void rescale(double oldFps, double newFps);

    // Clip edges for snapping, without the given clips.
    QList<qint64> snapPoints(const QSet<QUuid> &exclude) const;

    QJsonArray toJson() const;
    static Timeline fromJson(const QJsonArray &a);
    QJsonArray markersToJson() const;
    void markersFromJson(const QJsonArray &a);

    // Clears [from, to) on a track (overwrite).
    void clearRange(int track, qint64 from, qint64 to);
    void insertSorted(int track, const TimelineClip &c);

private:
    bool trimImpl(const QUuid &id, bool head, qint64 frame, const QHash<QUuid, qint64> &mediaFrames);
};

// Length of a media item in sequence frames; -1 = unlimited (still image).
qint64 mediaLengthFrames(const MediaItem &item, const SequenceSettings &seq);
// Length a new clip gets when placed (images: 5 seconds).
qint64 defaultClipFrames(const MediaItem &item, const SequenceSettings &seq);

template<typename F>
bool Timeline::trim(const QUuid &id, bool head, qint64 frame, F mediaFrames)
{
    QHash<QUuid, qint64> lens;
    for (const QUuid &cid : withLinked({id}))
        if (const TimelineClip *c = clip(cid))
            lens.insert(c->mediaId, mediaFrames(c->mediaId));
    return trimImpl(id, head, frame, lens);
}

} // namespace bc
