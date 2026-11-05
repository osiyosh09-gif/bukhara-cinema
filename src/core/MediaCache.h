#pragma once

#include <QString>

namespace bc::MediaCache {

// Files the program makes from media files, kept in the user's cache folder
// (never next to the media): thumbnails, audio waveforms and proxy videos.
// They are keyed by the media file's path, size and change time, so an
// edited or replaced file gets new ones.
// Loudness profiles share the waveform folder (both are small audio summaries).
// Rendered previews (phase 13b) are keyed by the sequence part, not a media file.
// Scene change profiles (phase 14a) are small too and share the waveform folder,
// as do sound envelopes for the automatic multicamera edit (phase 16a) and the
// words heard in a file (phase 14b; one file per model, see Speech).
enum class Kind { Thumbnail, Waveform, Proxy, Loudness, Preview, Scenes, Envelope, Transcript };

QString root();                                    // .../cache folder
QString folder(Kind kind);
QString pathFor(Kind kind, const QString &mediaPath);

// Total size in bytes of one kind of cache files.
qint64 size(Kind kind);
// Deletes the files of one kind. Files that are open (a proxy being
// played) may stay; returns the number of files deleted.
int clear(Kind kind);

} // namespace bc::MediaCache
