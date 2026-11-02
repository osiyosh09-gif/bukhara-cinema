#pragma once

#include "core/Project.h"

#include <QRectF>
#include <QUuid>

namespace bc::Multicam {

// The multicamera monitor (phase 12b) shows every angle at once in a grid:
// g = ceil(sqrt(n)) columns of square-ish cells, the rows centred.
int columns(int count);
// Cell of angle `index` as a fraction (0..1) of the frame.
QRectF cell(int index, int count);
// Angle under a point given as a fraction of the frame, or -1.
int angleAt(const QPointF &fraction, int count);

// A project whose main sequence shows every video angle of the multicamera
// sequence `nestId` in its cell (no sound, no subtitles). Frames of it match
// frames of the multicamera sequence. Empty timeline when it is not one.
Project gridProject(const Project &project, const QUuid &nestId);

} // namespace bc::Multicam
