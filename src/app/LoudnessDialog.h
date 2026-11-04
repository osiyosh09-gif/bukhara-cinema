#pragma once

#include <QDialog>
#include <QList>
#include <QUuid>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTreeWidget;

namespace bc {

class ProjectController;

// Clip > Normalize loudness: measures the selected audio clips (LUFS, as in
// EBU R128) and sets their volume so they reach a target loudness without
// peaks above a ceiling. One undo step.
class LoudnessDialog : public QDialog
{
    Q_OBJECT
public:
    LoudnessDialog(ProjectController *controller, const QList<QUuid> &selection, QWidget *parent = nullptr);

    int apply(); // what Apply does; returns the number of clips changed
    bool measured() const; // every clip measured (or found silent)

private:
    double target() const;
    void refresh();

    ProjectController *m_ctl;
    QList<QUuid> m_clips;
    QComboBox *m_preset;
    QDoubleSpinBox *m_target;
    QDoubleSpinBox *m_ceiling;
    QCheckBox *m_together;
    QTreeWidget *m_list;
    QLabel *m_status;
    QPushButton *m_apply;
};

} // namespace bc
