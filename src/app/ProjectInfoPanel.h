#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace bc {

class ProjectController;

// "Project files" tab: project file, sequence format, media statistics.
class ProjectInfoPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ProjectInfoPanel(ProjectController *controller, QWidget *parent = nullptr);

signals:
    void sequenceSettingsRequested();

protected:
    void changeEvent(QEvent *event) override;

private:
    void refresh();

    ProjectController *m_ctl;
    QLabel *m_text;
    QPushButton *m_seqBtn;
};

} // namespace bc
