// Export for other editors (phase 15a): part of ProjectController.
#include "app/ProjectController.h"

#include <QFileInfo>
#include <QSaveFile>

namespace bc {

Interchange::Result ProjectController::interchangeResult(Interchange::Format format) const
{
    return Interchange::write(m_project, format);
}

bool ProjectController::exportInterchange(Interchange::Format format, const QString &path, QString *error,
                                          Interchange::Result *result) const
{
    const Interchange::Result r = Interchange::write(m_project, format);
    if (result)
        *result = r;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(r.data) != r.data.size() || !f.commit()) {
        if (error)
            *error = tr("Could not write the file %1: %2").arg(QFileInfo(path).fileName(), f.errorString());
        return false;
    }
    return true;
}

} // namespace bc
