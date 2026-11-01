#pragma once

#include "core/Effects.h"

#include <QUuid>
#include <QWidget>

class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QListWidget;
class QToolButton;

namespace bc {

class ProjectController;
class TimelineView;

// "Effects" tab of the right panel (phase 8b): the picture effects of the
// video clip selected on the timeline. Add, switch on/off, reorder, remove
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

    ProjectController *m_ctl;
    TimelineView *m_view;
    QUuid m_clip;
    bool m_updating = false;
    // What the parameter form was built for (rebuilt only when it changes,
    // so a spin box being edited is not replaced).
    QUuid m_formClip;
    int m_formIndex = -1;
    int m_formKind = -1;

    QLabel *m_header;
    QLabel *m_empty;
    QWidget *m_body;
    QComboBox *m_kind;
    QToolButton *m_add;
    QListWidget *m_list;
    QToolButton *m_up;
    QToolButton *m_down;
    QToolButton *m_remove;
    QGroupBox *m_paramsBox;
    QFormLayout *m_form;
    QLabel *m_note;
};

} // namespace bc
