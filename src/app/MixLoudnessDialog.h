#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;

namespace bc {

class ProjectController;

// Sequence > Mix loudness (phase 13c): measures the loudness of the whole
// mixed sound of the open sequence (LUFS, as in EBU R128) and sets the
// master volume so it reaches a target without peaks above a ceiling.
class MixLoudnessDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MixLoudnessDialog(ProjectController *controller, QWidget *parent = nullptr);
    ~MixLoudnessDialog() override;

    bool apply(); // what Apply does
    void measure();

private:
    double target() const;
    void refresh();

    ProjectController *m_ctl;
    QComboBox *m_preset;
    QDoubleSpinBox *m_target;
    QDoubleSpinBox *m_ceiling;
    QProgressBar *m_progress;
    QLabel *m_result;
    QLabel *m_status;
    QPushButton *m_measure;
    QPushButton *m_apply;
};

} // namespace bc
