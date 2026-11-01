#pragma once

#include <QHash>
#include <QUuid>
#include <QWidget>

class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QSlider;
class QToolButton;

namespace bc {

class ProjectController;
class TimelineView;

// "Colour" tab of the right panel (phase 9a): basic colour correction of the
// video clip selected on the timeline. Every change goes through the
// controller (undoable; dragging a slider is one step).
class ColorPanel : public QWidget
{
    Q_OBJECT
public:
    ColorPanel(ProjectController *controller, TimelineView *view, QWidget *parent = nullptr);

    QUuid clip() const { return m_clip; }
    static QString valueName(const QString &key);

protected:
    void changeEvent(QEvent *e) override;

private:
    struct Row
    {
        QLabel *label;
        QSlider *slider;
        QDoubleSpinBox *spin;
        double step;
    };

    void retranslate();
    void refresh();
    void apply(const QString &key, double value);
    void chooseLut();

    ProjectController *m_ctl;
    TimelineView *m_view;
    QUuid m_clip;
    bool m_updating = false;

    QLabel *m_header;
    QLabel *m_empty;
    QWidget *m_body;
    QGroupBox *m_balanceBox;
    QGroupBox *m_toneBox;
    QGroupBox *m_colorBox;
    QGroupBox *m_lutBox;
    QLabel *m_lutName;
    QToolButton *m_lutChoose;
    QToolButton *m_lutRemove;
    QToolButton *m_reset;
    QLabel *m_note;
    QHash<QString, Row> m_rows;
};

} // namespace bc
