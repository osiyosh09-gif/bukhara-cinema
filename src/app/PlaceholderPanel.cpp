#include "app/PlaceholderPanel.h"

#include "app/FeatureStatus.h"

#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

namespace bc {

PlaceholderPanel::PlaceholderPanel(TextFn title, int phase, QWidget *parent)
    : QWidget(parent)
    , m_title(std::move(title))
    , m_phase(phase)
    , m_titleLabel(new QLabel(this))
    , m_noteLabel(new QLabel(this))
{
    auto *lay = new QVBoxLayout(this);
    lay->addStretch();
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(QStringLiteral("color:#9a9a9a; font-weight:600;"));
    m_noteLabel->setAlignment(Qt::AlignCenter);
    m_noteLabel->setWordWrap(true);
    m_noteLabel->setStyleSheet(QStringLiteral("color:#6e6e6e;"));
    lay->addWidget(m_titleLabel);
    lay->addWidget(m_noteLabel);
    lay->addStretch();
    retranslate();
}

QString PlaceholderPanel::titleText() const
{
    return m_titleLabel->text();
}

void PlaceholderPanel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QWidget::changeEvent(event);
}

void PlaceholderPanel::retranslate()
{
    m_titleLabel->setText(m_title());
    m_noteLabel->setText(tr("Not working yet. Planned for phase %1.").arg(m_phase));
}

// ---------------------------------------------------------------------------

TimelinePlaceholder::TimelinePlaceholder(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void TimelinePlaceholder::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int headerW = 90;
    const int rulerH = 24;
    p.fillRect(rect(), QColor(0x1c, 0x1c, 0x1c));

    // Ruler
    p.fillRect(QRect(0, 0, width(), rulerH), QColor(0x2a, 0x2a, 0x2a));
    p.setPen(QColor(0x55, 0x55, 0x55));
    for (int x = headerW, i = 0; x < width(); x += 20, ++i) {
        const int h = (i % 5 == 0) ? 10 : 5;
        p.drawLine(x, rulerH - h, x, rulerH - 1);
    }

    const QStringList tracks = {QStringLiteral("V2"), QStringLiteral("V1"),
                                QStringLiteral("A1"), QStringLiteral("A2")};
    const int avail = height() - rulerH - 30;
    const int trackH = qMax(22, avail / int(tracks.size()));
    int y = rulerH;
    for (int i = 0; i < tracks.size(); ++i) {
        const bool video = tracks[i].startsWith(QLatin1Char('V'));
        p.fillRect(QRect(0, y, headerW, trackH - 1),
                   video ? QColor(0x2f, 0x33, 0x3a) : QColor(0x2f, 0x3a, 0x33));
        p.fillRect(QRect(headerW, y, width() - headerW, trackH - 1), QColor(0x22, 0x22, 0x22));
        p.setPen(QColor(0xb0, 0xb0, 0xb0));
        p.drawText(QRect(8, y, headerW - 8, trackH - 1), Qt::AlignVCenter | Qt::AlignLeft, tracks[i]);
        y += trackH;
    }

    p.setPen(QColor(0x6e, 0x6e, 0x6e));
    p.drawText(QRect(headerW, y, width() - headerW, height() - y), Qt::AlignCenter,
               tr("Timeline editing is planned for phase 2. This is an empty layout only."));
}

} // namespace bc
