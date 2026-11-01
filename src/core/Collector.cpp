#include "core/Collector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace bc::Collector {

Plan plan(const Project &project, const QString &name, const QString &destination, bool onlyUsed)
{
    Plan p;
    p.folder = QDir::cleanPath(destination + QLatin1Char('/') + name);
    p.projectFile = p.folder + QLatin1Char('/') + name + QLatin1Char('.') + Project::fileExtension();
    p.project = project;
    p.project.name = name;

    if (onlyUsed) {
        QSet<QUuid> used;
        for (const Timeline *tl : p.project.allTimelines()) // nested sequences count too
            for (const TimelineTrack &t : tl->tracks)
                for (const TimelineClip &c : t.clips)
                    if (!c.mediaId.isNull())
                        used.insert(c.mediaId);
        QList<QUuid> drop;
        for (const MediaItem &m : p.project.items)
            if (!used.contains(m.id))
                drop << m.id;
        for (const QUuid &id : drop)
            p.project.removeItem(id);
        p.unusedRemoved = int(drop.size());
    }

    QSet<QString> taken;          // lower-case target names in Media/
    QHash<QString, QString> done; // same source file used twice -> one copy
    const QString mediaDir = p.folder + QStringLiteral("/Media/");
    for (MediaItem &m : p.project.items) {
        const QFileInfo fi(m.path);
        if (!fi.exists()) {
            p.missing << QDir::toNativeSeparators(m.path);
            continue;
        }
        const QString key = fi.absoluteFilePath().toLower();
        if (done.contains(key)) {
            m.path = done.value(key);
            continue;
        }
        QString fileName = fi.fileName();
        for (int n = 2; taken.contains(fileName.toLower()); ++n)
            fileName = QStringLiteral("%1 (%2)%3")
                           .arg(fi.completeBaseName())
                           .arg(n)
                           .arg(fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix());
        taken.insert(fileName.toLower());
        const QString to = mediaDir + fileName;
        p.copies.append({fi.absoluteFilePath(), to, fi.size()});
        p.totalBytes += fi.size();
        done.insert(key, to);
        m.path = to;
    }
    return p;
}

bool run(const Plan &plan, QString *error, const std::function<void(qint64)> &progress,
         const std::atomic_bool *cancel)
{
    QStringList written;
    auto fail = [&](const QString &e) {
        for (const QString &f : written)
            QFile::remove(f);
        QDir().rmdir(plan.folder + QStringLiteral("/Media")); // only if empty
        QDir().rmdir(plan.folder);
        if (error)
            *error = e;
        return false;
    };
    if (QFileInfo::exists(plan.projectFile))
        return fail(QStringLiteral("%1 already exists").arg(QDir::toNativeSeparators(plan.projectFile)));
    if (!QDir().mkpath(plan.folder + QStringLiteral("/Media")))
        return fail(QStringLiteral("cannot create %1").arg(QDir::toNativeSeparators(plan.folder)));

    qint64 copied = 0;
    QByteArray buf(4 << 20, Qt::Uninitialized);
    for (const Plan::Copy &c : plan.copies) {
        QFile in(c.from), out(c.to);
        if (!in.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("cannot read %1: %2").arg(QDir::toNativeSeparators(c.from), in.errorString()));
        if (QFileInfo::exists(c.to) || !out.open(QIODevice::WriteOnly))
            return fail(QStringLiteral("cannot write %1").arg(QDir::toNativeSeparators(c.to)));
        written << c.to;
        for (;;) {
            if (cancel && cancel->load())
                return out.close(), fail(QStringLiteral("cancelled"));
            const qint64 n = in.read(buf.data(), buf.size());
            if (n < 0)
                return out.close(), fail(in.errorString());
            if (n == 0)
                break;
            if (out.write(buf.constData(), n) != n) {
                const QString e = QStringLiteral("cannot write %1: %2").arg(QDir::toNativeSeparators(c.to), out.errorString());
                out.close(); // Windows cannot delete an open file
                return fail(e);
            }
            copied += n;
            if (progress)
                progress(copied);
        }
        out.setFileTime(in.fileTime(QFileDevice::FileModificationTime), QFileDevice::FileModificationTime);
        out.close();
    }
    QString err;
    if (!plan.project.save(plan.projectFile, &err))
        return fail(err);
    return true;
}

} // namespace bc::Collector
