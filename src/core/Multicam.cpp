#include "core/Multicam.h"

#include <cmath>

namespace bc::Multicam {

int columns(int count)
{
    return std::max(1, int(std::ceil(std::sqrt(double(std::max(1, count))))));
}

QRectF cell(int index, int count)
{
    if (index < 0 || index >= count)
        return {};
    const int g = columns(count);
    const int rows = (count + g - 1) / g;
    const double top = (1.0 - double(rows) / g) / 2;
    return {double(index % g) / g, top + double(index / g) / g, 1.0 / g, 1.0 / g};
}

int angleAt(const QPointF &fraction, int count)
{
    for (int i = 0; i < count; ++i)
        if (cell(i, count).contains(fraction))
            return i;
    return -1;
}

Project gridProject(const Project &project, const QUuid &nestId)
{
    Project grid = project;
    grid.timeline = Timeline();
    const NestedSequence *n = project.nest(nestId);
    if (!n || !n->multicam)
        return grid;
    grid.timeline = n->timeline;
    grid.timeline.subtitles.clear();
    const int count = grid.timeline.count(Timeline::Kind::Video);
    int angle = 0;
    for (TimelineTrack &t : grid.timeline.tracks) {
        t.solo = false;
        if (t.kind == Timeline::Kind::Audio) {
            t.clips.clear();
            continue;
        }
        const QRectF r = cell(angle++, count);
        t.hidden = false;
        ClipTransform place;
        place.scale = r.width() * 100;
        place.x = r.center().x() - 0.5;
        place.y = r.center().y() - 0.5;
        for (TimelineClip &c : t.clips) {
            c.motion = ClipMotion();
            c.motion.base = place;
            c.head = {};
            c.tail = {};
        }
    }
    return grid;
}

} // namespace bc::Multicam
