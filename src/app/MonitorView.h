#pragma once

#include <QImage>
#include <QWidget>
#include <functional>

class QAction;
class QHBoxLayout;
class QLabel;
class QToolButton;

namespace bc {

class MonitorScrubBar;

// Source / Program monitor: the picture, a position bar (with In/Out range
// and markers) and transport buttons. It only shows; the controller plays.
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

    // Position bar: length in frames (0 hides it), position, In/Out range
    // (-1 = not set) and marker frames.
    void setDuration(qint64 frames);
    void setPosition(qint64 frame);
    void setInOut(qint64 in, qint64 out);
    void setMarkers(const QList<qint64> &frames);
    // Extra buttons after the transport (Mark In, Insert, ...): they trigger
    // `action` and follow its enabled state, with a short label of their own.
    void addButton(QAction *action, std::function<QString()> shortText);
    // The monitor that J/K/L and Space act on has a highlighted title.
    void setActive(bool active);
    bool isActive() const { return m_active; }

signals:
    void goToStart();
    void stepBack();
    void playPause();
    void stepForward();
    void goToEnd();
    void seekRequested(qint64 frame);
    void activated(); // clicked

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

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
    MonitorScrubBar *m_scrub;
    QHBoxLayout *m_bar;
    int m_extraPos = 0;
    QList<QPair<QToolButton *, std::function<QString()>>> m_extra;
    bool m_transportEnabled = false;
    bool m_playing = false;
    bool m_active = false;
};

} // namespace bc
