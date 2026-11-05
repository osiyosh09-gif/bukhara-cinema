#pragma once

#include "core/Effects.h"

#include <QUuid>
#include <QWidget>

class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QListWidget;
class QTimer;
class QToolButton;

namespace bc {

class ProjectController;
class TimelineView;

// "Effects" tab of the right panel (phase 8b): the picture effects of the
// video clip selected on the timeline, or the sound effects (phase 10b) of an
// audio clip; for linked clips a Picture / Sound switch picks the part. Add, switch on/off, reorder, remove
// and change values; every change goes through the controller (undoable).
class EffectsPanel : public QWidget
{
    Q_OBJECT
public:
    EffectsPanel(ProjectController *controller, TimelineView *view, QWidget *parent = nullptr);

    QUuid clip() const { return m_clip; }
    int currentEffect() const;

    static QString kindName(ClipEffect::Kind kind);
    static QString paramName(const QString &key);

protected:
    void changeEvent(QEvent *e) override;

private:
    void retranslate();
    void refresh();        // follows the selection and the timeline
    void refreshParams();  // the form of the current effect
    void addEffect();
    void fillKinds(); // the kinds for the current clip (picture or sound)
    void buildPluginForm(const ClipEffect &e); // VST 3 parameters (phase 15c)
    void updatePluginValues(const ClipEffect &e);

    ProjectController *m_ctl;
    TimelineView *m_view;
    QUuid m_clip;
    bool m_audio = false;       // m_clip is a sound clip
    bool m_wantAudio = false;   // linked picture + sound selected: show the sound
    int m_kindsFor = -1;        // what the kind list holds: 0 picture, 1 sound
    bool m_updating = false;
    // What the parameter form was built for (rebuilt only when it changes,
    // so a spin box being edited is not replaced).
    QUuid m_formClip;
    int m_formIndex = -1;
    int m_formKind = -1;
    QString m_formPlugin; // path|uid the form shows
    bool m_formCrashed = false; // the form shows "the plug-in stopped" (phase 15c-2)
    QTimer *m_crashWatch;       // notices a plug-in that stops while its form is shown

    QLabel *m_header;
    QWidget *m_target;          // Picture / Sound switch for linked clips
    QToolButton *m_targetVideo;
    QToolButton *m_targetAudio;
    QLabel *m_empty;
    QWidget *m_body;
    QComboBox *m_kind;
    QToolButton *m_add;
    QToolButton *m_findPlugins;
    QListWidget *m_list;
    QToolButton *m_up;
    QToolButton *m_down;
    QToolButton *m_remove;
    QGroupBox *m_paramsBox;
    QFormLayout *m_form;
    QLabel *m_note;
    QLabel *m_pluginNote; // plug-in missing, or not all parameters shown
};

} // namespace bc
