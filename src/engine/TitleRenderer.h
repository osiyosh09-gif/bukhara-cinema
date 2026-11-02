#pragma once

#include "core/Timeline.h"

#include <QImage>
#include <QSize>

namespace bc {

// Draws a title on a transparent image of the frame size. The same picture
// is used for preview and (in phase 4) export.
QImage renderTitle(const TitleData &title, const QSize &frameSize);

// Draws one subtitle (phase 11) on a transparent image of the frame size:
// centred, its last line `style.margin` % above the bottom edge.
QImage renderSubtitle(const QString &text, const SubtitleStyle &style, const QSize &frameSize);

} // namespace bc
