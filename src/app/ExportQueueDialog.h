#pragma once

#include <QDialog>
#include <QUuid>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace bc {

class ExportQueue;

// File > Export queue: the waiting, running and finished exports with
// Start, Stop, Remove, Try again and Clear finished.
class ExportQueueDialog : public QDialog
{
    Q_OBJECT
public:
    ExportQueueDialog(ExportQueue *queue, QWidget *parent = nullptr);

    QUuid currentJob() const;
    void selectJob(const QUuid &id);

private:
    void rebuild();
    void updateRow(const QUuid &id);
    void updateButtons();

    ExportQueue *m_queue;
    QTreeWidget *m_list;
    QLabel *m_summary;
    QPushButton *m_start;
    QPushButton *m_stop;
    QPushButton *m_remove;
    QPushButton *m_retry;
    QPushButton *m_clear;
};

} // namespace bc
