#include "app/MonitorView.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace bc {

// Thin bar under the picture: the whole media (or sequence) from left to
// right, the playhead, the In/Out range and markers. Click or drag to seek.
class MonitorScrubBar : public QWidget
{
public:
    explicit MonitorScrubBar(MonitorView *monitor)
        : QWidget(monitor), m_monitor(monitor)
    {
        setObjectName(QStringLiteral("monitorScrub"));
        setFixedHeight(14);
        setCursor(Qt::PointingHandCursor);
    }

    qint64 duration = 0;
    qint64 position = 0;
    qint64 in = -1;
    qint64 out = -1;
    QList<qint64> markers;

protected:
    double x(qint64 f) const { return duration > 0 ? double(f) * width() / double(duration) : 0; }
    qint64 frameAt(int px) const
    {
        if (duration <= 0 || width() <= 0)
            return 0;
        return std::clamp<qint64>(qint64(double(px) * duration / width()), 0, duration - 1);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x26, 0x26, 0x26));
        if (duration <= 0)
            return;
        if (in >= 0 || out >= 0) {
            const qint64 a = in >= 0 ? in : 0;
            const qint64 b = (out >= 0 ? out : duration - 1) + 1;
            p.fillRect(QRectF(x(a), 3, std::max(1.0, x(b) - x(a)), height() - 6), QColor(0x4a, 0x6f, 0xa5));
        }
        p.setPen(QColor(0x5f, 0xc8, 0x6e));
        for (qint64 m : markers)
            p.drawLine(QPointF(x(m), 0), QPointF(x(m), height()));
        p.setPen(QPen(QColor(0xd1, 0xa5, 0x4a), 2));
        p.drawLine(QPointF(x(position), 0), QPointF(x(position), height()));
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        emit m_monitor->activated();
        emit m_monitor->seekRequested(frameAt(int(e->position().x())));
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton)
            emit m_monitor->seekRequested(frameAt(int(e->position().x())));
    }

private:
    MonitorView *m_monitor;
};

MonitorView::MonitorView(std::function<QString()> title, QWidget *parent)
    : QWidget(parent)
    , m_title(std::move(title))
    , m_titleLabel(new QLabel(this))
    , m_screen(new QLabel(this))
    , m_timecode(new QLabel(QStringLiteral("00:00:00:00"), this))
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    m_titleLabel->setStyleSheet(QStringLiteral("color:#cfcfcf; font-weight:600; padding:2px 4px;"));
    lay->addWidget(m_titleLabel);

    m_screen->setAlignment(Qt::AlignCenter);
    m_screen->setWordWrap(true);
    m_screen->setMinimumSize(160, 90);
    m_screen->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_screen->setStyleSheet(QStringLiteral("background:#000000; color:#5a5a5a; border:1px solid #151515;"));
    lay->addWidget(m_screen, 1);

    m_scrub = new MonitorScrubBar(this);
    m_scrub->hide();
    lay->addWidget(m_scrub);

    auto *bar = new QHBoxLayout;
    m_bar = bar;
    m_timecode->setStyleSheet(QStringLiteral("color:#d1a54a; font-family:Consolas,monospace; font-size:13px;"));
    bar->addWidget(m_timecode);
    bar->addStretch();
    const QList<QStyle::StandardPixmap> icons = {QStyle::SP_MediaSkipBackward, QStyle::SP_MediaSeekBackward,
                                                 QStyle::SP_MediaPlay, QStyle::SP_MediaSeekForward,
                                                 QStyle::SP_MediaSkipForward};
    const QStringList names = {QStringLiteral("tcStart"), QStringLiteral("tcBack"), QStringLiteral("tcPlay"),
                               QStringLiteral("tcForward"), QStringLiteral("tcEnd")};
    for (int i = 0; i < icons.size(); ++i) {
        auto *b = new QToolButton(this);
        b->setObjectName(names[i]);
        b->setIcon(style()->standardIcon(icons[i]));
        b->setEnabled(false); // until setTransportEnabled(true)
        bar->addWidget(b);
        m_transport << b;
    }
    connect(m_transport[0], &QToolButton::clicked, this, &MonitorView::goToStart);
    connect(m_transport[1], &QToolButton::clicked, this, &MonitorView::stepBack);
    connect(m_transport[2], &QToolButton::clicked, this, &MonitorView::playPause);
    connect(m_transport[3], &QToolButton::clicked, this, &MonitorView::stepForward);
    connect(m_transport[4], &QToolButton::clicked, this, &MonitorView::goToEnd);
    m_extraPos = bar->count();
    bar->addStretch();
    lay->addLayout(bar);

    retranslate();
}

void MonitorView::setEmptyText(std::function<QString()> text)
{
    m_emptyText = std::move(text);
    retranslate();
}

void MonitorView::setDisabledReason(std::function<QString()> text)
{
    m_disabledReason = std::move(text);
    retranslate();
}

void MonitorView::setImage(const QImage &image)
{
    m_image = image;
    showImage();
}

void MonitorView::setTimecode(const QString &tc)
{
    m_timecode->setText(tc);
}

void MonitorView::setTransportEnabled(bool on)
{
    m_transportEnabled = on;
    for (auto *b : m_transport)
        b->setEnabled(on);
    retranslate();
}

void MonitorView::setPlaying(bool playing)
{
    m_playing = playing;
    m_transport[2]->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    retranslate();
}

void MonitorView::setDuration(qint64 frames)
{
    m_scrub->duration = std::max<qint64>(0, frames);
    m_scrub->setVisible(frames > 0);
    m_scrub->update();
}

void MonitorView::setPosition(qint64 frame)
{
    m_scrub->position = frame;
    m_scrub->update();
}

void MonitorView::setInOut(qint64 in, qint64 out)
{
    m_scrub->in = in;
    m_scrub->out = out;
    m_scrub->update();
}

void MonitorView::setMarkers(const QList<qint64> &frames)
{
    m_scrub->markers = frames;
    m_scrub->update();
}

void MonitorView::addButton(QAction *action, std::function<QString()> shortText)
{
    auto *b = new QToolButton(this);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setAutoRaise(true);
    b->setObjectName(QStringLiteral("mb_") + action->objectName());
    connect(b, &QToolButton::clicked, action, &QAction::trigger);
    auto sync = [b, action] {
        b->setEnabled(action->isEnabled());
        const QString keys = action->shortcut().toString(QKeySequence::NativeText);
        b->setToolTip(keys.isEmpty() ? action->text() : action->text() + QStringLiteral(" (") + keys + QLatin1Char(')'));
    };
    connect(action, &QAction::changed, b, sync);
    sync();
    m_bar->insertWidget(m_extraPos++, b);
    m_extra.append({b, std::move(shortText)});
    b->setText(m_extra.last().second());
}

void MonitorView::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    m_titleLabel->setStyleSheet(active ? QStringLiteral("color:#d1a54a; font-weight:600; padding:2px 4px;")
                                       : QStringLiteral("color:#cfcfcf; font-weight:600; padding:2px 4px;"));
}

void MonitorView::mousePressEvent(QMouseEvent *event)
{
    emit activated();
    QWidget::mousePressEvent(event);
}

void MonitorView::showImage()
{
    if (m_image.isNull()) {
        m_screen->setPixmap(QPixmap());
        m_screen->setText(m_emptyText ? m_emptyText() : tr("No video yet."));
        return;
    }
    const QSize area = m_screen->contentsRect().size().boundedTo(QSize(4096, 4096)) - QSize(2, 2);
    m_screen->setPixmap(QPixmap::fromImage(
        m_image.scaled(area.expandedTo(QSize(16, 9)), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void MonitorView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_image.isNull())
        showImage();
}

void MonitorView::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QWidget::changeEvent(event);
}

void MonitorView::retranslate()
{
    m_titleLabel->setText(m_title());
    for (const auto &e : m_extra)
        e.first->setText(e.second());
    showImage();
    if (!m_transportEnabled) {
        for (auto *b : m_transport)
            b->setToolTip(m_disabledReason ? m_disabledReason() : QString());
        return;
    }
    m_transport[0]->setToolTip(tr("Go to start (Home)"));
    m_transport[1]->setToolTip(tr("Previous frame (Left)"));
    m_transport[2]->setToolTip(m_playing ? tr("Pause (Space, K)") : tr("Play (Space, L)"));
    m_transport[3]->setToolTip(tr("Next frame (Right)"));
    m_transport[4]->setToolTip(tr("Go to end (End)"));
}

} // namespace bc
