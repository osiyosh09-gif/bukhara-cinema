#pragma once

#include "core/Collector.h"

#include <QDialog>

#include <atomic>
#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace bc {

class ProjectController;

// File > Collect project: copies the project and its media into one folder.
class CollectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CollectDialog(ProjectController *ctl, QWidget *parent = nullptr);

    void setDestination(const QString &folder);
    void start();
    bool isRunning() const { return m_running; }
    QString collectedProject() const { return m_result; } // after success

signals:
    void finished(bool ok);

protected:
    void reject() override;

private:
    void updatePlan();

    ProjectController *m_ctl;
    Collector::Plan m_plan;
    QLineEdit *m_dest;
    QCheckBox *m_onlyUsed;
    QLabel *m_info;
    QProgressBar *m_progress;
    QPushButton *m_start;
    QPushButton *m_close;
    QPushButton *m_openResult;
    bool m_running = false;
    QString m_result;
    std::shared_ptr<std::atomic_bool> m_cancel;
};

} // namespace bc
