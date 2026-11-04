#pragma once

#include "core/Project.h"

#include <QImage>
#include <QString>
#include <QStringList>

namespace bc::MediaProbe {

// File extensions offered in the import dialog.
QStringList videoExtensions();
QStringList audioExtensions();
QStringList imageExtensions();
bool isSupportedFile(const QString &path);

// Reads technical information with FFmpeg (images with Qt).
// Returns false and fills *error if the file cannot be read.
bool probe(const QString &path, MediaInfo &out, QString *error);

// Decodes one representative frame (about 10% into the clip) and scales it
// to fit maxWidth x maxHeight. Audio-only files return a null image.
QImage thumbnail(const QString &path, int maxWidth, int maxHeight, QString *error = nullptr);

// Decodes the frame shown at `timeUs` (from the start of the file), scaled to
// fit maxWidth x maxHeight. Images return the image; audio-only a null image.
QImage frameAt(const QString &path, qint64 timeUs, int maxWidth, int maxHeight, QString *error = nullptr);

// A video encoder that can write MP4 on this computer.
struct VideoEncoder
{
    QString codec;      // FFmpeg encoder name, e.g. "h264_qsv"
    QString label;      // shown to the user, e.g. "H.264 (Intel Quick Sync)"
    QString pixFmt;     // pixel format the encoder takes
    bool isH264 = false;
};

// Encoders that really open on this computer, best first: H.264 through
// Intel Quick Sync, Windows Media Foundation, NVIDIA or AMD (all licensed
// with the hardware or Windows, no x264), then MPEG-4 Part 2 as the last
// resort that always works. The result is cached.
QList<VideoEncoder> usableVideoEncoders();

// "avformat 61.7.100" etc. for the About / licenses window.
QString ffmpegVersionString();
QString ffmpegLicense();

} // namespace bc::MediaProbe
