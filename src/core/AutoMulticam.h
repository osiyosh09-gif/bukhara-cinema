#pragma once

#include <QList>
#include <QString>

#include <vector>

namespace bc::AutoMulticam {

// Automatic multicamera edit (phase 16a): picks which camera is shown when,
// from the sound each camera recorded (no AI model, nothing leaves the PC).
//
// Speaker: each camera's microphone hears the person nearest to it loudest.
// Its level is compared with its own background (so microphones of different
// sensitivity are fair); the camera whose sound is clearly above the others
// for half a second is shown. When nobody or several people speak, the wide
// angle (if one is chosen) is shown, else the shot stays. Shots are never
// shorter than minShot; a shot longer than maxShot is broken by the wide
// angle or the next camera, so a long talk does not stay on one picture.
// Rhythm: shots of minShot..maxShot seconds in turn, never the same camera
// twice in a row (for music, or when every camera hears the same sound).

enum class Mode { Speaker, Rhythm };

struct Settings
{
    Mode mode = Mode::Speaker;
    int wideAngle = -1;   // shown when nobody or everybody speaks; -1 = none
    double minShot = 2.0; // seconds
    double maxShot = 8.0; // seconds; 0 = no limit
    double margin = 6.0;  // dB the speaker's microphone is above the others
    unsigned seed = 1;    // rhythm: order of the cameras
};

struct Cut
{
    qint64 frame = 0; // from this frame on ...
    int angle = 0;    // ... this camera is shown
    bool operator==(const Cut &o) const { return frame == o.frame && angle == o.angle; }
};

struct Plan
{
    QList<Cut> cuts;       // the first one is at `from`
    QList<double> share;   // part of the time each camera is shown (0..1)
    double separation = 0; // speaker: part of the speech where one camera clearly led (0..1)
};

constexpr int kRate = 100; // level values per second (as AudioSync::envelope)

// `levels[a]`: RMS level of camera a's sound, kRate values per second, index
// 0 = frame 0 (empty: no sound). `picture[a]`: whether camera a has a picture
// at each step (empty: always). Frames from..to (exclusive) at `fps`.
Plan plan(const QList<std::vector<float>> &levels, const QList<std::vector<bool>> &picture, double fps,
          qint64 from, qint64 to, const Settings &settings);

// Envelope files (MediaCache::Kind::Envelope): floats with a small header.
bool save(const QString &file, const std::vector<float> &env);
bool load(const QString &file, std::vector<float> &env);

} // namespace bc::AutoMulticam
