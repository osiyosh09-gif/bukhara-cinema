#include "core/Subtitles.h"

#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>

namespace bc {

namespace {
QString idStr(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }
} // namespace

QJsonObject SubtitleStyle::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("show")] = show;
    o[QStringLiteral("size")] = size;
    o[QStringLiteral("color")] = color;
    o[QStringLiteral("box")] = box;
    o[QStringLiteral("margin")] = margin;
    return o;
}

SubtitleStyle SubtitleStyle::fromJson(const QJsonObject &o)
{
    SubtitleStyle s;
    s.show = o.value(QStringLiteral("show")).toBool(s.show);
    s.size = std::clamp(o.value(QStringLiteral("size")).toInt(s.size), kMinSize, kMaxSize);
    const QString c = o.value(QStringLiteral("color")).toString();
    if (QRegularExpression(QStringLiteral("^#[0-9a-fA-F]{6}$")).match(c).hasMatch())
        s.color = c;
    s.box = o.value(QStringLiteral("box")).toBool(s.box);
    s.margin = std::clamp(o.value(QStringLiteral("margin")).toInt(s.margin), 0, kMaxMargin);
    return s;
}

QJsonArray subtitlesToJson(const QList<Subtitle> &list)
{
    QJsonArray a;
    for (const Subtitle &s : list) {
        QJsonObject o;
        o[QStringLiteral("id")] = idStr(s.id);
        o[QStringLiteral("start")] = double(s.start);
        o[QStringLiteral("length")] = double(s.length);
        o[QStringLiteral("text")] = s.text;
        a.append(o);
    }
    return a;
}

QList<Subtitle> subtitlesFromJson(const QJsonArray &a)
{
    QList<Subtitle> out;
    for (const auto &v : a) {
        const QJsonObject o = v.toObject();
        Subtitle s;
        s.id = QUuid(o.value(QStringLiteral("id")).toString());
        if (s.id.isNull())
            s.id = QUuid::createUuid();
        s.start = std::max<qint64>(0, qint64(o.value(QStringLiteral("start")).toDouble()));
        s.length = qint64(o.value(QStringLiteral("length")).toDouble());
        s.text = o.value(QStringLiteral("text")).toString();
        if (s.length > 0)
            out << s;
    }
    std::stable_sort(out.begin(), out.end(), [](const Subtitle &a, const Subtitle &b) { return a.start < b.start; });
    return out;
}

namespace Srt {

namespace {

// Windows-1251, bytes 0x80..0xFF.
const char16_t kCp1251[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

} // namespace

QString decode(const QByteArray &data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(data.mid(3));
    if (data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF")) {
        QStringDecoder dec(data.startsWith("\xFF\xFE") ? QStringConverter::Utf16LE : QStringConverter::Utf16BE);
        return dec(data.mid(2));
    }
    QStringDecoder utf8(QStringConverter::Utf8);
    const QString s = utf8(data);
    if (!utf8.hasError())
        return s;
    QString out;
    out.reserve(data.size());
    for (char ch : data) {
        const auto b = uint8_t(ch);
        out += b < 0x80 ? QChar(b) : QChar(kCp1251[b - 0x80]);
    }
    return out;
}

QString formatTime(qint64 ms)
{
    ms = std::max<qint64>(0, ms);
    return QStringLiteral("%1:%2:%3,%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

qint64 parseTime(const QString &s, bool *ok)
{
    // hh:mm:ss,mmm (also "." and fewer digits of the fraction).
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d+):(\\d{1,2}):(\\d{1,2})(?:[,.](\\d{1,3}))?\\s*$"));
    const auto m = re.match(s);
    if (ok)
        *ok = m.hasMatch();
    if (!m.hasMatch())
        return 0;
    QString frac = m.captured(4);
    while (frac.size() < 3)
        frac += QLatin1Char('0');
    return m.captured(1).toLongLong() * 3600000 + m.captured(2).toLongLong() * 60000
           + m.captured(3).toLongLong() * 1000 + frac.toLongLong();
}

QList<Cue> parse(const QString &text, int *skipped)
{
    QString t = text;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    t.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    static const QRegularExpression blank(QStringLiteral("\\n[ \\t]*\\n"));
    static const QRegularExpression tags(QStringLiteral("<[^>]*>|\\{\\\\[^}]*\\}"));
    QList<Cue> out;
    int bad = 0;
    for (const QString &raw : t.split(blank, Qt::SkipEmptyParts)) {
        QStringList lines = raw.split(QLatin1Char('\n'));
        while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
            lines.removeFirst();
        if (lines.isEmpty())
            continue;
        // An optional number line, then "start --> end".
        int timeLine = lines.first().contains(QStringLiteral("-->")) ? 0 : 1;
        if (timeLine >= lines.size() || !lines[timeLine].contains(QStringLiteral("-->"))) {
            ++bad;
            continue;
        }
        const QStringList parts = lines[timeLine].split(QStringLiteral("-->"));
        bool ok1 = false, ok2 = false;
        // Players allow position values after the end time ("X1:...").
        const QString endPart = parts.value(1).trimmed().section(QLatin1Char(' '), 0, 0);
        Cue c;
        c.startMs = parseTime(parts.value(0), &ok1);
        c.endMs = parseTime(endPart, &ok2);
        QStringList body = lines.mid(timeLine + 1);
        for (QString &l : body)
            l = l.remove(tags).trimmed();
        while (!body.isEmpty() && body.last().isEmpty())
            body.removeLast();
        c.text = body.join(QLatin1Char('\n'));
        if (!ok1 || !ok2 || c.endMs <= c.startMs || c.text.isEmpty()) {
            ++bad;
            continue;
        }
        out << c;
    }
    std::stable_sort(out.begin(), out.end(), [](const Cue &a, const Cue &b) { return a.startMs < b.startMs; });
    if (skipped)
        *skipped = bad;
    return out;
}

QByteArray write(const QList<Cue> &cues)
{
    QString s;
    int n = 0;
    for (const Cue &c : cues) {
        QString text = c.text;
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        s += QString::number(++n) + QStringLiteral("\r\n") + formatTime(c.startMs) + QStringLiteral(" --> ")
             + formatTime(c.endMs) + QStringLiteral("\r\n") + text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"))
             + QStringLiteral("\r\n\r\n");
    }
    return s.toUtf8();
}

QList<Cue> fromSubtitles(const QList<Subtitle> &list, double fps)
{
    QList<Cue> out;
    if (fps <= 0)
        return out;
    for (const Subtitle &s : list) {
        if (s.text.trimmed().isEmpty() || s.length <= 0)
            continue;
        Cue c;
        c.startMs = qint64(std::llround(s.start * 1000.0 / fps));
        c.endMs = qint64(std::llround(s.end() * 1000.0 / fps));
        c.text = s.text.trimmed();
        out << c;
    }
    return out;
}

QList<Subtitle> toSubtitles(const QList<Cue> &cues, double fps)
{
    QList<Subtitle> out;
    if (fps <= 0)
        return out;
    for (const Cue &c : cues) {
        Subtitle s;
        s.id = QUuid::createUuid();
        s.start = qint64(std::llround(c.startMs * fps / 1000.0));
        s.length = std::max<qint64>(1, qint64(std::llround(c.endMs * fps / 1000.0)) - s.start);
        s.text = c.text;
        out << s;
    }
    return out;
}

} // namespace Srt

} // namespace bc
