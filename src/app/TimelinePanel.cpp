#include "app/TimelinePanel.h"

#include "core/Waveform.h"

#include "app/Formatting.h"
#include "app/MediaBinPanel.h"
#include "app/ProjectController.h"
#include "app/LoudnessDialog.h"
#include "app/SpeedDialog.h"

#include <QAction>
#include <QLineEdit>
#include <QInputDialog>
#include <QComboBox>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bc {

namespace {

const QColor kBg(0x1b, 0x1b, 0x1b);
const QColor kRowA(0x24, 0x24, 0x24);
const QColor kRowB(0x27, 0x27, 0x27);
const QColor kHeaderBg(0x2c, 0x2c, 0x2c);
const QColor kRulerBg(0x2a, 0x2a, 0x2a);
const QColor kLine(0x14, 0x14, 0x14);
const QColor kText(0xcf, 0xcf, 0xcf);
const QColor kDim(0x8a, 0x8a, 0x8a);
const QColor kGold(0xd1, 0xa5, 0x4a);
const QColor kVideoClip(0x3d, 0x5a, 0x80);
const QColor kAudioClip(0x3a, 0x6e, 0x4a);
const QColor kOfflineClip(0x80, 0x30, 0x30);

constexpr double kMinPpf = 0.02;
constexpr double kMaxPpf = 40.0;

} // namespace

// ---------------------------------------------------------------------------
// TimelineView

TimelineView::TimelineView(ProjectController *controller, QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_ctl(controller)
{
    setObjectName(QStringLiteral("timelineView"));
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    viewport()->setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::NoFrame);
    setMinimumHeight(120);

    connect(m_ctl, &ProjectController::timelineChanged, this, [this] {
        // Drop selected ids that no longer exist (undo, delete, ...).
        QSet<QUuid> keep;
        for (const QUuid &id : std::as_const(m_selection))
            if (m_ctl->timeline().clip(id))
                keep.insert(id);
        if (keep != m_selection) {
            m_selection = keep;
            emit selectionChanged();
        }
        updateScrollBars();
        viewport()->update();
    });
    connect(m_ctl, &ProjectController::projectReset, this, [this] {
        m_selection.clear();
        horizontalScrollBar()->setValue(0);
        emit selectionChanged();
        updateScrollBars();
        viewport()->update();
    });
    connect(m_ctl, &ProjectController::playheadChanged, this, [this] {
        if (m_drag != Drag::Scrub)
            ensurePlayheadVisible();
        viewport()->update();
    });
    connect(m_ctl, &ProjectController::itemUpdated, viewport(), qOverload<>(&QWidget::update));
    connect(m_ctl, &ProjectController::binChanged, viewport(), qOverload<>(&QWidget::update));
    updateScrollBars();
}

void TimelineView::setSelection(const QList<QUuid> &ids)
{
    m_selection = QSet<QUuid>(ids.begin(), ids.end());
    emit selectionChanged();
    viewport()->update();
}

// --- geometry

const Timeline &TimelineView::shown() const
{
    return m_preview ? *m_preview : m_ctl->timeline();
}

QList<int> TimelineView::rows() const
{
    const Timeline &tl = shown();
    QList<int> r;
    for (int i = int(tl.tracks.size()) - 1; i >= 0; --i)
        if (tl.tracks[i].kind == Timeline::Kind::Video)
            r << i;
    for (int i = 0; i < tl.tracks.size(); ++i)
        if (tl.tracks[i].kind == Timeline::Kind::Audio)
            r << i;
    return r;
}

int TimelineView::rowHeight(int track) const
{
    return shown().tracks[track].kind == Timeline::Kind::Video ? kVideoH : kAudioH;
}

int TimelineView::rowTop(int row) const
{
    const QList<int> r = rows();
    int y = tracksTop() - verticalScrollBar()->value();
    for (int i = 0; i < row && i < r.size(); ++i)
        y += rowHeight(r[i]);
    return y;
}

int TimelineView::contentHeight() const
{
    int h = 0;
    for (int t : rows())
        h += rowHeight(t);
    return h;
}

QRect TimelineView::trackRect(int track) const
{
    const QList<int> r = rows();
    const int row = int(r.indexOf(track));
    if (row < 0)
        return {};
    return QRect(0, rowTop(row), viewport()->width(), rowHeight(track));
}

int TimelineView::trackAtY(int y) const
{
    if (y < tracksTop())
        return -1;
    const QList<int> r = rows();
    int top = tracksTop() - verticalScrollBar()->value();
    for (int t : r) {
        const int h = rowHeight(t);
        if (y >= top && y < top + h)
            return t;
        top += h;
    }
    return -1;
}

int TimelineView::kindIndex(int track) const
{
    const Timeline &tl = m_ctl->timeline();
    int n = 0;
    for (int i = 0; i < track && i < tl.tracks.size(); ++i)
        if (tl.tracks[i].kind == tl.tracks[track].kind)
            ++n;
    return n;
}

qint64 TimelineView::frameAtX(int x) const
{
    const double f = (x - kHeaderW + horizontalScrollBar()->value()) / m_ppf;
    return std::max<qint64>(0, qint64(std::floor(f)));
}

int TimelineView::xForFrame(qint64 frame) const
{
    return int(std::lround(kHeaderW + frame * m_ppf - horizontalScrollBar()->value()));
}

QRect TimelineView::clipRect(int track, const TimelineClip &c) const
{
    const QRect rr = trackRect(track);
    const int x1 = xForFrame(c.start);
    const int x2 = xForFrame(c.end());
    return QRect(x1, rr.top() + 2, std::max(2, x2 - x1), rr.height() - 4);
}

QRect TimelineView::headerButton(int track, int which) const
{
    const QRect rr = trackRect(track);
    const int size = 18;
    const int x = kHeaderW - 6 - size - which * (size + 4);
    return QRect(x, rr.top() + (rr.height() - size) / 2, size, size);
}

TimelineView::Hit TimelineView::hitTest(const QPoint &p) const
{
    Hit h;
    if (p.x() < kHeaderW)
        return h;
    h.track = trackAtY(p.y());
    if (h.track < 0)
        return h;
    for (const TimelineClip &c : m_ctl->timeline().tracks[h.track].clips) {
        const QRect r = clipRect(h.track, c);
        if (p.x() < r.left() - kEdgePx || p.x() > r.right() + kEdgePx)
            continue;
        const bool roomForEdges = r.width() >= 3 * kEdgePx;
        if (roomForEdges && std::abs(p.x() - r.left()) <= kEdgePx) {
            h.clip = c.id;
            h.headEdge = true;
        } else if (roomForEdges && std::abs(p.x() - r.right()) <= kEdgePx) {
            h.clip = c.id;
            h.tailEdge = true;
        } else if (r.contains(QPoint(p.x(), r.center().y()))) {
            h.clip = c.id;
        }
        if (!h.clip.isNull())
            return h;
    }
    return h;
}

qint64 TimelineView::snap(qint64 frame, const QSet<QUuid> &exclude, qint64 *snappedTo) const
{
    *snappedTo = -1;
    if (!m_snapping)
        return frame;
    QList<qint64> pts = m_ctl->timeline().snapPoints(exclude);
    pts << m_ctl->playhead();
    const double maxDist = kSnapPx / m_ppf;
    qint64 best = frame;
    double bestDist = maxDist + 1;
    for (qint64 p : pts) {
        const double d = std::abs(double(p - frame));
        if (d <= maxDist && d < bestDist) {
            bestDist = d;
            best = p;
        }
    }
    if (bestDist <= maxDist)
        *snappedTo = best;
    return best;
}

qint64 TimelineView::snapDelta(qint64 delta, const QList<QUuid> &moving, qint64 *snappedTo) const
{
    *snappedTo = -1;
    if (!m_snapping)
        return delta;
    const Timeline &tl = m_ctl->timeline();
    const QSet<QUuid> ex(moving.begin(), moving.end());
    QList<qint64> pts = tl.snapPoints(ex);
    pts << m_ctl->playhead();
    const double maxDist = kSnapPx / m_ppf;
    double bestDist = maxDist + 1;
    qint64 bestDelta = delta;
    for (const QUuid &id : moving) {
        const TimelineClip *c = tl.clip(id);
        if (!c)
            continue;
        for (qint64 edge : {c->start, c->end()}) {
            const qint64 moved = edge + delta;
            for (qint64 p : pts) {
                const double d = std::abs(double(p - moved));
                if (d <= maxDist && d < bestDist) {
                    bestDist = d;
                    bestDelta = delta + (p - moved);
                    *snappedTo = p;
                }
            }
        }
    }
    return bestDelta;
}

// --- zoom and scrolling

void TimelineView::setPixelsPerFrame(double ppf, int anchorX)
{
    ppf = std::clamp(ppf, kMinPpf, kMaxPpf);
    if (std::abs(ppf - m_ppf) < 1e-9)
        return;
    if (anchorX < kHeaderW)
        anchorX = std::clamp(xForFrame(m_ctl->playhead()), kHeaderW, std::max(kHeaderW, viewport()->width()));
    const double frameAtAnchor = (anchorX - kHeaderW + horizontalScrollBar()->value()) / m_ppf;
    m_ppf = ppf;
    updateScrollBars();
    horizontalScrollBar()->setValue(int(std::lround(frameAtAnchor * m_ppf - (anchorX - kHeaderW))));
    viewport()->update();
    emit zoomChanged(m_ppf);
}

void TimelineView::zoomIn()
{
    setPixelsPerFrame(m_ppf * 1.5);
}

void TimelineView::zoomOut()
{
    setPixelsPerFrame(m_ppf / 1.5);
}

void TimelineView::zoomToFit()
{
    const qint64 d = m_ctl->timeline().duration();
    const int w = std::max(50, viewport()->width() - kHeaderW - 20);
    const double fps = std::max(1.0, m_ctl->project().sequence.fps());
    const double frames = d > 0 ? double(d) : 30.0 * fps; // empty: show 30 seconds
    setPixelsPerFrame(w / frames, kHeaderW);
    horizontalScrollBar()->setValue(0);
}

void TimelineView::updateScrollBars()
{
    const double fps = std::max(1.0, m_ctl->project().sequence.fps());
    const int visibleW = std::max(1, viewport()->width() - kHeaderW);
    const qint64 frames = std::max(m_ctl->timeline().duration(), m_ctl->playhead()) + qint64(30 * fps);
    const int total = int(std::min<double>(frames * m_ppf, 2e9));
    horizontalScrollBar()->setRange(0, std::max(0, total - visibleW));
    horizontalScrollBar()->setPageStep(visibleW);
    horizontalScrollBar()->setSingleStep(std::max(1, visibleW / 20));

    const int visibleH = std::max(1, viewport()->height() - tracksTop());
    verticalScrollBar()->setRange(0, std::max(0, contentHeight() - visibleH));
    verticalScrollBar()->setPageStep(visibleH);
    verticalScrollBar()->setSingleStep(20);
}

void TimelineView::ensurePlayheadVisible()
{
    const int x = xForFrame(m_ctl->playhead());
    const int w = viewport()->width();
    if (x < kHeaderW || x > w - 20) {
        updateScrollBars();
        const int target = int(std::lround(m_ctl->playhead() * m_ppf)) - (w - kHeaderW) / 3;
        horizontalScrollBar()->setValue(std::max(0, target));
    }
}

void TimelineView::scrollContentsBy(int, int)
{
    viewport()->update();
}

void TimelineView::resizeEvent(QResizeEvent *e)
{
    QAbstractScrollArea::resizeEvent(e);
    updateScrollBars();
}

// --- painting

void TimelineView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), kBg);
    drawTracks(p);
    drawSubtitles(p);
    drawRuler(p);

    // Playhead
    const int px = xForFrame(m_ctl->playhead());
    if (px >= kHeaderW) {
        p.setPen(QPen(kGold, 1));
        p.drawLine(px, kRulerH - 6, px, viewport()->height());
        QPainterPath head;
        head.moveTo(px - 6, 2);
        head.lineTo(px + 6, 2);
        head.lineTo(px + 6, kRulerH - 10);
        head.lineTo(px, kRulerH - 4);
        head.lineTo(px - 6, kRulerH - 10);
        head.closeSubpath();
        p.fillPath(head, kGold);
    }
    // Snap guide
    if (m_snapLine >= 0) {
        const int sx = xForFrame(m_snapLine);
        if (sx >= kHeaderW) {
            p.setPen(QPen(Qt::white, 1, Qt::DashLine));
            p.drawLine(sx, kRulerH, sx, viewport()->height());
        }
    }

    if (m_ctl->timeline().isEmpty() && !m_preview) {
        p.setPen(kDim);
        p.drawText(QRect(kHeaderW, kRulerH, viewport()->width() - kHeaderW, viewport()->height() - kRulerH),
                   Qt::AlignCenter, tr("Drag media from the bin here"));
    }
}

int TimelineView::subtitleStripHeight() const
{
    return m_ctl->timeline().subtitles.isEmpty() ? 0 : kSubsH;
}

QRect TimelineView::subtitleRect(const Subtitle &s) const
{
    qint64 start = s.start, length = s.length;
    if (m_drag != Drag::None && m_dragStarted && s.id == m_dragSub) {
        start = m_subStart;
        length = m_subLength;
    }
    const int x1 = xForFrame(start);
    const int x2 = xForFrame(start + length);
    return QRect(x1, kRulerH + 2, std::max(3, x2 - x1), kSubsH - 4);
}

QUuid TimelineView::subtitleAt(const QPoint &pos, bool *headEdge, bool *tailEdge) const
{
    if (headEdge)
        *headEdge = false;
    if (tailEdge)
        *tailEdge = false;
    if (pos.y() < kRulerH || pos.y() >= tracksTop() || pos.x() < kHeaderW)
        return {};
    const auto &subs = m_ctl->timeline().subtitles;
    // Later subtitles are drawn on top: search from the end.
    for (auto it = subs.crbegin(); it != subs.crend(); ++it) {
        const QRect r = subtitleRect(*it);
        if (pos.x() < r.left() - 2 || pos.x() > r.right() + 2)
            continue;
        const int edge = std::min(kEdgePx, r.width() / 3);
        if (headEdge)
            *headEdge = pos.x() <= r.left() + edge;
        if (tailEdge)
            *tailEdge = !(headEdge && *headEdge) && pos.x() >= r.right() - edge;
        return it->id;
    }
    return {};
}

void TimelineView::setSelectedSubtitle(const QUuid &id)
{
    if (m_subSel == id)
        return;
    m_subSel = id;
    viewport()->update();
}

void TimelineView::drawSubtitles(QPainter &p)
{
    const int h = subtitleStripHeight();
    if (h == 0)
        return;
    const int w = viewport()->width();
    const QRect strip(0, kRulerH, w, h);
    p.save();
    p.fillRect(strip, QColor(0x20, 0x20, 0x26));
    p.fillRect(QRect(0, kRulerH, kHeaderW, h), kHeaderBg);
    p.setPen(kLine);
    p.drawLine(0, strip.bottom(), w, strip.bottom());
    QFont f = p.font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1));
    p.setFont(f);
    p.setPen(kDim);
    p.drawText(QRect(8, kRulerH, kHeaderW - 10, h), Qt::AlignVCenter | Qt::AlignLeft, tr("Subtitles"));
    p.setClipRect(QRect(kHeaderW, kRulerH, w - kHeaderW, h));
    const QColor fill(0x8a, 0x5a, 0x2c), fillSel(0xc0, 0x84, 0x3c);
    for (const Subtitle &s : m_ctl->timeline().subtitles) {
        const QRect r = subtitleRect(s);
        if (r.right() < kHeaderW || r.left() > w)
            continue;
        const bool sel = s.id == m_subSel;
        p.setPen(QPen(sel ? kGold : QColor(0x2a, 0x1a, 0x0c), 1));
        p.setBrush(sel ? fillSel : fill);
        p.drawRoundedRect(r, 3, 3);
        if (r.width() > 14) {
            p.setPen(QColor(0xf4, 0xec, 0xe0));
            const QString text = s.text.simplified().isEmpty() ? tr("(empty)") : s.text.simplified();
            p.drawText(r.adjusted(4, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(text, Qt::ElideRight, r.width() - 7));
        }
    }
    p.restore();
}

void TimelineView::drawRuler(QPainter &p)
{
    const QRect ruler(0, 0, viewport()->width(), kRulerH);
    p.fillRect(ruler, kRulerBg);
    p.fillRect(QRect(0, 0, kHeaderW, kRulerH), kHeaderBg);
    p.setPen(kLine);
    p.drawLine(0, kRulerH - 1, ruler.right(), kRulerH - 1);

    const double fps = std::max(1.0, m_ctl->project().sequence.fps());
    const qint64 fpsInt = std::max<qint64>(1, qint64(std::llround(fps)));
    // Pick a label step that leaves at least ~90 px between labels.
    const QList<qint64> steps = {1, 2, 5, 10, fpsInt, 2 * fpsInt, 5 * fpsInt, 10 * fpsInt, 30 * fpsInt,
                                 60 * fpsInt, 120 * fpsInt, 300 * fpsInt, 600 * fpsInt, 1800 * fpsInt,
                                 3600 * fpsInt};
    qint64 step = steps.last();
    for (qint64 s : steps) {
        if (s * m_ppf >= 90) {
            step = s;
            break;
        }
    }
    const qint64 minor = step >= 10 ? step / 5 : step;

    p.save();
    p.setClipRect(QRect(kHeaderW, 0, viewport()->width() - kHeaderW, kRulerH));
    QFont f = p.font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1));
    p.setFont(f);
    const qint64 first = frameAtX(kHeaderW);
    const qint64 last = frameAtX(viewport()->width()) + 1;
    for (qint64 fr = (first / minor) * minor; fr <= last; fr += minor) {
        const int x = xForFrame(fr);
        const bool major = fr % step == 0;
        p.setPen(major ? kDim : QColor(0x55, 0x55, 0x55));
        p.drawLine(x, kRulerH - (major ? 10 : 5), x, kRulerH - 1);
        if (major) {
            p.setPen(kDim);
            p.drawText(x + 3, 12, Fmt::frameTimecode(fr, fps));
        }
    }
    // Markers: a small green flag with the name (M adds one at the playhead).
    for (const TimelineMarker &m : m_ctl->timeline().markers) {
        const int x = xForFrame(m.frame);
        if (x < kHeaderW - 8 || x > viewport()->width() + 8)
            continue;
        QPainterPath flag;
        flag.moveTo(x - 5, kRulerH - 14);
        flag.lineTo(x + 5, kRulerH - 14);
        flag.lineTo(x + 5, kRulerH - 7);
        flag.lineTo(x, kRulerH - 3);
        flag.lineTo(x - 5, kRulerH - 7);
        flag.closeSubpath();
        p.fillPath(flag, QColor(0x5f, 0xc8, 0x6e));
        if (!m.name.isEmpty()) {
            p.setPen(QColor(0x9f, 0xe0, 0xa8));
            p.drawText(x + 7, kRulerH - 4, m.name);
        }
    }
    p.restore();
}

void TimelineView::drawTracks(QPainter &p)
{
    const Timeline &tl = shown();
    const QList<int> r = rows();
    const int w = viewport()->width();

    p.save();
    p.setClipRect(QRect(0, tracksTop(), w, viewport()->height() - tracksTop()));
    for (int row = 0; row < r.size(); ++row) {
        const int t = r[row];
        const QRect rr(0, rowTop(row), w, rowHeight(t));
        p.fillRect(rr, row % 2 ? kRowB : kRowA);
        if (tl.tracks[t].locked)
            p.fillRect(QRect(kHeaderW, rr.top(), w - kHeaderW, rr.height()), QBrush(QColor(0, 0, 0, 70), Qt::BDiagPattern));
        p.setPen(kLine);
        p.drawLine(0, rr.bottom(), w, rr.bottom());
        // Boundary between video and audio
        if (row + 1 < r.size() && tl.tracks[t].kind != tl.tracks[r[row + 1]].kind) {
            p.setPen(QPen(QColor(0x50, 0x50, 0x50), 2));
            p.drawLine(0, rr.bottom(), w, rr.bottom());
        }
    }

    // Clips
    p.save();
    p.setClipRect(QRect(kHeaderW, tracksTop(), w - kHeaderW, viewport()->height() - tracksTop()));
    for (int t : r)
        for (const TimelineClip &c : tl.tracks[t].clips)
            drawClip(p, t, c);
    p.restore();

    // Headers
    for (int row = 0; row < r.size(); ++row) {
        const int t = r[row];
        const QRect hr(0, rowTop(row), kHeaderW, rowHeight(t));
        p.fillRect(hr, kHeaderBg);
        p.setPen(kLine);
        p.drawLine(hr.right(), hr.top(), hr.right(), hr.bottom());
        p.drawLine(hr.left(), hr.bottom(), hr.right(), hr.bottom());
        QFont bold = p.font();
        bold.setBold(true);
        p.setFont(bold);
        p.setPen(tl.tracks[t].kind == Timeline::Kind::Video ? QColor(0x9c, 0xb8, 0xdc) : QColor(0x9c, 0xd0, 0xa8));
        p.drawText(hr.adjusted(8, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, tl.trackName(t));
        bold.setBold(false);
        p.setFont(bold);

        // Lock button
        const QRect lock = headerButton(t, 0);
        const bool locked = tl.tracks[t].locked;
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(locked ? kGold : kDim, 1.5));
        p.setBrush(Qt::NoBrush);
        const QRect body(lock.left() + 4, lock.top() + 8, lock.width() - 8, lock.height() - 10);
        if (locked)
            p.fillRect(body, kGold);
        p.drawRect(body);
        p.drawArc(QRect(lock.left() + 6, lock.top() + 2, lock.width() - 12, 12), 0, 180 * 16);
        // Eye button (video tracks): hidden tracks are left out of the Program monitor.
        if (tl.tracks[t].kind == Timeline::Kind::Video) {
            const QRect eye = headerButton(t, 1);
            const bool hidden = tl.tracks[t].hidden;
            p.setPen(QPen(hidden ? kGold : kDim, 1.5));
            p.drawEllipse(eye.adjusted(1, 5, -1, -5));
            p.setBrush(hidden ? kGold : kDim);
            p.drawEllipse(eye.center(), 2, 2);
            p.setBrush(Qt::NoBrush);
            if (hidden)
                p.drawLine(eye.topLeft() + QPoint(2, 2), eye.bottomRight() - QPoint(2, 2));
        } else {
            // Mute (M) and solo (S) buttons on audio tracks.
            const auto letter = [&](int which, const QString &text, bool on, const QColor &onColor) {
                const QRect b = headerButton(t, which).adjusted(1, 1, -1, -1);
                p.setPen(on ? onColor : kDim);
                p.setBrush(on ? onColor : QColor(Qt::transparent));
                p.drawRoundedRect(b, 3, 3);
                p.setPen(on ? QColor(0x1e, 0x1e, 0x1e) : kDim);
                p.drawText(b, Qt::AlignCenter, text);
                p.setBrush(Qt::NoBrush);
            };
            letter(2, QStringLiteral("M"), tl.tracks[t].muted, kGold);
            letter(1, QStringLiteral("S"), tl.tracks[t].solo, QColor(0x7c, 0xc4, 0x6a));
        }
        p.setRenderHint(QPainter::Antialiasing, false);
    }
    p.restore();
}

void TimelineView::drawClip(QPainter &p, int track, const TimelineClip &c)
{
    const QRect r = clipRect(track, c);
    if (r.right() < kHeaderW || r.left() > viewport()->width())
        return;
    const Timeline &tl = shown();
    const bool video = tl.tracks[track].kind == Timeline::Kind::Video;
    const MediaItem *m = m_ctl->project().item(c.mediaId);
    const bool online = c.isTitle() || c.isNested() || (m && m_ctl->isOnline(c.mediaId));
    QColor fill = !online      ? kOfflineClip
                  : c.isTitle()  ? QColor(0x7a, 0x55, 0x9c)
                  : c.isNested() ? QColor(0x2f, 0x7f, 0x7a)
                                 : (video ? kVideoClip : kAudioClip);
    if (tl.tracks[track].locked || (video && tl.tracks[track].hidden))
        fill = fill.darker(150);
    p.fillRect(r, fill);

    int textX = r.left() + 4;
    if (video && online && m) {
        const QImage thumb = m_ctl->thumbnail(c.mediaId);
        if (!thumb.isNull() && r.width() > 80) {
            const QImage s = thumb.scaledToHeight(r.height() - 4, Qt::SmoothTransformation);
            p.drawImage(QPoint(r.left() + 2, r.top() + 2), s);
            textX += s.width() + 2;
        }
    }
    // Audio waveform of the used part of the source, scaled by the clip volume.
    if (!video && online && m) {
        const QByteArray peaks = m_ctl->waveform(c.mediaId);
        const double fps = m_ctl->project().sequence.fps();
        if (!peaks.isEmpty() && fps > 0 && m_ppf > 0) {
            const double gain = std::pow(10.0, c.gainDb / 20.0);
            const int top = r.top() + 2; // the name is drawn over it
            const int h = r.bottom() - 2 - top;
            const double mid = top + h / 2.0;
            const int x0 = std::max(r.left(), kHeaderW);
            const int x1 = std::min(r.right(), viewport()->width());
            p.save();
            p.setPen(QColor(0xa8, 0xe0, 0xb4, 200));
            for (int x = x0; x <= x1 && h > 4; ++x) {
                const double f = c.in + (x - r.left()) / m_ppf;
                const int v = Waveform::peakIn(peaks, f / fps, (f + 1 / m_ppf) / fps);
                const double a = std::min(1.0, v / 255.0 * gain) * h / 2.0;
                if (a >= 0.5)
                    p.drawLine(QPointF(x + 0.5, mid - a), QPointF(x + 0.5, mid + a));
            }
            p.restore();
        }
    }
    QString label = m ? m->name : QString();
    if (c.isTitle())
        label = QStringLiteral("T  ") + c.title.text.simplified();
    else if (c.isNested())
        label = QStringLiteral("\u29c9  ") + m_ctl->sequenceName(c.nestId);
    if (c.isNested() && m_ctl->isMulticam(c.nestId)) {
        const QStringList angles = m_ctl->multicamAngles(c.nestId, video ? Timeline::Kind::Video : Timeline::Kind::Audio);
        label += QStringLiteral("  \u25b8 %1. %2").arg(c.angle + 1).arg(angles.value(c.angle));
    }
    if (c.speed != 1.0)
        label += QStringLiteral("  [%1%]").arg(c.speed * 100, 0, 'f', c.speed * 100 == std::round(c.speed * 100) ? 0 : 1);
    if (effectsActive(c.effects) || (video && !c.color.isNeutral()))
        label = QStringLiteral("fx  ") + label;
    if (!video && c.gainDb != 0)
        label += QStringLiteral("  %1%2 dB").arg(c.gainDb > 0 ? QStringLiteral("+") : QString()).arg(c.gainDb, 0, 'f', 1);
    if (r.width() > 30 && !label.isEmpty()) {
        p.setPen(Qt::white);
        p.drawText(QRect(textX, r.top() + 2, r.right() - textX - 2, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(label, Qt::ElideRight, std::max(0, r.right() - textX - 2)));
    }
    // Fades: a ramp from the bottom corner to the top.
    if (!video && (c.fadeIn > 0 || c.fadeOut > 0)) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(255, 255, 255, 170), 1.2));
        if (c.fadeIn > 0)
            p.drawLine(QPointF(r.left(), r.bottom()), QPointF(r.left() + c.fadeIn * m_ppf, r.top() + 2));
        if (c.fadeOut > 0)
            p.drawLine(QPointF(r.right() - c.fadeOut * m_ppf, r.top() + 2), QPointF(r.right(), r.bottom()));
        p.restore();
    }
    // Transitions: a light box over the frames they take.
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(255, 255, 255, 170), 1));
        p.setBrush(QColor(255, 255, 255, 55));
        const Timeline &tl = m_ctl->timeline();
        auto box = [&](double x0, double x1) {
            const QRectF b(x0, r.top() + 1, std::max(4.0, x1 - x0), r.height() - 2);
            p.drawRect(b);
            p.drawLine(b.bottomLeft(), b.topRight());
        };
        if (!c.head.isNone()) {
            const bool cut = tl.touchingBefore(c.id) != nullptr;
            const double w = c.head.length * m_ppf;
            box(cut ? r.left() - w / 2 : r.left(), cut ? r.left() + w / 2 : r.left() + w);
        }
        if (!c.tail.isNone() && !tl.touchingAfter(c.id))
            box(r.right() + 1 - c.tail.length * m_ppf, r.right() + 1);
        p.restore();
    }
    // Motion keyframes: small diamonds along the bottom edge.
    if (video && c.motion.animated()) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(20, 20, 20), 1));
        p.setBrush(kGold);
        const double y = r.bottom() - 5;
        for (const TransformKey &k : c.motion.keys) {
            const qint64 off = qint64(std::llround((k.frame - c.in) / c.speed));
            if (off < 0 || off >= c.length)
                continue;
            const double x = r.left() + (off + 0.5) * m_ppf;
            const QPointF pts[4] = {{x, y - 4}, {x + 4, y}, {x, y + 4}, {x - 4, y}};
            p.drawPolygon(pts, 4);
        }
        p.restore();
    }
    if (m_selection.contains(c.id)) {
        p.setPen(QPen(kGold, 2));
        p.drawRect(r.adjusted(1, 1, -1, -1));
    } else {
        p.setPen(fill.lighter(140));
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }
}

// --- mouse

void TimelineView::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    if (e->button() != Qt::LeftButton)
        return QAbstractScrollArea::mousePressEvent(e);
    const QPoint pos = e->position().toPoint();
    m_pressPos = pos;
    m_dragStarted = false;
    m_preview.reset();
    m_snapLine = -1;

    if (pos.y() < kRulerH) {
        if (pos.x() >= kHeaderW) {
            m_drag = Drag::Scrub;
            m_ctl->setPlayhead(frameAtX(pos.x()));
        }
        return;
    }
    if (pos.y() < tracksTop()) { // subtitle strip
        bool head = false, tail = false;
        const QUuid id = pos.x() >= kHeaderW ? subtitleAt(pos, &head, &tail) : QUuid();
        setSelectedSubtitle(id);
        m_drag = Drag::None;
        if (const Subtitle *s = m_ctl->timeline().subtitle(id)) {
            emit subtitleClicked(id);
            m_dragSub = id;
            m_subStart = s->start;
            m_subLength = s->length;
            m_trimHead = head;
            m_drag = (head || tail) ? Drag::SubTrim : Drag::SubMove;
        }
        return;
    }
    if (pos.x() < kHeaderW) {
        const int t = trackAtY(pos.y());
        if (t < 0)
            return;
        const TimelineTrack &trk = m_ctl->timeline().tracks[t];
        if (headerButton(t, 0).contains(pos))
            m_ctl->setTrackFlag(t, QStringLiteral("locked"), !trk.locked);
        else if (trk.kind == Timeline::Kind::Video && headerButton(t, 1).contains(pos))
            m_ctl->setTrackFlag(t, QStringLiteral("hidden"), !trk.hidden);
        else if (trk.kind == Timeline::Kind::Audio && headerButton(t, 2).contains(pos))
            m_ctl->setTrackFlag(t, QStringLiteral("muted"), !trk.muted);
        else if (trk.kind == Timeline::Kind::Audio && headerButton(t, 1).contains(pos))
            m_ctl->setTrackFlag(t, QStringLiteral("solo"), !trk.solo);
        return;
    }

    const Hit h = hitTest(pos);
    if (h.clip.isNull()) {
        if (!(e->modifiers() & Qt::ControlModifier) && !m_selection.isEmpty()) {
            m_selection.clear();
            emit selectionChanged();
        }
        m_drag = Drag::None;
        viewport()->update();
        return;
    }

    const Timeline &tl = m_ctl->timeline();
    const QList<QUuid> group = (e->modifiers() & Qt::AltModifier) ? QList<QUuid>{h.clip} : tl.withLinked({h.clip});
    if (e->modifiers() & Qt::ControlModifier) {
        const bool on = !m_selection.contains(h.clip);
        for (const QUuid &id : group)
            on ? (void)m_selection.insert(id) : (void)m_selection.remove(id);
    } else if (!m_selection.contains(h.clip)) {
        m_selection = QSet<QUuid>(group.begin(), group.end());
    }
    emit selectionChanged();

    m_dragClip = h.clip;
    m_dragTrack = h.track;
    const bool locked = tl.tracks[h.track].locked;
    switch (m_tool) {
    case Tool::Razor:
        m_drag = Drag::None;
        if (!locked) {
            qint64 snapped = -1;
            const QList<QUuid> linked = tl.withLinked({h.clip});
            const qint64 f = snap(frameUnderMouse(pos.x()), QSet<QUuid>(linked.begin(), linked.end()), &snapped);
            m_ctl->splitClips(f, {h.clip});
        }
        break;
    case Tool::Roll:
        m_drag = (h.headEdge || h.tailEdge) && !locked ? Drag::Roll : Drag::None;
        m_trimHead = h.headEdge;
        break;
    case Tool::Slip:
        m_drag = locked ? Drag::None : Drag::Slip;
        break;
    case Tool::Slide:
        m_drag = locked ? Drag::None : Drag::Slide;
        break;
    case Tool::Select:
        if ((h.headEdge || h.tailEdge) && !locked) {
            m_drag = Drag::Trim;
            m_trimHead = h.headEdge;
        } else {
            m_drag = m_selection.contains(h.clip) ? Drag::Move : Drag::None;
        }
        break;
    }
    m_moveDelta = 0;
    m_moveTracks = 0;
    viewport()->update();
}

void TimelineView::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    if (m_drag == Drag::None) {
        updateCursor(pos);
        return;
    }
    if (m_drag == Drag::Scrub) {
        m_ctl->setPlayhead(frameAtX(std::max(kHeaderW, pos.x())));
        return;
    }
    if (!m_dragStarted && (pos - m_pressPos).manhattanLength() < 4)
        return;
    m_dragStarted = true;
    const Timeline &tl = m_ctl->timeline();

    if (m_drag == Drag::SubMove || m_drag == Drag::SubTrim) {
        const Subtitle *s = tl.subtitle(m_dragSub);
        if (!s)
            return;
        const qint64 delta = qint64(std::llround((pos.x() - m_pressPos.x()) / m_ppf));
        if (m_drag == Drag::SubMove) {
            // Snap the start, else the end, to clip edges, markers and the playhead.
            qint64 start = std::max<qint64>(0, s->start + delta);
            const qint64 snapped = snap(start, {}, &m_snapLine);
            if (m_snapLine >= 0) {
                start = snapped;
            } else {
                const qint64 end = snap(start + s->length, {}, &m_snapLine);
                if (m_snapLine >= 0)
                    start = std::max<qint64>(0, end - s->length);
            }
            m_subStart = start;
            m_subLength = s->length;
        } else {
            const qint64 frame = snap(std::max<qint64>(0, frameUnderMouse(pos.x())), {}, &m_snapLine);
            if (m_trimHead) {
                const qint64 start = std::clamp<qint64>(frame, 0, s->end() - 1);
                m_subStart = start;
                m_subLength = s->end() - start;
            } else {
                m_subStart = s->start;
                m_subLength = std::max<qint64>(1, frame - s->start);
            }
        }
        viewport()->update();
        return;
    }

    if (m_drag == Drag::Move) {
        const QList<QUuid> sel = selection();
        qint64 delta = qint64(std::llround((pos.x() - m_pressPos.x()) / m_ppf));
        delta = snapDelta(delta, sel, &m_snapLine);
        qint64 minStart = std::numeric_limits<qint64>::max();
        for (const QUuid &id : sel)
            if (const TimelineClip *c = tl.clip(id))
                minStart = std::min(minStart, c->start);
        if (minStart != std::numeric_limits<qint64>::max())
            delta = std::max(delta, -minStart);

        const Timeline::Kind kind = tl.tracks[m_dragTrack].kind;
        const int target = trackAtY(pos.y());
        if (target >= 0 && tl.tracks[target].kind == kind)
            m_moveTracks = kindIndex(target) - kindIndex(m_dragTrack);
        m_moveDelta = delta;

        Timeline t = tl;
        if (t.move(sel, m_moveDelta, kind, m_moveTracks))
            m_preview = t;
        else
            m_preview = tl; // not allowed (locked or missing track): show unchanged
    } else if (m_drag == Drag::Trim) {
        qint64 frame = std::max<qint64>(
            0, qint64(std::llround((pos.x() - kHeaderW + horizontalScrollBar()->value()) / m_ppf)));
        const QList<QUuid> linked = tl.withLinked({m_dragClip});
        frame = snap(frame, QSet<QUuid>(linked.begin(), linked.end()), &m_snapLine);
        m_trimFrame = frame;
        Timeline t = tl;
        t.trim(m_dragClip, m_trimHead, frame, [this](const QUuid &mid) { return m_ctl->mediaFrames(mid); });
        m_preview = t;
    } else if (m_drag == Drag::Roll) {
        const QList<QUuid> linked = tl.withLinked({m_dragClip});
        m_trimFrame = snap(frameUnderMouse(pos.x()), QSet<QUuid>(linked.begin(), linked.end()), &m_snapLine);
        Timeline t = tl;
        t.roll(m_dragClip, m_trimHead, m_trimFrame, [this](const QUuid &mid) { return m_ctl->mediaFrames(mid); });
        m_preview = t;
    } else if (m_drag == Drag::Slip || m_drag == Drag::Slide) {
        const qint64 dx = qint64(std::llround((pos.x() - m_pressPos.x()) / m_ppf));
        Timeline t = tl;
        if (m_drag == Drag::Slip) {
            // Dragging right shows an earlier part of the source.
            m_moveDelta = -dx;
            t.slip(m_dragClip, m_moveDelta, [this](const QUuid &mid) { return m_ctl->mediaFrames(mid); });
        } else {
            m_moveDelta = snapDelta(dx, tl.withLinked({m_dragClip}), &m_snapLine);
            t.slide(m_dragClip, m_moveDelta, [this](const QUuid &mid) { return m_ctl->mediaFrames(mid); });
        }
        m_preview = t;
    }
    viewport()->update();
}

qint64 TimelineView::frameUnderMouse(int x) const
{
    return std::max<qint64>(0, qint64(std::llround((x - kHeaderW + horizontalScrollBar()->value()) / m_ppf)));
}

void TimelineView::updateCursor(const QPoint &pos)
{
    if (pos.y() >= kRulerH && pos.y() < tracksTop()) {
        bool head = false, tail = false;
        const QUuid id = subtitleAt(pos, &head, &tail);
        viewport()->setCursor(id.isNull() ? Qt::ArrowCursor : (head || tail) ? Qt::SizeHorCursor : Qt::OpenHandCursor);
        return;
    }
    const Hit h = hitTest(pos);
    Qt::CursorShape shape = Qt::ArrowCursor;
    switch (m_tool) {
    case Tool::Select:
        shape = (h.headEdge || h.tailEdge) ? Qt::SizeHorCursor : Qt::ArrowCursor;
        break;
    case Tool::Razor:
        shape = h.clip.isNull() ? Qt::ArrowCursor : Qt::CrossCursor;
        break;
    case Tool::Roll:
        shape = (h.headEdge || h.tailEdge) ? Qt::SplitHCursor : Qt::ArrowCursor;
        break;
    case Tool::Slip:
    case Tool::Slide:
        shape = h.clip.isNull() ? Qt::ArrowCursor : Qt::SizeHorCursor;
        break;
    }
    viewport()->setCursor(shape);
}

void TimelineView::setTool(Tool tool)
{
    if (tool == m_tool)
        return;
    m_tool = tool;
    viewport()->setCursor(Qt::ArrowCursor);
    emit toolChanged(tool);
}

void TimelineView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return QAbstractScrollArea::mouseReleaseEvent(e);
    const Drag d = m_drag;
    m_drag = Drag::None;
    m_preview.reset();
    m_snapLine = -1;
    if (m_dragStarted) {
        const Timeline &tl = m_ctl->timeline();
        if (d == Drag::Move && m_dragTrack >= 0 && m_dragTrack < tl.tracks.size())
            m_ctl->moveClips(selection(), m_moveDelta, tl.tracks[m_dragTrack].kind, m_moveTracks);
        else if (d == Drag::Trim)
            m_ctl->trimClip(m_dragClip, m_trimHead, m_trimFrame);
        else if (d == Drag::Roll)
            m_ctl->rollClip(m_dragClip, m_trimHead, m_trimFrame);
        else if (d == Drag::Slip)
            m_ctl->slipClip(m_dragClip, m_moveDelta);
        else if (d == Drag::Slide)
            m_ctl->slideClip(m_dragClip, m_moveDelta);
        else if (d == Drag::SubMove || d == Drag::SubTrim)
            m_ctl->setSubtitleTiming(m_dragSub, m_subStart, m_subLength, false);
    }
    m_dragStarted = false;
    viewport()->update();
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent *e)
{
    // Double click on a nested clip opens its sequence; on another clip it
    // moves the playhead to the clip start. On the subtitle strip: a free
    // place gets a new subtitle, a subtitle opens for editing.
    const QPoint pos = e->position().toPoint();
    if (pos.y() >= kRulerH && pos.y() < tracksTop() && pos.x() >= kHeaderW) {
        QUuid id = subtitleAt(pos);
        if (id.isNull())
            id = m_ctl->addSubtitle(frameAtX(pos.x()));
        else if (const Subtitle *s = m_ctl->timeline().subtitle(id))
            m_ctl->setPlayhead(s->start);
        setSelectedSubtitle(id);
        if (!id.isNull())
            emit subtitleClicked(id);
        return;
    }
    const Hit h = hitTest(pos);
    if (const TimelineClip *c = m_ctl->timeline().clip(h.clip)) {
        if (c->isNested()) {
            const QUuid nest = c->nestId;
            m_selection.clear();
            emit selectionChanged();
            m_ctl->openSequence(nest);
        } else {
            m_ctl->setPlayhead(c->start);
        }
    }
}

void TimelineView::wheelEvent(QWheelEvent *e)
{
    const int dy = e->angleDelta().y();
    if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) {
        const double factor = std::pow(1.25, dy / 120.0);
        setPixelsPerFrame(m_ppf * factor, int(e->position().x()));
    } else if (e->modifiers() & Qt::ShiftModifier) {
        verticalScrollBar()->setValue(verticalScrollBar()->value() - dy / 2);
    } else {
        const int d = dy != 0 ? dy : e->angleDelta().x();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d);
    }
    e->accept();
}

void TimelineView::keyPressEvent(QKeyEvent *e)
{
    const qint64 ph = m_ctl->playhead();
    const qint64 big = std::max<qint64>(1, qint64(std::llround(m_ctl->project().sequence.fps())));
    switch (e->key()) {
    case Qt::Key_Left:
        m_ctl->setPlayhead(ph - ((e->modifiers() & Qt::ShiftModifier) ? big : 1));
        break;
    case Qt::Key_Right:
        m_ctl->setPlayhead(ph + ((e->modifiers() & Qt::ShiftModifier) ? big : 1));
        break;
    case Qt::Key_Home:
        m_ctl->setPlayhead(0);
        break;
    case Qt::Key_End:
        m_ctl->setPlayhead(m_ctl->timeline().duration());
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomIn();
        break;
    case Qt::Key_Minus:
        zoomOut();
        break;
    case Qt::Key_Backslash:
        zoomToFit();
        break;
    case Qt::Key_Escape:
        if (m_drag != Drag::None) {
            m_drag = Drag::None;
            m_preview.reset();
            m_snapLine = -1;
            m_dragStarted = false;
        } else {
            m_selection.clear();
            emit selectionChanged();
        }
        viewport()->update();
        break;
    default:
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    e->accept();
}

void TimelineView::contextMenuEvent(QContextMenuEvent *e)
{
    const QPoint pos = viewport()->mapFrom(this, e->pos());
    if (pos.x() < kHeaderW) {
        if (!m_trackActions.isEmpty())
            QMenu::exec(m_trackActions, e->globalPos(), nullptr, this);
        return;
    }
    const Hit h = hitTest(pos);
    if (!h.clip.isNull() && !m_selection.contains(h.clip)) {
        const QList<QUuid> group = m_ctl->timeline().withLinked({h.clip});
        m_selection = QSet<QUuid>(group.begin(), group.end());
        emit selectionChanged();
        viewport()->update();
    }
    if (!m_clipActions.isEmpty())
        QMenu::exec(m_clipActions, e->globalPos(), nullptr, this);
}

bool TimelineView::viewportEvent(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const QPoint pos = he->pos();
        const int t = trackAtY(pos.y());
        QString tip;
        if (const Subtitle *st = m_ctl->timeline().subtitle(subtitleAt(pos))) {
            const double fps = m_ctl->project().sequence.fps();
            tip = QStringLiteral("%1\n%2 – %3").arg(st->text, Fmt::frameTimecode(st->start, fps),
                                                   Fmt::frameTimecode(st->end(), fps));
        } else if (pos.y() >= kRulerH && pos.y() < tracksTop()) {
            tip = tr("Subtitles: drag to move, drag an edge to change the time, double-click a free place to add one");
        } else if (t >= 0 && pos.x() < kHeaderW) {
            const TimelineTrack &trk = m_ctl->timeline().tracks[t];
            if (headerButton(t, 0).contains(pos))
                tip = trk.locked ? tr("Unlock track") : tr("Lock track");
            else if (trk.kind == Timeline::Kind::Video && headerButton(t, 1).contains(pos))
                tip = trk.hidden ? tr("Show track in the Program monitor") : tr("Hide track in the Program monitor");
            else if (trk.kind == Timeline::Kind::Audio && headerButton(t, 2).contains(pos))
                tip = trk.muted ? tr("Turn the track sound on") : tr("Mute track");
            else if (trk.kind == Timeline::Kind::Audio && headerButton(t, 1).contains(pos))
                tip = trk.solo ? tr("Solo off") : tr("Solo: hear only this track (and other solo tracks)");
        } else if (t >= 0) {
            const Hit h = hitTest(pos);
            if (const TimelineClip *c = m_ctl->timeline().clip(h.clip)) {
                const double fps = m_ctl->project().sequence.fps();
                const MediaItem *m = m_ctl->project().item(c->mediaId);
                tip = QStringLiteral("%1\n%2 – %3 (%4)")
                          .arg(c->isTitle()    ? c->title.text.simplified()
                               : c->isNested() ? m_ctl->sequenceName(c->nestId)
                               : m             ? m->name
                                               : QString(), Fmt::frameTimecode(c->start, fps),
                               Fmt::frameTimecode(c->end(), fps), Fmt::frameTimecode(c->length, fps));
            }
        }
        if (tip.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(he->globalPos(), tip, viewport());
        return true;
    }
    return QAbstractScrollArea::viewportEvent(e);
}

// --- drop from the media bin

QList<QUuid> TimelineView::mediaFromMime(const QMimeData *mime) const
{
    QList<QUuid> ids;
    if (!mime || !mime->hasFormat(QString::fromLatin1(kMediaIdsMime)))
        return ids;
    for (const QByteArray &line : mime->data(QString::fromLatin1(kMediaIdsMime)).split('\n')) {
        const QUuid id(QString::fromLatin1(line.trimmed()));
        if (!id.isNull())
            ids << id;
    }
    return ids;
}

void TimelineView::updateDropPreview(const QPoint &pos, const QMimeData *mime)
{
    m_dropMedia = mediaFromMime(mime);
    const Timeline &tl = m_ctl->timeline();
    int track = trackAtY(pos.y());
    if (track < 0)
        track = tl.indexOf(Timeline::Kind::Video, 0);
    m_dropTrack = track;
    m_dropFrame = snap(frameAtX(pos.x()), {}, &m_snapLine);
    Timeline t = tl;
    if (m_ctl->previewPlacement(t, m_dropMedia, m_dropTrack, m_dropFrame))
        m_preview = t;
    else
        m_preview.reset();
    viewport()->update();
}

void TimelineView::dragEnterEvent(QDragEnterEvent *e)
{
    if (mediaFromMime(e->mimeData()).isEmpty()) {
        e->ignore();
        return;
    }
    e->setDropAction(Qt::CopyAction);
    e->accept();
    updateDropPreview(e->position().toPoint(), e->mimeData());
}

void TimelineView::dragMoveEvent(QDragMoveEvent *e)
{
    if (mediaFromMime(e->mimeData()).isEmpty()) {
        e->ignore();
        return;
    }
    e->setDropAction(Qt::CopyAction);
    e->accept();
    updateDropPreview(e->position().toPoint(), e->mimeData());
}

void TimelineView::dragLeaveEvent(QDragLeaveEvent *)
{
    m_preview.reset();
    m_snapLine = -1;
    viewport()->update();
}

void TimelineView::dropEvent(QDropEvent *e)
{
    updateDropPreview(e->position().toPoint(), e->mimeData());
    m_preview.reset();
    m_snapLine = -1;
    const QList<QUuid> created = m_ctl->placeMedia(m_dropMedia, m_dropTrack, m_dropFrame);
    // Copy, not move: the bin must keep its items.
    e->setDropAction(Qt::CopyAction);
    e->accept();
    if (!created.isEmpty())
        setSelection(created);
    setFocus();
    viewport()->update();
}

// ---------------------------------------------------------------------------
// TimelinePanel

TimelinePanel::TimelinePanel(ProjectController *controller, QWidget *parent)
    : QWidget(parent)
    , m_ctl(controller)
    , m_view(new TimelineView(controller, this))
    , m_timecode(new QLabel(this))
    , m_duration(new QLabel(this))
    , m_zoom(new QSlider(Qt::Horizontal, this))
    , m_snapBtn(new QToolButton(this))
    , m_splitBtn(new QToolButton(this))
    , m_fitBtn(new QToolButton(this))
{
    setObjectName(QStringLiteral("timelinePanel"));

    auto makeAction = [this](const QString &name, const QKeySequence &key) {
        auto *a = new QAction(this);
        a->setObjectName(name);
        if (!key.isEmpty()) {
            a->setShortcut(key);
            // Active while the timeline has focus, so Delete in the bin keeps
            // deleting bin items.
            a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        }
        addAction(a);
        return a;
    };
    m_split = makeAction(QStringLiteral("actSplit"), QKeySequence(QStringLiteral("Ctrl+K")));
    m_delete = makeAction(QStringLiteral("actDelete"), QKeySequence(QKeySequence::Delete));
    m_rippleDelete = makeAction(QStringLiteral("actRippleDelete"), QKeySequence(QStringLiteral("Shift+Del")));
    m_snapAct = makeAction(QStringLiteral("actSnapping"), QKeySequence(QStringLiteral("S")));
    m_snapAct->setCheckable(true);
    m_snapAct->setChecked(true);
    m_addVideo = makeAction(QStringLiteral("actAddVideoTrack"), {});
    m_addAudio = makeAction(QStringLiteral("actAddAudioTrack"), {});
    m_selectAll = makeAction(QStringLiteral("actSelectAll"), QKeySequence(QKeySequence::SelectAll));
    m_copy = makeAction(QStringLiteral("actCopy"), QKeySequence(QKeySequence::Copy));
    m_cut = makeAction(QStringLiteral("actCut"), QKeySequence(QKeySequence::Cut));
    m_paste = makeAction(QStringLiteral("actPaste"), QKeySequence(QKeySequence::Paste));
    m_pasteInsert = makeAction(QStringLiteral("actPasteInsert"), QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    m_link = makeAction(QStringLiteral("actLink"), QKeySequence(QStringLiteral("Ctrl+L")));
    m_nest = makeAction(QStringLiteral("actNest"), {});
    m_speed = makeAction(QStringLiteral("actSpeed"), QKeySequence(QStringLiteral("Ctrl+R")));
    m_transition = makeAction(QStringLiteral("actAddTransition"), QKeySequence(QStringLiteral("Ctrl+D")));
    connect(m_transition, &QAction::triggered, this,
            [this] { m_ctl->addDefaultTransitions(m_view->selection(), m_ctl->playhead()); });
    connect(m_speed, &QAction::triggered, this, [this] {
        const QUuid id = speedClip();
        if (id.isNull())
            return;
        SpeedDialog dlg(m_ctl, id, this);
        dlg.exec();
    });
    m_loudness = makeAction(QStringLiteral("actNormalizeLoudness"), {});
    connect(m_loudness, &QAction::triggered, this, [this] {
        LoudnessDialog dlg(m_ctl, m_view->selection(), this);
        dlg.exec();
    });
    m_renameSeq = makeAction(QStringLiteral("actRenameSequence"), {});
    connect(m_nest, &QAction::triggered, this, [this] {
        const QUuid id = m_ctl->nestClips(m_view->selection());
        if (!id.isNull()) {
            // Select the new nested clip(s).
            QList<QUuid> made;
            for (const auto &t : m_ctl->timeline().tracks)
                for (const auto &c : t.clips)
                    if (c.nestId == id)
                        made << c.id;
            m_view->setSelection(made);
        }
    });
    connect(m_renameSeq, &QAction::triggered, this, [this] {
        const QUuid id = m_ctl->openNest();
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename sequence"), tr("Sequence name:"),
                                                   QLineEdit::Normal, m_ctl->sequenceName(id), &ok);
        if (ok)
            m_ctl->renameSequence(id, name);
    });
    connect(m_copy, &QAction::triggered, this, [this] { m_ctl->copyClips(m_view->selection()); updateState(); });
    connect(m_cut, &QAction::triggered, this, [this] { m_ctl->cutClips(m_view->selection()); });
    connect(m_paste, &QAction::triggered, this, [this] { m_view->setSelection(m_ctl->pasteClips(false)); });
    connect(m_pasteInsert, &QAction::triggered, this, [this] { m_view->setSelection(m_ctl->pasteClips(true)); });
    connect(m_link, &QAction::triggered, this, [this] { m_ctl->toggleLink(m_view->selection()); });

    // Tools: one at a time.
    auto *tools = new QActionGroup(this);
    const struct { const char *name; const char *key; TimelineView::Tool tool; } toolDefs[] = {
        {"actToolSelect", "V", TimelineView::Tool::Select}, {"actToolRazor", "C", TimelineView::Tool::Razor},
        {"actToolRoll", "N", TimelineView::Tool::Roll},     {"actToolSlip", "Y", TimelineView::Tool::Slip},
        {"actToolSlide", "U", TimelineView::Tool::Slide},
    };
    for (const auto &d : toolDefs) {
        QAction *a = makeAction(QString::fromLatin1(d.name), QKeySequence(QString::fromLatin1(d.key)));
        a->setCheckable(true);
        a->setChecked(d.tool == TimelineView::Tool::Select);
        tools->addAction(a);
        const TimelineView::Tool tool = d.tool;
        connect(a, &QAction::triggered, this, [this, tool] { m_view->setTool(tool); });
        m_tools << a;
    }
    connect(m_view, &TimelineView::toolChanged, this, [this](TimelineView::Tool t) {
        m_tools.value(int(t))->setChecked(true);
    });

    connect(m_split, &QAction::triggered, this, &TimelinePanel::split);
    connect(m_delete, &QAction::triggered, this, [this] { deleteSelection(false); });
    connect(m_rippleDelete, &QAction::triggered, this, [this] { deleteSelection(true); });
    connect(m_snapAct, &QAction::toggled, m_view, &TimelineView::setSnapping);
    connect(m_addVideo, &QAction::triggered, this, [this] { m_ctl->addTrack(Timeline::Kind::Video); });
    connect(m_addAudio, &QAction::triggered, this, [this] { m_ctl->addTrack(Timeline::Kind::Audio); });
    connect(m_selectAll, &QAction::triggered, this, [this] {
        QList<QUuid> all;
        for (const auto &t : m_ctl->timeline().tracks)
            for (const auto &c : t.clips)
                all << c.id;
        m_view->setSelection(all);
    });

    m_view->setContextActions({m_split, m_cut, m_copy, m_paste, m_link, m_nest, m_speed, m_loudness, m_transition, m_delete, m_rippleDelete},
                              {m_addVideo, m_addAudio});

    m_snapBtn->setDefaultAction(m_snapAct);
    m_snapBtn->setObjectName(QStringLiteral("timelineSnapButton"));
    m_splitBtn->setDefaultAction(m_split);
    m_fitBtn->setObjectName(QStringLiteral("timelineFitButton"));
    connect(m_fitBtn, &QToolButton::clicked, m_view, &TimelineView::zoomToFit);
    for (auto *b : {m_snapBtn, m_splitBtn, m_fitBtn})
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_zoom->setObjectName(QStringLiteral("timelineZoom"));
    m_zoom->setRange(0, 1000);
    m_zoom->setFixedWidth(140);
    auto ppfToSlider = [](double ppf) {
        return int(std::lround(1000.0 * std::log(ppf / kMinPpf) / std::log(kMaxPpf / kMinPpf)));
    };
    m_zoom->setValue(ppfToSlider(m_view->pixelsPerFrame()));
    connect(m_zoom, &QSlider::valueChanged, this, [this](int v) {
        const double ppf = kMinPpf * std::pow(kMaxPpf / kMinPpf, v / 1000.0);
        m_view->setPixelsPerFrame(ppf);
    });
    connect(m_view, &TimelineView::zoomChanged, this, [this, ppfToSlider](double ppf) {
        QSignalBlocker b(m_zoom);
        m_zoom->setValue(ppfToSlider(ppf));
    });

    m_timecode->setObjectName(QStringLiteral("timelineTimecode"));
    m_timecode->setStyleSheet(QStringLiteral("color:#d1a54a; font-family:Consolas,monospace; font-size:14px; padding:0 6px;"));
    m_duration->setStyleSheet(QStringLiteral("color:#8a8a8a; padding:0 6px;"));

    // Which sequence the timeline shows: the main one or a nested one.
    m_sequences = new QComboBox(this);
    m_sequences->setObjectName(QStringLiteral("timelineSequence"));
    m_sequences->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(m_sequences, &QComboBox::activated, this,
            [this](int i) { m_ctl->openSequence(m_sequences->itemData(i).toUuid()); });
    auto *renameBtn = new QToolButton(this);
    renameBtn->setDefaultAction(m_renameSeq);
    renameBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    renameBtn->setAutoRaise(true);
    renameBtn->setObjectName(QStringLiteral("timelineRenameSequence"));
    connect(m_ctl, &ProjectController::openSequenceChanged, this, &TimelinePanel::rebuildSequences);
    connect(m_ctl, &ProjectController::timelineChanged, this, &TimelinePanel::rebuildSequences);
    connect(m_ctl, &ProjectController::projectReset, this, &TimelinePanel::rebuildSequences);
    connect(m_ctl, &ProjectController::openSequenceChanged, m_view, [this] { m_view->setSelection({}); });

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(4, 2, 4, 2);
    bar->setSpacing(4);
    bar->addWidget(m_sequences);
    bar->addWidget(renameBtn);
    bar->addWidget(m_timecode);
    bar->addWidget(m_snapBtn);
    bar->addWidget(m_splitBtn);
    bar->addSpacing(12);
    for (QAction *a : m_tools) {
        auto *b = new QToolButton(this);
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setAutoRaise(true);
        b->setObjectName(QStringLiteral("tb_") + a->objectName());
        b->setStyleSheet(QStringLiteral("QToolButton:checked { background:#3a3a3a; color:#d1a54a; border-radius:3px; }"));
        bar->addWidget(b);
        m_toolButtons << b;
    }
    bar->addStretch();
    bar->addWidget(m_duration);
    bar->addWidget(m_zoom);
    bar->addWidget(m_fitBtn);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addLayout(bar);
    lay->addWidget(m_view, 1);

    connect(m_ctl, &ProjectController::timelineChanged, this, &TimelinePanel::updateState);
    connect(m_ctl, &ProjectController::playheadChanged, this, &TimelinePanel::updateState);
    connect(m_ctl, &ProjectController::sequenceChanged, this, &TimelinePanel::updateState);
    connect(m_ctl, &ProjectController::projectReset, this, &TimelinePanel::updateState);
    connect(m_view, &TimelineView::selectionChanged, this, &TimelinePanel::updateState);
    connect(m_ctl, &ProjectController::clipboardChanged, this, &TimelinePanel::updateState);

    retranslate();
    updateState();
}

void TimelinePanel::rebuildSequences()
{
    const QSignalBlocker b(m_sequences);
    m_sequences->clear();
    m_sequences->addItem(m_ctl->sequenceName({}), QUuid());
    for (const NestedSequence &n : m_ctl->project().nested)
        m_sequences->addItem(QStringLiteral("\u29c9 ") + n.name, n.id);
    m_sequences->setCurrentIndex(std::max(0, m_sequences->findData(m_ctl->openNest())));
    m_sequences->setVisible(m_sequences->count() > 1 || !m_ctl->openNest().isNull());
    m_renameSeq->setEnabled(!m_ctl->openNest().isNull());
}

void TimelinePanel::split()
{
    const qint64 ph = m_ctl->playhead();
    // Selected clips under the playhead, or every clip under it.
    QList<QUuid> under;
    for (const QUuid &id : m_view->selection())
        if (const TimelineClip *c = m_ctl->timeline().clip(id); c && ph > c->start && ph < c->end())
            under << id;
    m_ctl->splitClips(ph, under);
}

void TimelinePanel::deleteSelection(bool ripple)
{
    const QList<QUuid> sel = m_view->selection();
    if (!sel.isEmpty())
        m_ctl->removeClips(sel, ripple);
}

QUuid TimelinePanel::speedClip() const
{
    const Timeline &tl = m_ctl->timeline();
    QSet<QUuid> groups;
    QUuid found;
    for (const QUuid &id : m_view->selection()) {
        const TimelineClip *c = tl.clip(id);
        if (!c)
            continue;
        groups.insert(c->linkId.isNull() ? c->id : c->linkId);
        if (found.isNull() && m_ctl->canChangeSpeed(id))
            found = id;
    }
    return groups.size() == 1 ? found : QUuid();
}

void TimelinePanel::updateState()
{
    const Timeline &tl = m_ctl->timeline();
    const double fps = m_ctl->project().sequence.fps();
    m_timecode->setText(Fmt::frameTimecode(m_ctl->playhead(), fps));
    m_duration->setText(tr("Duration: %1").arg(Fmt::frameTimecode(tl.duration(), fps)));
    const bool hasSel = !m_view->selection().isEmpty();
    m_delete->setEnabled(hasSel);
    m_rippleDelete->setEnabled(hasSel);
    bool underPlayhead = false;
    for (const auto &t : tl.tracks)
        for (const auto &c : t.clips)
            underPlayhead |= m_ctl->playhead() > c.start && m_ctl->playhead() < c.end();
    m_split->setEnabled(underPlayhead);
    m_copy->setEnabled(hasSel);
    m_cut->setEnabled(hasSel);
    m_paste->setEnabled(m_ctl->canPaste());
    m_pasteInsert->setEnabled(m_ctl->canPaste());
    const QList<QUuid> sel = m_view->selection();
    bool linked = false;
    for (const QUuid &id : sel)
        linked |= tl.isLinked(id);
    m_link->setEnabled(linked || sel.size() >= 2);
    m_nest->setEnabled(hasSel);
    m_speed->setEnabled(!speedClip().isNull());
    m_loudness->setEnabled(!m_ctl->loudnessClips(sel).isEmpty());
    m_transition->setEnabled(hasSel);
    m_link->setText(linked ? tr("Unlink audio and video") : tr("Link audio and video"));
}

void TimelinePanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslate();
    QWidget::changeEvent(e);
}

void TimelinePanel::retranslate()
{
    m_split->setText(tr("Split at playhead"));
    m_delete->setText(tr("Delete"));
    m_rippleDelete->setText(tr("Ripple delete"));
    m_snapAct->setText(tr("Snapping"));
    m_snapAct->setToolTip(tr("Clips stick to edges and the playhead (S)"));
    m_addVideo->setText(tr("Add video track"));
    m_addAudio->setText(tr("Add audio track"));
    m_selectAll->setText(tr("Select all clips"));
    m_fitBtn->setText(tr("Fit"));
    m_fitBtn->setToolTip(tr("Show the whole sequence (\\)"));
    m_zoom->setToolTip(tr("Zoom (Ctrl + mouse wheel)"));
    m_split->setToolTip(tr("Split at playhead (Ctrl+K)"));
    m_copy->setText(tr("Copy"));
    m_nest->setText(tr("Nest"));
    m_speed->setText(tr("Speed / duration..."));
    m_transition->setText(tr("Add transition"));
    m_transition->setToolTip(tr("A 1-second dissolve at the edit of the selected clips nearest the playhead (Ctrl+D)"));
    m_speed->setToolTip(tr("Play the clip faster or slower (Ctrl+R)"));
    m_loudness->setText(tr("Normalize loudness..."));
    m_loudness->setToolTip(tr("Measure the selected audio clips (LUFS) and set their volume to a target loudness"));
    m_nest->setToolTip(tr("Put the selected clips into a new nested sequence that works as one clip"));
    m_renameSeq->setText(tr("Rename..."));
    m_renameSeq->setToolTip(tr("Rename the nested sequence shown on the timeline"));
    rebuildSequences();
    m_cut->setText(tr("Cut"));
    m_paste->setText(tr("Paste"));
    m_paste->setToolTip(tr("Paste at the playhead over what is there (Ctrl+V)"));
    m_pasteInsert->setText(tr("Paste insert"));
    m_pasteInsert->setToolTip(tr("Paste at the playhead; later clips move right (Ctrl+Shift+V)"));
    const char *const toolText[] = {QT_TR_NOOP("Selection"), QT_TR_NOOP("Razor"), QT_TR_NOOP("Rolling edit"),
                                    QT_TR_NOOP("Slip"), QT_TR_NOOP("Slide")};
    const char *const toolTip[] = {
        QT_TR_NOOP("Select, move and trim clips (V)"),
        QT_TR_NOOP("Click a clip to split it there (C)"),
        QT_TR_NOOP("Drag the cut between two clips: one gets longer, the other shorter (N)"),
        QT_TR_NOOP("Drag inside a clip to use another part of its source in the same place (Y)"),
        QT_TR_NOOP("Drag a clip between its neighbours; they get longer or shorter (U)")};
    for (int i = 0; i < m_tools.size(); ++i) {
        m_tools[i]->setText(tr(toolText[i]));
        m_tools[i]->setToolTip(tr(toolTip[i]));
    }
    updateState();
}

} // namespace bc
