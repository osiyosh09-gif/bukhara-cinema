#pragma once

#include "core/MediaProbe.h"
#include "engine/Exporter.h"

#include <optional>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace bc {

class ExportQueue;
class ProjectController;

// File > Export: writes the whole sequence to an MP4 file (H.264 or
// MPEG-4 Part 2 video, AAC sound) with progress and cancel.
class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    // With a queue the dialog also offers "Add to queue" (phase 13a).
    explicit ExportDialog(ProjectController *controller, QWidget *parent = nullptr, ExportQueue *queue = nullptr);

    QString path() const;
    void setPath(const QString &path);
    // Starts the export; false (and a message in the dialog) when it cannot.
    bool startExport();
    // Puts the export in the queue with a copy of the project as it is now.
    bool addToQueue();

protected:
    void reject() override;

private:
    void browse();
    void updateInfo();
    void onProgress(qint64 frame, qint64 total, qint64 remainingMs);
    void onFinished(bool ok, const QString &error);
    void setRunning(bool running);
    QString defaultPath() const;
    // Settings from the widgets; nothing (and a message) when the file name is not usable.
    std::optional<ExportSettings> settings(bool askReplace);

    ProjectController *m_ctl;
    QList<MediaProbe::VideoEncoder> m_encoders;
    QLineEdit *m_path;
    QPushButton *m_browse;
    QComboBox *m_quality;
    QComboBox *m_encoder;
    QCheckBox *m_burnSubs;
    QCheckBox *m_writeSrt;
    QLabel *m_info;
    QLabel *m_warning;
    QProgressBar *m_progress;
    QLabel *m_status;
    QPushButton *m_start;
    QPushButton *m_close;
    QPushButton *m_openFolder;
    QPushButton *m_addQueue;
    ExportQueue *m_queue;
    bool m_mine = false; // the running export was started here (not by the queue)
};

} // namespace bc
