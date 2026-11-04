#pragma once

#include "core/Scopes.h"

#include <QImage>
#include <QWidget>

class QComboBox;
class QLabel;
class QTimer;

namespace bc {

class ScopeView;

// "Scopes" window (phase 9c): waveform, RGB parade, vectorscope or histogram
// of the frame shown in the Program monitor. Works only while visible, and
// at most about 12 times a second during playback.
class ScopesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ScopesPanel(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);
    Scopes::Kind kind() const;
    void setKind(Scopes::Kind kind);
    QImage scopeImage() const; // the last drawn scope (for tests)
    void refreshNow();

protected:
    void changeEvent(QEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    void retranslate();
    void schedule();

    QComboBox *m_kind;
    ScopeView *m_view;
    QLabel *m_note;
    QTimer *m_timer;
    QImage m_frame;
};

} // namespace bc
