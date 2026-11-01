#pragma once

#include <QByteArray>
#include <QString>

#include <atomic>

namespace bc::Waveform {

// Audio peaks of a media file: one byte (0..255 = silence..full scale) per
// 1/kPeaksPerSecond of a second, the loudest sample of all channels.
constexpr int kPeaksPerSecond = 200;

// Decodes the first audio stream with FFmpeg. Returns false (and fills
// *error) if the file has no readable audio. `cancel` may stop it early.
bool compute(const QString &path, QByteArray &peaks, QString *error = nullptr,
             const std::atomic_bool *cancel = nullptr);

// Highest peak (0..255) of the source time range [fromSec, toSec).
int peakIn(const QByteArray &peaks, double fromSec, double toSec);

// Small binary cache file ("BCWF" + version + rate + data).
bool save(const QString &file, const QByteArray &peaks);
bool load(const QString &file, QByteArray &peaks);

} // namespace bc::Waveform
