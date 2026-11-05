#pragma once

#include "core/Project.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

namespace bc {

// Timeline interchange with other editors (phase 15a): the main sequence is
// written as Final Cut Pro 7 XML (read by Premiere Pro and DaVinci Resolve),
// OpenTimelineIO or a CMX 3600 EDL. Only the edit itself travels: which
// part of which file sits where on which track, cross dissolves, markers and
// (XML only) clip volume. Everything else is counted in Result::losses so
// the user is told before exporting. No widgets here.
namespace Interchange {

enum class Format { FcpXml, Otio, Edl };

// What the chosen format cannot carry, counted per clip (or per item).
enum class Loss {
    Titles,            // text title clips (left out, a gap stays)
    PictureEffects,    // picture effects, chroma key, masks
    Color,             // colour correction, wheels, LUT
    Motion,            // position, scale, rotation, opacity
    Speed,             // speed changes: the clip plays at 100 % from the same first frame
    TransitionKinds,   // dips to black/white and wipes, written as cross dissolves
    Transitions,       // transitions left out (EDL, or next to a speed change or a nest edge)
    EndFades,          // fades at free clip ends
    SoundFades,        // fade in/out of audio clips
    ClipVolume,        // clip volume in dB
    SoundEffects,      // equalizer, compressor, noise reduction, hum removal
    Mixer,             // track volume, pan, master volume (counted once)
    Subtitles,         // subtitles of the sequence
    Markers,           // markers (EDL)
    OtherTracks,       // EDL: clips not on video track V1, and all sound
};

struct Result
{
    QByteArray data;
    QMap<Loss, int> losses;  // only entries > 0
    int clips = 0;           // clips written
    int flattened = 0;       // nested / multicamera clips written as the clips inside them
    int missingMedia = 0;    // written clips whose file is not on the disk
};

QString fileSuffix(Format f); // "xml", "otio", "edl"

// Writes the main sequence of the project. `checkFiles` = look on the disk
// for missing media (off in tests that use made-up paths).
Result write(const Project &project, Format format, bool checkFiles = true);

// Timecode HH:MM:SS:FF (non-drop) at a whole-number frame base.
QString timecode(qint64 frame, int base);
// Whole-number timecode base of a rate: 25 -> 25, 29.97 -> 30, 23.976 -> 24.
int timecodeBase(int fpsNum, int fpsDen);
// Frame number of a timecode "HH:MM:SS:FF" (";" or a drop-frame flag = 29.97/59.94
// drop-frame counting); -1 if it is not a timecode.
qint64 parseTimecode(const QString &tc, int base, bool dropFrame);

// --- reading (phase 15b): the same three formats, from other editors

// What is in the file but not imported, counted per clip (or item).
enum class ImportLoss {
    Generators,      // titles, colour mattes, bars and other generated items
    Effects,         // effects, motion, colour and opacity settings
    Speed,           // speed changes and freeze frames (the clip plays at 100 %)
    Nested,          // nested sequences, compound and multicamera clips
    TransitionKinds, // transitions other than dissolves (imported as dissolves)
    DisabledClips,   // clips switched off in the other editor
    NoMedia,         // clips without a usable media reference
};

struct ImportClip
{
    QString path;      // absolute local path; empty when the file gave only a name
    QString name;      // clip or file name (used to find the file when there is no path)
    TimelineTrack::Kind kind = TimelineTrack::Kind::Video;
    int track = 0;     // n-th track of its kind (0 = V1 / A1)
    qint64 start = 0;  // sequence frames
    qint64 in = 0;     // first source frame, sequence frames
    qint64 length = 0;
    double gainDb = 0;
    ClipTransition head; // dissolve with the touching clip before, or a fade at a free start
    ClipTransition tail; // fade at a free end
    int link = -1;       // clips with the same number belong together (picture + sound)

    qint64 end() const { return start + length; }
};

struct ImportMarker
{
    qint64 frame = 0;
    QString name;
};

struct ReadResult
{
    enum class Error { None, NotRecognized, Empty };
    Error error = Error::None;
    Format format = Format::FcpXml;
    QString name;           // sequence / timeline name from the file
    int fpsNum = 0;         // the file's own rate; 0 = none given (EDL)
    int fpsDen = 1;
    int width = 0, height = 0;
    QList<ImportClip> clips;  // sorted by kind, track, start; no overlaps on a track
    QList<ImportMarker> markers;
    QMap<ImportLoss, int> losses;
    int videoTracks = 0, audioTracks = 0;
};

// Format by file suffix (.xml, .otio, .edl); false for anything else.
bool formatFromPath(const QString &path, Format *format);
// Reads a timeline; times are converted to the rate of `seq`. Relative or
// name-only references are resolved against `baseDir` (the file's folder).
ReadResult read(const QByteArray &data, Format format, const SequenceSettings &seq, const QString &baseDir);

} // namespace Interchange
} // namespace bc
