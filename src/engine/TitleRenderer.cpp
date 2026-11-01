#include "engine/TitleRenderer.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace bc {

QImage renderTitle(const TitleData &title, const QSize &frameSize)
{
    const QSize size(std::max(2, frameSize.width()), std::max(2, frameSize.height()));
    QImage img(size, QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    const QString text = title.text.trimmed().isEmpty() ? QString() : title.text;
    if (text.isEmpty())
        return img.convertToFormat(QImage::Format_RGBA8888);

    const double scale = size.height() / 1080.0;
    QFont font = QGuiApplication::font();
    font.setPixelSize(std::max(4, int(std::lround(title.size * scale))));
    font.setBold(true);
    const QFontMetricsF fm(font);

    const double margin = size.height() * 0.08;
    const double sideMargin = size.width() * 0.05;
    const QRectF area(sideMargin, margin, size.width() - 2 * sideMargin, size.height() - 2 * margin);
    const int flags = Qt::AlignHCenter | Qt::TextWordWrap;
    QRectF textRect = fm.boundingRect(area, flags, text);
    // Vertical place inside the safe area.
    double top = area.top();
    if (title.position == TitleData::Position::Center)
        top = area.center().y() - textRect.height() / 2;
    else if (title.position == TitleData::Position::Bottom)
        top = area.bottom() - textRect.height();
    textRect.moveTop(top);
    textRect.moveLeft(area.center().x() - textRect.width() / 2);

    QPainter p(&img);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.setFont(font);
    if (title.box) {
        const double pad = font.pixelSize() * 0.35;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 170));
        p.drawRoundedRect(textRect.adjusted(-pad, -pad / 2, pad, pad / 2), pad / 2, pad / 2);
    } else {
        // Soft shadow keeps the text readable on bright pictures.
        const double off = std::max(1.0, font.pixelSize() * 0.05);
        p.setPen(QColor(0, 0, 0, 160));
        p.drawText(textRect.translated(off, off), flags, text);
    }
    QColor color(title.color);
    if (!color.isValid())
        color = Qt::white;
    p.setPen(color);
    p.drawText(textRect, flags, text);
    p.end();
    return img.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace bc
