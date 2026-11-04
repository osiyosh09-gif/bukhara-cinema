#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace bc::Backups {

// Earlier versions of a project file. Before a save replaces the file, the
// old file is copied to "<project folder>/Backups/<name> <date time>.bcproj".
struct Version
{
    QString path;
    QDateTime time;
};

QString folderFor(const QString &projectPath);

// Versions of this project, newest first.
QList<Version> list(const QString &projectPath);

// Copies the current file into the backups folder (unless it equals the
// newest version) and keeps at most `keep` versions. keep <= 0 does nothing.
bool backup(const QString &projectPath, int keep, QString *error = nullptr);

} // namespace bc::Backups
