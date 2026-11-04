#pragma once

#include <QDialog>
#include <QList>
#include <QUuid>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;

namespace bc {

class ProjectController;

// Clip > Detect scene changes (phase 14a): analyses the selected video clips
// on this computer and splits them at every new shot, or marks the shots.
class SceneDialog : public QDialog
{
    Q_OBJECT
public:
    SceneDialog(ProjectController *controller, const QList<QUuid> &clipIds, QWidget *parent = nullptr);
    ~SceneDialog() override;

    double sensitivity() const;
    int apply(); // what the Split / Add markers button does; returns the number of cuts

private:
    void refresh();

    ProjectController *m_ctl;
    QList<QUuid> m_clips;
    QSlider *m_sensitivity;
    QComboBox *m_mode;
    QProgressBar *m_progress;
    QLabel *m_result;
    QPushButton *m_apply;
};

} // namespace bc
