#pragma once

#include "core/Project.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace bc::Collector {

// "Collect project": copies a project and its media into one new folder
//   <destination>/<name>/<name>.bcproj
//   <destination>/<name>/Media/...
// so it can be moved to another computer or archived. The open project is
// not changed. Missing files are listed and keep their old paths.
struct Plan
{
    QString folder;       // <destination>/<name>
    QString projectFile;  // the new .bcproj
    Project project;      // with media paths pointing into Media/
    struct Copy { QString from; QString to; qint64 bytes = 0; };
    QList<Copy> copies;
    qint64 totalBytes = 0;
    QStringList missing;  // media files that were not found
    int unusedRemoved = 0;
};

// onlyUsed: leave out media that no clip on the timeline uses.
Plan plan(const Project &project, const QString &name, const QString &destination, bool onlyUsed);

// Copies the files and writes the project. `progress` gets bytes copied.
// A cancelled or failed run removes what it wrote.
bool run(const Plan &plan, QString *error = nullptr, const std::function<void(qint64)> &progress = {},
         const std::atomic_bool *cancel = nullptr);

} // namespace bc::Collector
