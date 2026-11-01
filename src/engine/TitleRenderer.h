#pragma once

#include "core/Timeline.h"

#include <QImage>
#include <QSize>

namespace bc {

// Draws a title on a transparent image of the frame size. The same picture
// is used for preview and (in phase 4) export.
QImage renderTitle(const TitleData &title, const QSize &frameSize);

} // namespace bc
