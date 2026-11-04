#include "app/Formatting.h"

#include <cmath>
#include <algorithm>

#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>

namespace bc::Fmt {

QString timecode(qint64 durationUs, double fps)
{
    if (durationUs < 0)
        durationUs = 0;
    const qint64 totalSec = durationUs / 1000000;
    const int h = int(totalSec / 3600);
    const int m = int((totalSec / 60) % 60);
    const int s = int(totalSec % 60);
    if (fps <= 0)
        return QStringLiteral("%1:%2:%3").arg(h, 2, 10, QLatin1Char('0')).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    const int f = int(double(durationUs % 1000000) / 1000000.0 * fps);
    return QStringLiteral("%1:%2:%3:%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(f, 2, 10, QLatin1Char('0'));
}

QString frameRate(int num, int den)
{
    if (num <= 0 || den <= 0)
        return {};
    const double v = double(num) / den;
    if (den == 1)
        return QString::number(num);
    return QString::number(v, 'f', 3).remove(QRegularExpression(QStringLiteral("0+$"))).remove(QRegularExpression(QStringLiteral("\\.$")));
}

QString fileSize(qint64 bytes)
{
    const double kb = 1024.0, mb = kb * 1024, gb = mb * 1024;
    if (bytes >= gb)
        return QCoreApplication::translate("Fmt", "%1 GB").arg(bytes / gb, 0, 'f', 2);
    if (bytes >= mb)
        return QCoreApplication::translate("Fmt", "%1 MB").arg(bytes / mb, 0, 'f', 1);
    if (bytes >= kb)
        return QCoreApplication::translate("Fmt", "%1 KB").arg(bytes / kb, 0, 'f', 0);
    return QCoreApplication::translate("Fmt", "%1 B").arg(bytes);
}

QString resolution(int w, int h)
{
    if (w <= 0 || h <= 0)
        return {};
    return QStringLiteral("%1×%2").arg(w).arg(h);
}

QString audio(int sampleRate, int channels)
{
    if (sampleRate <= 0 || channels <= 0)
        return {};
    QString ch = channels == 1 ? QCoreApplication::translate("Fmt", "mono") : channels == 2 ? QCoreApplication::translate("Fmt", "stereo") : QCoreApplication::translate("Fmt", "%1 ch").arg(channels);
    return QCoreApplication::translate("Fmt", "%1 kHz, %2").arg(sampleRate / 1000.0, 0, 'g', 4).arg(ch);
}

QString kindName(MediaInfo::Kind kind)
{
    switch (kind) {
    case MediaInfo::Kind::Video: return QCoreApplication::translate("Fmt", "Video");
    case MediaInfo::Kind::Audio: return QCoreApplication::translate("Fmt", "Audio");
    case MediaInfo::Kind::Image: return QCoreApplication::translate("Fmt", "Image");
    case MediaInfo::Kind::Unknown: break;
    }
    return QCoreApplication::translate("Fmt", "Unknown");
}

QString sequenceSummary(const SequenceSettings &s)
{
    return QCoreApplication::translate("Fmt", "%1, %2 fps, %3")
        .arg(resolution(s.width, s.height), frameRate(s.fpsNum, s.fpsDen), audio(s.sampleRate, s.channels));
}

QString frameTimecode(qint64 frames, double fps)
{
    const qint64 base = std::max<qint64>(1, qint64(std::llround(fps)));
    frames = std::max<qint64>(0, frames);
    const qint64 f = frames % base;
    const qint64 totalSec = frames / base;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(totalSec / 3600, 2, 10, QLatin1Char('0'))
        .arg((totalSec / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(totalSec % 60, 2, 10, QLatin1Char('0'))
        .arg(f, 2, 10, QLatin1Char('0'));
}

} // namespace bc::Fmt
