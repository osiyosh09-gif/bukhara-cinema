#pragma once

#include "core/Project.h"

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>
#include <QSize>

#include <memory>

namespace bc::RenderPreview {

// Render preview (phase 13, part 2). The picture of the parts of a sequence
// that are heavy to play (effects, colour, motion, speed, transitions,
// nested sequences, several layers) is rendered once into a file; playback
// then shows the file there instead of computing every layer again. Sound is
// never rendered (it is cheap) and subtitles are drawn on top as usual.
//
// The sequence is cut into regions at clip edges (never inside a transition).
// A region's key is a hash of everything that makes its picture: the clips
// in it (relative to the region, so moving a whole part keeps its file), the
// media files (path, size, change time), nested sequences, the sequence
// settings and the preview size. Changing any of it gives a new key, so an
// old file is simply not used any more.
struct Region
{
    qint64 start = 0;
    qint64 length = 0;
    QByteArray key;    // hex hash; the file name
    bool heavy = false; // worth rendering (shown red on the timeline until it is)
    qint64 end() const { return start + length; }
};

// Regions of `timeline` (a sequence of `project`) that have any picture.
QList<Region> regions(const Project &project, const Timeline &timeline, const QSize &previewSize);

// Where a region's file lives (cache folder "previews").
QString fileFor(const QByteArray &key);
QString folder();

// Writes pictures into a preview file: MPEG-4 Part 2, every frame a key
// frame (instant seeking), in Matroska. Via a temporary file, renamed by
// finish(), so a cancelled run leaves nothing behind.
class Writer
{
public:
    Writer();
    ~Writer();
    bool open(const QString &path, const QSize &size, int fpsNum, int fpsDen, QString *error = nullptr);
    bool write(const QImage &image, QString *error = nullptr); // any size: scaled to the file's
    bool finish(QString *error = nullptr);
    void abort(); // deletes the temporary file

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// Reads a preview file back, frame by frame, with the exact inverse of the
// Writer's colour conversion (so a rendered part looks like the computed
// one). Thread-safe; cheap to seek (every frame is a key frame).
class Reader
{
public:
    Reader();
    ~Reader();
    bool open(const QString &path, QString *error = nullptr);
    qint64 frameCount() const;
    QSize size() const;
    // Frame `index` scaled to `size` (empty = the file's own size), RGBA8888.
    QImage frame(qint64 index, const QSize &size = {});

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace bc::RenderPreview
