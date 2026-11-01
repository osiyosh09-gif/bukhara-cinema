#include "app/MonitorView.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace bc {

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

    auto *bar = new QHBoxLayout;
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

void MonitorView::showImage()
{
    if (m_image.isNull()) {
        m_screen->setPixmap(QPixmap());
        m_screen->setText(m_emptyText ? m_emptyText() : tr("No video yet.\nSource monitor playback is planned for phase 6."));
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
    showImage();
    if (!m_transportEnabled) {
        for (auto *b : m_transport)
            b->setToolTip(m_disabledReason ? m_disabledReason()
                                           : tr("Source monitor playback is planned for phase 6"));
        return;
    }
    m_transport[0]->setToolTip(tr("Go to start (Home)"));
    m_transport[1]->setToolTip(tr("Previous frame (Left)"));
    m_transport[2]->setToolTip(m_playing ? tr("Pause (Space)") : tr("Play (Space)"));
    m_transport[3]->setToolTip(tr("Next frame (Right)"));
    m_transport[4]->setToolTip(tr("Go to end (End)"));
}

} // namespace bc
