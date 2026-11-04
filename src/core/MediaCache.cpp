#include "core/MediaCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace bc::MediaCache {

namespace {
QString subfolder(Kind kind)
{
    switch (kind) {
    case Kind::Thumbnail: return QStringLiteral("thumbs");
    case Kind::Waveform: return QStringLiteral("waveforms");
    case Kind::Proxy: return QStringLiteral("proxies");
    case Kind::Loudness: return QStringLiteral("waveforms");
    case Kind::Preview: return QStringLiteral("previews");
    }
    return {};
}
QString extension(Kind kind)
{
    switch (kind) {
    case Kind::Thumbnail: return QStringLiteral(".jpg");
    case Kind::Waveform: return QStringLiteral(".bcwf");
    case Kind::Proxy: return QStringLiteral(".mkv");
    case Kind::Loudness: return QStringLiteral(".bcld");
    case Kind::Preview: return QStringLiteral(".mkv");
    }
    return {};
}
} // namespace

QString root()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

QString folder(Kind kind)
{
    return root() + QLatin1Char('/') + subfolder(kind);
}

QString pathFor(Kind kind, const QString &mediaPath)
{
    const QFileInfo fi(mediaPath);
    const QByteArray key = (fi.absoluteFilePath() + QString::number(fi.size())
                            + QString::number(fi.lastModified().toMSecsSinceEpoch()))
                               .toUtf8();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    return folder(kind) + QLatin1Char('/') + hash + extension(kind);
}

qint64 size(Kind kind)
{
    qint64 total = 0;
    QDirIterator it(folder(kind), QDir::Files);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

int clear(Kind kind)
{
    int n = 0;
    QDirIterator it(folder(kind), QDir::Files);
    while (it.hasNext())
        n += QFile::remove(it.next()) ? 1 : 0;
    return n;
}

} // namespace bc::MediaCache
