#include "core/Backups.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace bc::Backups {

namespace {
const char *kStamp = "yyyy-MM-dd HH-mm-ss";

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

QString folderFor(const QString &projectPath)
{
    return QFileInfo(projectPath).absolutePath() + QStringLiteral("/Backups");
}

QList<Version> list(const QString &projectPath)
{
    const QFileInfo pi(projectPath);
    const QRegularExpression re(QStringLiteral("^%1 (\\d{4}-\\d\\d-\\d\\d \\d\\d-\\d\\d-\\d\\d)(?:-(\\d+))?\\.%2$")
                                    .arg(QRegularExpression::escape(pi.completeBaseName()),
                                         QRegularExpression::escape(pi.suffix())),
                                QRegularExpression::CaseInsensitiveOption); // Windows file names
    struct Row { Version v; int seq; };
    QList<Row> rows;
    const QDir dir(folderFor(projectPath));
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files)) {
        const auto m = re.match(fi.fileName());
        if (!m.hasMatch())
            continue;
        const QDateTime t = QDateTime::fromString(m.captured(1), QString::fromLatin1(kStamp));
        if (!t.isValid())
            continue;
        rows.append({{fi.absoluteFilePath(), t}, m.captured(2).toInt()});
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        return a.v.time != b.v.time ? a.v.time > b.v.time : a.seq > b.seq;
    });
    QList<Version> out;
    for (const Row &r : rows)
        out.append(r.v);
    return out;
}

bool backup(const QString &projectPath, int keep, QString *error)
{
    if (keep <= 0 || !QFileInfo::exists(projectPath))
        return true;
    const QByteArray current = readAll(projectPath);
    QList<Version> versions = list(projectPath);
    if (!versions.isEmpty() && readAll(versions.first().path) == current)
        return true; // nothing new since the last version
    const QFileInfo pi(projectPath);
    QDir().mkpath(folderFor(projectPath));
    const QString stem = folderFor(projectPath) + QLatin1Char('/') + pi.completeBaseName() + QLatin1Char(' ')
                         + QDateTime::currentDateTime().toString(QString::fromLatin1(kStamp));
    QString target = stem + QLatin1Char('.') + pi.suffix();
    for (int n = 2; QFileInfo::exists(target); ++n)
        target = stem + QLatin1Char('-') + QString::number(n) + QLatin1Char('.') + pi.suffix();
    if (!QFile::copy(projectPath, target)) {
        if (error)
            *error = QStringLiteral("cannot write %1").arg(QDir::toNativeSeparators(target));
        return false;
    }
    versions = list(projectPath);
    for (qsizetype i = keep; i < versions.size(); ++i)
        QFile::remove(versions[i].path);
    return true;
}

} // namespace bc::Backups
