#pragma once

#include <QWidget>
#include <functional>

class QLabel;
class QToolButton;

namespace bc {

// Source / Program monitor shell. Playback arrives in phase 2:
// until then the screen is empty and the transport buttons are disabled.
class MonitorView : public QWidget
{
    Q_OBJECT
public:
    MonitorView(std::function<QString()> title, QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslate();

    std::function<QString()> m_title;
    QLabel *m_titleLabel;
    QLabel *m_screen;
    QLabel *m_timecode;
    QList<QToolButton *> m_transport;
};

} // namespace bc
