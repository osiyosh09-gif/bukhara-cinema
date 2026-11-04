#include "app/ColorWheel.h"

#include "core/Color.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace bc {

ColorWheel::ColorWheel(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
}

double ColorWheel::radius() const
{
    return std::max(10.0, std::min(width(), height()) / 2.0 - 4);
}

QPointF ColorWheel::centre() const
{
    return QPointF(width() / 2.0, height() / 2.0);
}

QPointF ColorWheel::pointPos(double x, double y) const
{
    // y grows upwards on the wheel.
    return centre() + QPointF(x, -y) / 100.0 * radius();
}

void ColorWheel::setPoint(double x, double y)
{
    x = std::clamp(x, -100.0, 100.0);
    y = std::clamp(y, -100.0, 100.0);
    if (x == m_x && y == m_y)
        return;
    m_x = x;
    m_y = y;
    update();
}

void ColorWheel::setByUser(double x, double y)
{
    // Keep the point inside the disc; whole numbers like the stored values.
    const double len = std::hypot(x, y);
    if (len > 100) {
        x = x / len * 100;
        y = y / len * 100;
    }
    x = std::round(x);
    y = std::round(y);
    if (x == m_x && y == m_y)
        return;
    setPoint(x, y);
    emit moved(m_x, m_y);
}

void ColorWheel::moveTo(const QPointF &pos)
{
    const QPointF d = (pos - centre()) / radius() * 100.0;
    setByUser(d.x(), -d.y());
}

void ColorWheel::resizeEvent(QResizeEvent *e)
{
    m_disc = QImage();
    QWidget::resizeEvent(e);
}

void ColorWheel::paintEvent(QPaintEvent *)
{
    const qreal dpr = devicePixelRatioF();
    const double r = radius();
    const int side = int(std::ceil(2 * r * dpr));
    if (m_disc.width() != side) {
        // Each point shows the colour the wheel adds there, on a mid grey.
        m_disc = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
        m_disc.fill(Qt::transparent);
        const double half = side / 2.0;
        for (int j = 0; j < side; ++j) {
            auto *line = reinterpret_cast<QRgb *>(m_disc.scanLine(j));
            for (int i = 0; i < side; ++i) {
                const double dx = (i + 0.5 - half) / half, dy = (half - j - 0.5) / half;
                const double d = std::hypot(dx, dy);
                if (d > 1.0)
                    continue;
                double rgb[3];
                wheelBalance(dx * 100, dy * 100, rgb);
                const double edge = std::clamp((1.0 - d) * half, 0.0, 1.0); // soft rim
                auto ch = [&](double v) { return int(std::clamp(0.36 + 0.34 * v, 0.0, 1.0) * 255 * edge); };
                line[i] = qRgba(ch(rgb[0]), ch(rgb[1]), ch(rgb[2]), int(255 * edge));
            }
        }
        m_disc.setDevicePixelRatio(dpr);
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = centre();
    p.drawImage(QPointF(c.x() - r, c.y() - r), m_disc);
    p.setPen(QPen(QColor(20, 20, 20), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, r, r);
    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    p.drawLine(QPointF(c.x() - r, c.y()), QPointF(c.x() + r, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - r), QPointF(c.x(), c.y() + r));
    const QPointF pt = pointPos(m_x, m_y);
    if (m_x != 0 || m_y != 0) {
        p.setPen(QPen(QColor(255, 255, 255, 140), 1));
        p.drawLine(c, pt);
    }
    p.setPen(QPen(hasFocus() ? QColor(0xd1, 0xa5, 0x4a) : QColor(240, 240, 240), 2));
    p.setBrush(QColor(30, 30, 30));
    p.drawEllipse(pt, 4.5, 4.5);
    if (!isEnabled()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 40, 40, 150));
        p.drawEllipse(c, r, r);
    }
}

void ColorWheel::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        moveTo(e->position());
}

void ColorWheel::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::LeftButton)
        moveTo(e->position());
}

void ColorWheel::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        setByUser(0, 0);
}

void ColorWheel::keyPressEvent(QKeyEvent *e)
{
    const double step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    switch (e->key()) {
    case Qt::Key_Left: setByUser(m_x - step, m_y); break;
    case Qt::Key_Right: setByUser(m_x + step, m_y); break;
    case Qt::Key_Up: setByUser(m_x, m_y + step); break;
    case Qt::Key_Down: setByUser(m_x, m_y - step); break;
    case Qt::Key_Home:
    case Qt::Key_0: setByUser(0, 0); break;
    default: QWidget::keyPressEvent(e); return;
    }
}

} // namespace bc
