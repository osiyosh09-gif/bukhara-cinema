#pragma once

#include "core/Timeline.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QUuid>

namespace bc {

// Technical description of a media file, filled by MediaProbe.
struct MediaInfo
{
    enum class Kind { Unknown, Video, Audio, Image };

    Kind kind = Kind::Unknown;
    qint64 durationUs = 0;      // 0 for still images
    int width = 0;
    int height = 0;
    int fpsNum = 0;             // frame rate as a fraction, 0 if none
    int fpsDen = 1;
    QString videoCodec;
    QString audioCodec;
    int sampleRate = 0;
    int channels = 0;
    qint64 fileSize = 0;
    QDateTime fileModified;

    bool hasVideo() const { return width > 0 && height > 0; }
    bool hasAudio() const { return channels > 0; }
    double fps() const { return fpsDen > 0 ? double(fpsNum) / fpsDen : 0.0; }
    static QString kindToString(Kind k);
    static Kind kindFromString(const QString &s);
};

struct BinFolder
{
    QUuid id;
    QString name;
    QUuid parentId; // null = root
};

struct MediaItem
{
    QUuid id;
    QString name;           // shown in the bin (defaults to file name)
    QString path;           // absolute path
    QUuid folderId;         // null = root
    MediaInfo info;
    bool probed = false;    // info was read successfully at least once
};

// Sequence (timeline) format. Defaults: 1920x1080, 25 fps, 48 kHz stereo.
struct SequenceSettings
{
    int width = 1920;
    int height = 1080;
    int fpsNum = 25;
    int fpsDen = 1;
    int sampleRate = 48000;
    int channels = 2;

    double fps() const { return fpsDen > 0 ? double(fpsNum) / fpsDen : 0.0; }
    bool operator==(const SequenceSettings &o) const
    {
        return width == o.width && height == o.height && fpsNum == o.fpsNum && fpsDen == o.fpsDen
               && sampleRate == o.sampleRate && channels == o.channels;
    }
    bool operator!=(const SequenceSettings &o) const { return !(*this == o); }
};

// A sequence used as one clip in another (Clip > Nest). It has the frame
// size and rate of the project sequence.
struct NestedSequence
{
    QUuid id;
    QString name;
    Timeline timeline;
    // Multicamera sequence (phase 12a): video track n and audio track n hold
    // angle n, lined up in time; a clip of it shows one angle (TimelineClip::angle).
    bool multicam = false;

    bool operator==(const NestedSequence &o) const
    {
        return id == o.id && name == o.name && timeline == o.timeline && multicam == o.multicam;
    }
};

// The whole editing project. It is the single source of truth that later
// phases (timeline, preview, export) read from.
class Project
{
public:
    static constexpr int kFormatVersion = 14; // 2: timeline added; 3: titles, clip volume and fades; 4: markers; 5: nested sequences; 6: clip motion and speed; 7: transitions; 8: clip effects; 9: colour correction; 10: audio mixer (track volume and pan, master); 11: sound effects on audio clips; 12: subtitles; 13: multicamera sequences and clip angles; 14: VST 3 plug-ins on audio clips
    static QString fileExtension() { return QStringLiteral("bcproj"); }

    QString name = QStringLiteral("Untitled");
    SequenceSettings sequence;
    Timeline timeline = Timeline::makeDefault();
    QList<BinFolder> folders;
    QList<MediaItem> items;
    QList<NestedSequence> nested;

    NestedSequence *nest(const QUuid &id);
    const NestedSequence *nest(const QUuid &id) const;
    // Every timeline of the project: the main one and the nested ones.
    QList<Timeline *> allTimelines();

    // --- folders
    BinFolder &addFolder(const QString &name, const QUuid &parent = {});
    const BinFolder *folder(const QUuid &id) const;
    bool renameFolder(const QUuid &id, const QString &name);
    // Removes the folder, its sub-folders and the media items inside them
    // (and their clips on the timeline).
    void removeFolder(const QUuid &id);

    // --- media
    MediaItem &addItem(const QString &path, const QUuid &folder = {});
    MediaItem *item(const QUuid &id);
    const MediaItem *item(const QUuid &id) const;
    bool removeItem(const QUuid &id);
    bool containsPath(const QString &path) const;

    // --- (de)serialization
    // Paths are stored absolute and relative to the project file, so a
    // project folder moved together with its media still opens.
    QByteArray toJson(const QString &projectFilePath) const;
    static bool fromJson(const QByteArray &json, const QString &projectFilePath, Project &out, QString *error);

    bool save(const QString &filePath, QString *error) const;
    static bool load(const QString &filePath, Project &out, QString *error);
};

} // namespace bc
