#pragma once

#include <QJsonObject>
#include <QList>

#include <QtGlobal>

namespace bc {

// Place of a clip's picture in the frame (phase 7). Position is the offset
// of the picture centre from the frame centre, as a fraction of the frame
// width and height (so it survives a change of the sequence size).
struct ClipTransform
{
    double x = 0;          // -1 .. 1 of the frame width
    double y = 0;          // -1 .. 1 of the frame height
    double scale = 100;    // percent
    double rotation = 0;   // degrees, clockwise
    double opacity = 100;  // percent

    bool isIdentity() const { return x == 0 && y == 0 && scale == 100 && rotation == 0 && opacity == 100; }
    bool operator==(const ClipTransform &o) const
    {
        return x == o.x && y == o.y && scale == o.scale && rotation == o.rotation && opacity == o.opacity;
    }
    bool operator!=(const ClipTransform &o) const { return !(*this == o); }
    ClipTransform clamped() const;
    static ClipTransform lerp(const ClipTransform &a, const ClipTransform &b, double t);
};

struct TransformKey
{
    qint64 frame = 0; // source frame (the clip's `in` counts), so splits and trims keep keys in place
    ClipTransform value;
    bool operator==(const TransformKey &o) const { return frame == o.frame && value == o.value; }
};

// The transform of a clip: one value, or keyframes (linear in between, held
// before the first and after the last).
struct ClipMotion
{
    ClipTransform base;        // used when there are no keys
    QList<TransformKey> keys;  // sorted by frame, one per frame

    bool animated() const { return !keys.isEmpty(); }
    bool isIdentity() const;
    ClipTransform at(qint64 sourceFrame) const;
    bool hasKey(qint64 sourceFrame) const;
    // With keys: sets (adds) the key at the frame. Without: sets the base value.
    void set(qint64 sourceFrame, const ClipTransform &value);
    bool removeKey(qint64 sourceFrame);
    // Turns keyframes on (one key at the frame with the current value) or off
    // (the value at the frame becomes the base).
    void setAnimated(bool on, qint64 sourceFrame);
    // Nearest key after / before the frame, or -1.
    qint64 nextKey(qint64 sourceFrame) const;
    qint64 prevKey(qint64 sourceFrame) const;
    void rescale(double factor);

    bool operator==(const ClipMotion &o) const { return base == o.base && keys == o.keys; }
    bool operator!=(const ClipMotion &o) const { return !(*this == o); }

    QJsonObject toJson() const; // empty for the identity
    static ClipMotion fromJson(const QJsonObject &o);
    // Compact text for the engine filter; frames are shifted by -offset.
    QByteArray toEngineString(qint64 offset) const;
    static ClipMotion fromEngineString(const QByteArray &s);
};

} // namespace bc
