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
    // Tooltip of the transport buttons while they are disabled.
    void setDisabledReason(std::function<QString()> text);
    void setImage(const QImage &image);
    QImage image() const { return m_image; }
    void setTimecode(const QString &tc);

    // Transport buttons work only when a monitor can play.
    void setTransportEnabled(bool on);
    void setPlaying(bool playing);

signals:
    void goToStart();
    void stepBack();
    void playPause();
    void stepForward();
    void goToEnd();

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void retranslate();
    void showImage();

    std::function<QString()> m_title;
    std::function<QString()> m_emptyText;
    std::function<QString()> m_disabledReason;
    QImage m_image;
    QLabel *m_titleLabel;
    QLabel *m_screen;
    QLabel *m_timecode;
    QList<QToolButton *> m_transport;
    bool m_transportEnabled = false;
    bool m_playing = false;
};

} // namespace bc
