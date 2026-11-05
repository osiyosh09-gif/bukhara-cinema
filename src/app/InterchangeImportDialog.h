#pragma once

#include "core/Interchange.h"

#include <QDialog>
#include <QStringList>

class QLabel;
class QPushButton;
class QRadioButton;

namespace bc {

class ProjectController;

// File > Import from other editors (phase 15b): shows what a Final Cut Pro
// XML, OpenTimelineIO or EDL file holds and what will not be imported, then
// imports it as one nested clip or straight onto the open sequence.
class InterchangeImportDialog : public QDialog
{
    Q_OBJECT
public:
    InterchangeImportDialog(ProjectController *controller, const QString &path, QWidget *parent = nullptr);

    bool isReadable() const { return m_readable; }
    QStringList reportLines() const { return m_lines; } // the "not imported" list as shown
    void setNested(bool nested);
    // What the Import button does; returns the number of clips (0 = failed).
    int importNow(QString *error = nullptr);
    int importedClips() const { return m_imported; }

    static QString lossText(Interchange::ImportLoss loss, int count);

private:
    ProjectController *m_ctl;
    QString m_path;
    bool m_readable = false;
    QStringList m_lines;
    QLabel *m_summary;
    QLabel *m_report;
    QRadioButton *m_asNest;
    QRadioButton *m_direct;
    QPushButton *m_import;
    int m_imported = 0;
};

} // namespace bc
