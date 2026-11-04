#pragma once

#include <QImage>
#include <QUuid>
#include <QWidget>

namespace bc {

class ProjectController;

// Window > Multicamera monitor (phase 12b): every angle of the multicamera
// clip at the playhead in a grid. The angle shown in the Program monitor has
// a red frame (the sound angle a speaker mark); click a cell, or press 1-9
// (Alt+1-9 for sound), to switch from the playhead on, also while playing.
// Renders only while visible.
class MulticamMonitor : public QWidget
{
    Q_OBJECT
public:
    explicit MulticamMonitor(ProjectController *controller, QWidget *parent = nullptr);

    QImage image() const { return m_image; }
    QUuid clip() const { return m_clip; }
    // Angle under a point of this widget, or -1.
    int angleAt(const QPoint &pos) const;
    QRect imageRect() const; // where the grid is drawn

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    void request();
    int angleCount() const;

    ProjectController *m_ctl;
    QImage m_image;
    QUuid m_clip;
};

} // namespace bc
