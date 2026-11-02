#include "app/MulticamMonitor.h"

#include "app/ProjectController.h"
#include "core/Multicam.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>

namespace bc {

MulticamMonitor::MulticamMonitor(ProjectController *controller, QWidget *parent)
    : QWidget(parent)
    , m_ctl(controller)
{
    setObjectName(QStringLiteral("multicamMonitor"));
    setMinimumSize(240, 160);
    setMouseTracking(true);
    connect(m_ctl, &ProjectController::multicamGridReady, this, [this](const QImage &img, const QUuid &clip) {
        m_image = img;
        m_clip = clip;
        update();
    });
    connect(m_ctl, &ProjectController::playheadChanged, this, &MulticamMonitor::request);
    connect(m_ctl, &ProjectController::timelineChanged, this, &MulticamMonitor::request);
    connect(m_ctl, &ProjectController::openSequenceChanged, this, &MulticamMonitor::request);
    connect(m_ctl, &ProjectController::projectReset, this, &MulticamMonitor::request);
}

void MulticamMonitor::request()
{
    if (isVisible())
        m_ctl->requestMulticamGrid();
    update(); // the red frame follows the timeline at once
}

void MulticamMonitor::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    m_ctl->requestMulticamGrid();
}

void MulticamMonitor::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        update();
    QWidget::changeEvent(e);
}

int MulticamMonitor::angleCount() const
{
    const TimelineClip *c = m_ctl->timeline().clip(m_clip);
    return c ? int(m_ctl->multicamAngles(c->nestId, Timeline::Kind::Video).size()) : 0;
}

QRect MulticamMonitor::imageRect() const
{
    if (m_image.isNull())
        return {};
    QSize s = m_image.size();
    s.scale(size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
}

int MulticamMonitor::angleAt(const QPoint &pos) const
{
    const QRect r = imageRect();
    if (r.isEmpty() || !r.contains(pos))
        return -1;
    return Multicam::angleAt(QPointF(double(pos.x() - r.x()) / r.width(), double(pos.y() - r.y()) / r.height()),
                             angleCount());
}

void MulticamMonitor::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x10, 0x10, 0x10));
    const Timeline &tl = m_ctl->timeline();
    const TimelineClip *c = tl.clip(m_clip);
    if (m_image.isNull() || !c) {
        p.setPen(QColor(0x8a, 0x8a, 0x8a));
        p.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
                   m_ctl->canPlay() ? tr("Move the playhead onto a multicamera clip to see all its cameras here.")
                                    : tr("The video engine is not available."));
        return;
    }
    const QRect r = imageRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, m_image);

    const QStringList names = m_ctl->multicamAngles(c->nestId, Timeline::Kind::Video);
    const int n = int(names.size());
    // The angles shown and heard at the playhead (they change as soon as the timeline does).
    const TimelineClip *shown = tl.clip(m_ctl->multicamClipAt(m_ctl->playhead()));
    const TimelineClip *heard = tl.clip(m_ctl->multicamClipAt(m_ctl->playhead(), Timeline::Kind::Audio));
    const int picture = shown && shown->nestId == c->nestId ? shown->angle : -1;
    const int sound = heard && heard->nestId == c->nestId ? heard->angle : -1;
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    for (int i = 0; i < n; ++i) {
        const QRectF fr = Multicam::cell(i, n);
        const QRect cellRect(r.x() + int(fr.x() * r.width()), r.y() + int(fr.y() * r.height()),
                             int(fr.width() * r.width()), int(fr.height() * r.height()));
        const QString label = sound == i ? QStringLiteral("%1  ♪  %2").arg(i + 1).arg(names[i])
                                         : QStringLiteral("%1  %2").arg(i + 1).arg(names[i]);
        const QRect tag(cellRect.x() + 4, cellRect.y() + 4, std::min(cellRect.width() - 8, p.fontMetrics().horizontalAdvance(label) + 12),
                        p.fontMetrics().height() + 4);
        p.fillRect(tag, QColor(0, 0, 0, 170));
        p.setPen(Qt::white);
        p.drawText(tag.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(label, Qt::ElideRight, tag.width() - 12));
        if (i == picture) {
            p.setPen(QPen(QColor(0xe0, 0x30, 0x30), 3));
            p.setBrush(Qt::NoBrush);
            p.drawRect(cellRect.adjusted(1, 1, -2, -2));
        }
    }
}

void MulticamMonitor::mousePressEvent(QMouseEvent *e)
{
    const int angle = angleAt(e->position().toPoint());
    if (e->button() == Qt::LeftButton && angle >= 0)
        m_ctl->switchMulticamAngle(angle);
    QWidget::mousePressEvent(e);
}

} // namespace bc
