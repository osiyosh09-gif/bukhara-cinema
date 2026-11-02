#pragma once

#include <QDialog>

class QLabel;
class QPushButton;

namespace bc {

class ProjectController;

// File > Media cache: sizes of thumbnails, waveforms, proxy and preview files, and
// buttons to delete them (they are made again when needed).
class CacheDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CacheDialog(ProjectController *ctl, QWidget *parent = nullptr);

    void refresh();
    void clearThumbnails();
    void clearWaveforms();
    void clearProxies();
    void clearPreviews();

private:
    ProjectController *m_ctl;
    QLabel *m_sizes[4];
    QPushButton *m_clear[4];
    QLabel *m_total;
};

} // namespace bc
