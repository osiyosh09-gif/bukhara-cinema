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

// "avformat 61.7.100" etc. for the About / licenses window.
QString ffmpegVersionString();
QString ffmpegLicense();

} // namespace bc::MediaProbe
