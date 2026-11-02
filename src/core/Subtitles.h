#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

namespace bc {

// One subtitle line on the sequence (phase 11). Times are in frames of the
// sequence frame rate, like markers.
struct Subtitle
{
    QUuid id;
    qint64 start = 0;
    qint64 length = 0;
    QString text; // may hold line breaks

    qint64 end() const { return start + length; } // exclusive
    bool covers(qint64 frame) const { return frame >= start && frame < end(); }
    bool operator==(const Subtitle &o) const
    {
        return id == o.id && start == o.start && length == o.length && text == o.text;
    }
    bool operator!=(const Subtitle &o) const { return !(*this == o); }
};

// How the subtitles of a sequence look when drawn into the picture. The size
// is in pixels of a 1080-line frame and scales with the sequence height.
struct SubtitleStyle
{
    bool show = true;  // drawn in the Program monitor and burned in on export
    int size = 54;
    QString color = QStringLiteral("#ffffff");
    bool box = true;   // dark box behind the text (else a shadow)
    int margin = 7;    // distance of the last line from the bottom, % of the height

    static constexpr int kMinSize = 16, kMaxSize = 160, kMaxMargin = 45;

    bool operator==(const SubtitleStyle &o) const
    {
        return show == o.show && size == o.size && color == o.color && box == o.box && margin == o.margin;
    }
    bool operator!=(const SubtitleStyle &o) const { return !(*this == o); }
    QJsonObject toJson() const;
    static SubtitleStyle fromJson(const QJsonObject &o);
};

QJsonArray subtitlesToJson(const QList<Subtitle> &list);
QList<Subtitle> subtitlesFromJson(const QJsonArray &a); // sorted by start, empty ones left out

// SubRip (.srt) files.
namespace Srt {

struct Cue
{
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    bool operator==(const Cue &o) const { return startMs == o.startMs && endMs == o.endMs && text == o.text; }
};

// Text of a file: UTF-8 (with or without BOM), UTF-16 with BOM, otherwise
// Windows-1251 (old Uzbek and Russian files) when the bytes are not UTF-8.
QString decode(const QByteArray &data);
// Reads cues; broken blocks are skipped (their count goes to *skipped).
// Formatting tags (<i>, <b>, {\an8} ...) are removed.
QList<Cue> parse(const QString &text, int *skipped = nullptr);
// UTF-8 file text with Windows line ends, numbered from 1.
QByteArray write(const QList<Cue> &cues);
QString formatTime(qint64 ms);  // 01:02:03,456
qint64 parseTime(const QString &s, bool *ok = nullptr);

QList<Cue> fromSubtitles(const QList<Subtitle> &list, double fps);
QList<Subtitle> toSubtitles(const QList<Cue> &cues, double fps); // new ids

} // namespace Srt

} // namespace bc
