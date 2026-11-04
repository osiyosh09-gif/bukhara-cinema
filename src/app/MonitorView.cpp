#include "app/MonitorView.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
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
    for (auto icon : icons) {
        auto *b = new QToolButton(this);
        b->setIcon(style()->standardIcon(icon));
        b->setEnabled(false); // playback is planned for phase 2
        bar->addWidget(b);
        m_transport << b;
    }
    bar->addStretch();
    lay->addLayout(bar);

    retranslate();
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
    m_screen->setText(tr("No video yet.\nPlayback is planned for phase 2."));
    for (auto *b : m_transport)
        b->setToolTip(tr("Playback is planned for phase 2"));
}

} // namespace bc
