#pragma once

#include "core/AutoMulticam.h"

#include <QDialog>
#include <QList>
#include <QUuid>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;

namespace bc {

class ProjectController;

// Sequence > Automatic multicamera edit (phase 16a): cuts the selected
// multicamera clips between their cameras by who is speaking (each camera's
// own sound) or in a rhythm. One undo step; every cut can be changed after.
class AutoMulticamDialog : public QDialog
{
    Q_OBJECT
public:
    AutoMulticamDialog(ProjectController *controller, const QList<QUuid> &selection, QWidget *parent = nullptr);
    ~AutoMulticamDialog() override;

    AutoMulticam::Settings settings() const;
    int apply(); // what the Edit button does; returns the number of cuts

private:
    void refresh();

    ProjectController *m_ctl;
    QList<QUuid> m_clips;
    QComboBox *m_mode;
    QComboBox *m_wide;
    QDoubleSpinBox *m_minShot;
    QDoubleSpinBox *m_maxShot;
    QDoubleSpinBox *m_margin;
    QProgressBar *m_progress;
    QLabel *m_result;
    QLabel *m_warning;
    QPushButton *m_apply;
};

} // namespace bc
