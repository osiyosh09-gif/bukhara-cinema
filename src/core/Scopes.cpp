#include "core/Scopes.h"

#include <QPainter>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace bc::Scopes {

int luma(int r, int g, int b)
{
    return std::clamp(int(std::lround(0.2126 * r + 0.7152 * g + 0.0722 * b)), 0, 255);
}

void chroma(int r, int g, int b, double *cb, double *cr)
{
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    *cb = (b - y) / 1.8556; // -127.5 .. 127.5
    *cr = (r - y) / 1.5748;
}

Stats statistics(const QImage &frame)
{
    Stats s;
    if (frame.isNull())
        return s;
    const QImage img = frame.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
            ++s.red[r];
            ++s.green[g];
            ++s.blue[b];
            ++s.luma[luma(r, g, b)];
        }
    }
    s.pixels = img.width() * img.height();
    return s;
}

namespace {

const QColor kBackground(18, 18, 18);
const QColor kGrid(70, 70, 70);

QImage prepared(const QImage &frame, int analyseWidth)
{
    QImage img = frame.convertToFormat(QImage::Format_RGB32);
    if (img.width() > analyseWidth)
        img = img.scaledToWidth(analyseWidth, Qt::FastTransformation);
    return img;
}

// Brightness of a trace point from how many pixels landed there.
inline int glow(int count, double gain)
{
    return count <= 0 ? 0 : std::min(255, int(110 + std::log1p(count * gain) * 70));
}

// One waveform (of channel 0 = luma, 1..3 = R, G, B) into `area` of `out`.
void drawWaveform(QImage &out, const QImage &img, const QRect &area, int channel, QColor tint)
{
    const int w = area.width(), h = area.height();
    if (w <= 0 || h <= 0)
        return;
    QVector<int> grid(w * h, 0);
    for (int y = 0; y < img.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = line[x];
            const int v = channel == 1 ? qRed(p) : channel == 2 ? qGreen(p) : channel == 3 ? qBlue(p)
                                                                                         : luma(qRed(p), qGreen(p), qBlue(p));
            const int ox = std::min(w - 1, x * w / img.width());
            const int oy = (h - 1) - v * (h - 1) / 255;
            ++grid[oy * w + ox];
        }
    }
    const double gain = double(w) / std::max(1, img.width()) * 255.0 / std::max(1, img.height()) * 0.6;
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(out.scanLine(area.top() + y)) + area.left();
        for (int x = 0; x < w; ++x) {
            const int c = glow(grid[y * w + x], gain);
            if (c == 0)
                continue;
            const QColor base(line[x]);
            line[x] = qRgb(std::min(255, base.red() + tint.red() * c / 255),
                           std::min(255, base.green() + tint.green() * c / 255),
                           std::min(255, base.blue() + tint.blue() * c / 255));
        }
    }
}

void levelGrid(QPainter &p, const QRect &area)
{
    p.setPen(QPen(kGrid, 1, Qt::DashLine));
    for (int pct : {0, 25, 50, 75, 100}) {
        const int y = area.top() + (area.height() - 1) - pct * (area.height() - 1) / 100;
        p.drawLine(area.left(), y, area.right(), y);
    }
}

} // namespace

QImage render(const QImage &frame, Kind kind, QSize size, int analyseWidth)
{
    QImage out(size.expandedTo(QSize(16, 16)), QImage::Format_RGB32);
    out.fill(kBackground);
    if (frame.isNull())
        return out;
    const QImage img = prepared(frame, std::max(16, analyseWidth));
    const QRect all = out.rect();

    switch (kind) {
    case Kind::Waveform: {
        {
            QPainter p(&out);
            levelGrid(p, all);
        }
        drawWaveform(out, img, all, 0, QColor(150, 255, 150));
        break;
    }
    case Kind::Parade: {
        const int third = out.width() / 3;
        const QRect parts[3] = {QRect(0, 0, third - 2, out.height()), QRect(third, 0, third - 2, out.height()),
                                QRect(2 * third, 0, out.width() - 2 * third, out.height())};
        {
            QPainter p(&out);
            for (const QRect &r : parts)
                levelGrid(p, r);
        }
        drawWaveform(out, img, parts[0], 1, QColor(255, 70, 70));
        drawWaveform(out, img, parts[1], 2, QColor(70, 255, 70));
        drawWaveform(out, img, parts[2], 3, QColor(90, 120, 255));
        break;
    }
    case Kind::Vectorscope: {
        const int side = std::min(out.width(), out.height());
        const QRect sq((out.width() - side) / 2, (out.height() - side) / 2, side, side);
        const double radius = side / 2.0 - 4;
        const QPointF centre = QRectF(sq).center();
        {
            QPainter p(&out);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(kGrid, 1));
            p.drawEllipse(centre, radius, radius);
            p.drawEllipse(centre, radius * 0.5, radius * 0.5);
            p.drawLine(QPointF(centre.x() - radius, centre.y()), QPointF(centre.x() + radius, centre.y()));
            p.drawLine(QPointF(centre.x(), centre.y() - radius), QPointF(centre.x(), centre.y() + radius));
            // Targets of 75 % colour bars, and the skin tone line.
            const QColor targets[6] = {QColor(191, 0, 0), QColor(191, 191, 0), QColor(0, 191, 0),
                                       QColor(0, 191, 191), QColor(0, 0, 191), QColor(191, 0, 191)};
            for (const QColor &t : targets) {
                double cb, cr;
                chroma(t.red(), t.green(), t.blue(), &cb, &cr);
                const QPointF pt(centre.x() + cb / 128.0 * radius, centre.y() - cr / 128.0 * radius);
                p.setPen(QPen(t.lighter(130), 1));
                p.drawRect(QRectF(pt.x() - 4, pt.y() - 4, 8, 8));
            }
            double cb, cr;
            chroma(224, 172, 140, &cb, &cr); // a typical skin tone
            const double len = std::hypot(cb, cr);
            p.setPen(QPen(QColor(120, 100, 80), 1, Qt::DashLine));
            p.drawLine(centre, QPointF(centre.x() + cb / len * radius, centre.y() - cr / len * radius));
        }
        QVector<int> grid(side * side, 0);
        for (int y = 0; y < img.height(); ++y) {
            const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                double cb, cr;
                chroma(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), &cb, &cr);
                const int px = std::clamp(int(std::lround(side / 2.0 + cb / 128.0 * radius)), 0, side - 1);
                const int py = std::clamp(int(std::lround(side / 2.0 - cr / 128.0 * radius)), 0, side - 1);
                ++grid[py * side + px];
            }
        }
        const double gain = 300.0 / std::max(1, img.width() * img.height() / 100);
        for (int y = 0; y < side; ++y) {
            auto *line = reinterpret_cast<QRgb *>(out.scanLine(sq.top() + y)) + sq.left();
            for (int x = 0; x < side; ++x) {
                const int c = glow(grid[y * side + x], gain);
                if (c > 0)
                    line[x] = qRgb(std::max(qRed(line[x]), c * 3 / 4), std::max(qGreen(line[x]), c),
                                   std::max(qBlue(line[x]), c * 3 / 4));
            }
        }
        break;
    }
    case Kind::Histogram: {
        const Stats s = statistics(img);
        int peak = 1;
        for (int i = 1; i < 255; ++i) // the ends are often huge (black bars): leave them out of the scale
            peak = std::max({peak, s.red[i], s.green[i], s.blue[i]});
        {
            QPainter p(&out);
            p.setPen(QPen(kGrid, 1, Qt::DashLine));
            for (int pct : {25, 50, 75})
                p.drawLine(pct * (out.width() - 1) / 100, 0, pct * (out.width() - 1) / 100, out.height());
        }
        const int h = out.height();
        for (int x = 0; x < out.width(); ++x) {
            const int bin = x * 256 / out.width();
            // Log scale: one flat colour area does not flatten everything else.
            auto bar = [&](int n) {
                return std::min(h, int(std::lround(std::log1p(double(n)) / std::log1p(double(peak)) * (h - 1))));
            };
            const int bars[3] = {bar(s.red[bin]), bar(s.green[bin]), bar(s.blue[bin])};
            for (int y = 0; y < h; ++y) {
                const int fromBottom = h - 1 - y;
                const int r = fromBottom < bars[0] ? 200 : 0;
                const int g = fromBottom < bars[1] ? 200 : 0;
                const int b = fromBottom < bars[2] ? 220 : 0;
                if (r || g || b) {
                    auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
                    line[x] = qRgb(std::max(qRed(line[x]), r), std::max(qGreen(line[x]), g), std::max(qBlue(line[x]), b));
                }
            }
        }
        break;
    }
    }
    return out;
}

} // namespace bc::Scopes
