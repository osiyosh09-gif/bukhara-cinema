#pragma once

#include <QImage>
#include <QWidget>
#include <functional>

class QLabel;
class QToolButton;

namespace bc {

// Source / Program monitor. It can show a still frame (the Program monitor
// shows the frame at the timeline playhead). Real-time playback arrives in a
// later step; until then the transport buttons are disabled.
class MonitorView : public QWidget
{
    Q_OBJECT
public:
    MonitorView(std::function<QString()> title, QWidget *parent = nullptr);

    // Text shown when there is no image (re-evaluated on language change).
    void setEmptyText(std::function<QString()> text);
    void setImage(const QImage &image);
    QImage image() const { return m_image; }
    void setTimecode(const QString &tc);

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void retranslate();
    void showImage();

    std::function<QString()> m_title;
    std::function<QString()> m_emptyText;
    QImage m_image;
    QLabel *m_titleLabel;
    QLabel *m_screen;
    QLabel *m_timecode;
    QList<QToolButton *> m_transport;
};

} // namespace bc
