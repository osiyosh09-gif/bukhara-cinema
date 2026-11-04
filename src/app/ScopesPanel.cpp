#include "app/ScopesPanel.h"

#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

namespace bc {

// Paints the scope image to fill the widget, with level labels.
class ScopeView : public QWidget
{
public:
    explicit ScopeView(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(200, 140);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    QImage image;
    Scopes::Kind kind = Scopes::Kind::Waveform;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(18, 18, 18));
        if (!image.isNull())
            p.drawImage(rect(), image);
        if (kind == Scopes::Kind::Waveform || kind == Scopes::Kind::Parade) {
            p.setPen(QColor(150, 150, 150));
            QFont f = font();
            f.setPointSizeF(f.pointSizeF() * 0.8);
            p.setFont(f);
            for (int pct : {0, 50, 100}) {
                const int y = (height() - 1) - pct * (height() - 1) / 100;
                p.drawText(QRect(2, std::clamp(y - 12, 0, height() - 14), 40, 14), Qt::AlignLeft | Qt::AlignVCenter,
                           QString::number(pct));
            }
        }
    }
};

ScopesPanel::ScopesPanel(QWidget *parent)
    : QWidget(parent)
    , m_kind(new QComboBox)
    , m_view(new ScopeView)
    , m_note(new QLabel)
    , m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("scopesPanel"));
    m_kind->setObjectName(QStringLiteral("scopeKind"));
    m_view->setObjectName(QStringLiteral("scopeView"));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->addWidget(m_kind);
    lay->addWidget(m_view, 1);
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    lay->addWidget(m_note);

    m_timer->setSingleShot(true);
    m_timer->setInterval(80);
    connect(m_timer, &QTimer::timeout, this, &ScopesPanel::refreshNow);
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(QStringLiteral("scopes/kind"), m_kind->currentData().toInt());
        refreshNow();
    });
    retranslate();
    const int saved = QSettings().value(QStringLiteral("scopes/kind"), 0).toInt();
    m_kind->setCurrentIndex(std::max(0, m_kind->findData(saved)));
}

Scopes::Kind ScopesPanel::kind() const
{
    return Scopes::Kind(m_kind->currentData().toInt());
}

void ScopesPanel::setKind(Scopes::Kind kind)
{
    m_kind->setCurrentIndex(m_kind->findData(int(kind)));
}

QImage ScopesPanel::scopeImage() const
{
    return m_view->image;
}

void ScopesPanel::setFrame(const QImage &frame)
{
    m_frame = frame;
    schedule();
}

void ScopesPanel::schedule()
{
    if (isVisible() && !m_timer->isActive())
        m_timer->start();
}

void ScopesPanel::refreshNow()
{
    m_timer->stop();
    m_view->kind = kind();
    const QSize size = kind() == Scopes::Kind::Vectorscope ? QSize(256, 256) : QSize(384, 256);
    m_view->image = m_frame.isNull() ? QImage() : Scopes::render(m_frame, kind(), size);
    m_view->update();
}

void ScopesPanel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    refreshNow(); // frames that arrived while hidden were not drawn
}

void ScopesPanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslate();
    QWidget::changeEvent(e);
}

void ScopesPanel::retranslate()
{
    const QSignalBlocker b(m_kind);
    const int cur = m_kind->currentIndex();
    m_kind->clear();
    m_kind->addItem(tr("Waveform (brightness)"), int(Scopes::Kind::Waveform));
    m_kind->addItem(tr("RGB parade"), int(Scopes::Kind::Parade));
    m_kind->addItem(tr("Vectorscope (hue and saturation)"), int(Scopes::Kind::Vectorscope));
    m_kind->addItem(tr("Histogram"), int(Scopes::Kind::Histogram));
    m_kind->setCurrentIndex(std::max(0, cur));
    m_note->setText(tr("Shows the picture in the Program monitor. Waveform and parade: 0 = black, 100 = white. "
                       "Vectorscope: squares are pure colours, the dashed line is skin tone."));
}

} // namespace bc
