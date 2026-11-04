#pragma once

#include "core/Project.h"

#include <QString>

namespace bc::Fmt {

// 00:01:23:12 (hours:minutes:seconds:frames) when fps > 0, else 00:01:23
QString timecode(qint64 durationUs, double fps);
QString frameRate(int num, int den);       // "25", "29.97"
QString fileSize(qint64 bytes);            // "1.4 GB"
QString resolution(int w, int h);          // "1920×1080"
QString audio(int sampleRate, int channels);
QString kindName(MediaInfo::Kind kind);    // translated
QString sequenceSummary(const SequenceSettings &s);

} // namespace bc::Fmt
