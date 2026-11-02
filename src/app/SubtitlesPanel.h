#pragma once

#include <QUuid>
#include <QWidget>

class QAction;
class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QToolButton;
class QTreeWidget;

namespace bc {

class ProjectController;

// Right "Subtitles" tab (phase 11): the subtitles of the open sequence in
// time order, the selected one's time and text, the look of all of them,
// and .srt import/export. Every change goes through the controller.
class SubtitlesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit SubtitlesPanel(ProjectController *controller, QWidget *parent = nullptr);

    QAction *addAction_() const { return m_addAct; }
    QAction *importAction() const { return m_importAct; }
    QAction *exportAction() const { return m_exportAct; }
    QUuid current() const { return m_current; }
    void select(const QUuid &id);

    // What the menu actions do; the file dialogs are skipped when a path is given.
    int importFile(const QString &path = {});
    bool exportFile(const QString &path = {});

protected:
    void changeEvent(QEvent *e) override;

private:
    void retranslate();
    void rebuild();
    void showCurrent();
    void followPlayhead(qint64 frame);
    void timingEdited();
    void styleEdited();
    void pickColor();

    ProjectController *m_ctl;
    QUuid m_current;
    bool m_updating = false;
    QAction *m_addAct;
    QAction *m_deleteAct;
    QAction *m_importAct;
    QAction *m_exportAct;
    QToolButton *m_addBtn;
    QToolButton *m_deleteBtn;
    QToolButton *m_importBtn;
    QToolButton *m_exportBtn;
    QTreeWidget *m_list;
    QLabel *m_empty;
    QGroupBox *m_editBox;
    QLabel *m_startLabel;
    QLabel *m_lengthLabel;
    QDoubleSpinBox *m_start;
    QDoubleSpinBox *m_length;
    QPlainTextEdit *m_text;
    QGroupBox *m_styleBox;
    QCheckBox *m_show;
    QLabel *m_sizeLabel;
    QLabel *m_marginLabel;
    QSpinBox *m_size;
    QSpinBox *m_margin;
    QToolButton *m_color;
    QCheckBox *m_box;
};

} // namespace bc
