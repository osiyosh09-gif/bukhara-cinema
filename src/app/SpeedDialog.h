#pragma once

#include <QDialog>
#include <QUuid>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;

namespace bc {

class ProjectController;

// Clip > Speed / duration (Ctrl+R): speed in percent and the resulting
// length; changing one updates the other. Applies to the clip and its
// linked clips through the controller (one undo step).
class SpeedDialog : public QDialog
{
    Q_OBJECT
public:
    SpeedDialog(ProjectController *controller, const QUuid &clipId, QWidget *parent = nullptr);

    bool apply(); // what OK does

private:
    void speedEdited();
    void durationEdited();

    ProjectController *m_ctl;
    QUuid m_clip;
    double m_fps = 25;
    qint64 m_sourceFrames = 0; // source frames the clip uses now
    bool m_lengthTouched = false;
    QDoubleSpinBox *m_speed;
    QDoubleSpinBox *m_duration;
    QCheckBox *m_ripple;
    QLabel *m_note;
};

} // namespace bc
