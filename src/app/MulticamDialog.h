#pragma once

#include <QDialog>
#include <QList>
#include <QUuid>

#include <atomic>
#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;

namespace bc {

class ProjectController;

// Sequence > Create multicamera sequence: the selected recordings of one
// event become the angles of a multicamera sequence, lined up by their sound
// (or all starting together), and one clip of it goes to the timeline.
class MulticamDialog : public QDialog
{
    Q_OBJECT
public:
    MulticamDialog(ProjectController *controller, const QList<QUuid> &mediaIds, QWidget *parent = nullptr);
    ~MulticamDialog() override;

    void setSyncBySound(bool on);
    // What "Create" does: by sound it first measures (in the background) and
    // creates when every angle was found; otherwise right away.
    void start();
    bool running() const { return m_running; }
    QUuid result() const { return m_result; }
    QList<double> offsets() const { return m_offsets; } // seconds, after measuring
    QList<bool> found() const { return m_found; }

signals:
    void finished(bool created);

protected:
    void reject() override;

private:
    void measured(const QList<std::vector<float>> &envelopes);
    void create();
    void showOffsets();

    ProjectController *m_ctl;
    QList<QUuid> m_media;
    QComboBox *m_sync;
    QComboBox *m_audio;
    QTreeWidget *m_list;
    QProgressBar *m_progress;
    QLabel *m_info;
    QPushButton *m_create;
    bool m_running = false;
    bool m_measured = false;
    QList<double> m_offsets;
    QList<bool> m_found;
    QUuid m_result;
    std::shared_ptr<std::atomic_bool> m_cancel;
};

} // namespace bc
