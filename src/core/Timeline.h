#pragma once

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QUuid>

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

// One clip on a track. All positions are in frames of the sequence frame rate.
struct TimelineClip
{
    QUuid id;
    QUuid mediaId;      // null for a title clip
    QUuid linkId;       // video and audio parts of one placement share it; null = not linked
    qint64 start = 0;   // position on the timeline
    qint64 in = 0;      // first used frame of the source
    qint64 length = 0;  // number of frames
    TitleData title;    // used when isTitle()
    double gainDb = 0;  // audio clips: volume change in dB
    qint64 fadeIn = 0;  // audio clips: fade lengths in frames
    qint64 fadeOut = 0;

    bool isTitle() const { return mediaId.isNull(); }
    qint64 end() const { return start + length; } // exclusive
    bool covers(qint64 frame) const { return frame >= start && frame < end(); }
    bool operator==(const TimelineClip &o) const
    {
        return id == o.id && mediaId == o.mediaId && linkId == o.linkId && start == o.start && in == o.in
               && length == o.length && title == o.title && gainDb == o.gainDb && fadeIn == o.fadeIn
               && fadeOut == o.fadeOut;
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

    static Timeline makeDefault(); // V1-V3, A1-A3

    bool operator==(const Timeline &o) const { return tracks == o.tracks; }
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
    // Converts all positions when the sequence frame rate changes.
    void rescale(double oldFps, double newFps);

    // Clip edges for snapping, without the given clips.
    QList<qint64> snapPoints(const QSet<QUuid> &exclude) const;

    QJsonArray toJson() const;
    static Timeline fromJson(const QJsonArray &a);

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
