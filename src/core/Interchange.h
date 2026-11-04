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

} // namespace Interchange
} // namespace bc
