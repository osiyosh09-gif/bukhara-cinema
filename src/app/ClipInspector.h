#pragma once

#include <QUuid>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPlainTextEdit;
class QSlider;
class QSpinBox;
class QToolButton;

namespace bc {

class ProjectController;
class TimelineView;

// "Clip" tab of the right panel: settings of the clip selected on the
// timeline. Titles: text, size, colour, place, box. Audio clips: volume and
// fades. Every change goes through the controller (undoable).
class ClipInspector : public QWidget
{
    Q_OBJECT
public:
    ClipInspector(ProjectController *controller, TimelineView *view, QWidget *parent = nullptr);

    QUuid titleClip() const { return m_title; }
    QUuid audioClip() const { return m_audio; }

    void focusTitleText();
    void focusGain();

protected:
    void changeEvent(QEvent *e) override;

private:
    void retranslate();
    void refresh();        // follows the selection and the timeline
    void applyTitle();     // widgets -> title clip
    void applyAudio();     // widgets -> audio clip
    void pickColor();
    void showColor(const QString &color);

    ProjectController *m_ctl;
    TimelineView *m_view;
    QUuid m_title;
    QUuid m_audio;
    QUuid m_video;
    bool m_updating = false;

    QLabel *m_header;
    QLabel *m_empty;
    QLabel *m_videoNote;
    QGroupBox *m_titleBox;
    QLabel *m_textLabel;
    QPlainTextEdit *m_text;
    QLabel *m_sizeLabel;
    QSpinBox *m_size;
    QLabel *m_colorLabel;
    QToolButton *m_color;
    QString m_colorValue;
    QLabel *m_posLabel;
    QComboBox *m_position;
    QCheckBox *m_box;
    QGroupBox *m_audioBox;
    QLabel *m_gainLabel;
    QDoubleSpinBox *m_gain;
    QSlider *m_gainSlider;
    QLabel *m_fadeInLabel;
    QDoubleSpinBox *m_fadeIn;
    QLabel *m_fadeOutLabel;
    QDoubleSpinBox *m_fadeOut;
};

} // namespace bc
