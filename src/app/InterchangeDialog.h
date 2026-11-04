#pragma once

#include "core/Interchange.h"

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QPushButton;

namespace bc {

class ProjectController;

// File > Export for other editors (phase 15a): writes the main sequence as
// Final Cut Pro 7 XML, OpenTimelineIO or EDL, and lists beforehand what the
// chosen format will not carry.
class InterchangeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit InterchangeDialog(ProjectController *controller, QWidget *parent = nullptr);

    Interchange::Format format() const;
    void setFormat(Interchange::Format format);
    // The lines of the "will not be transferred" list, as shown.
    QStringList reportLines() const { return m_lines; }
    // Writes the file without asking for a name (the Export button asks).
    bool exportTo(const QString &path, QString *error = nullptr);
    QString savedPath() const { return m_saved; }

    static QString lossText(Interchange::Loss loss, int count);

private:
    void refresh();
    void chooseAndExport();

    ProjectController *m_ctl;
    QComboBox *m_format;
    QLabel *m_report;
    QLabel *m_info;
    QPushButton *m_export;
    QStringList m_lines;
    QString m_saved;
};

} // namespace bc
