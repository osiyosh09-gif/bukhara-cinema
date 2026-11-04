#pragma once

#include "core/Timeline.h"

#include <QAbstractScrollArea>
#include <QSet>
#include <QUuid>
#include <QWidget>
#include <optional>

class QAction;
class QLabel;
class QMimeData;
class QSlider;
class QToolButton;
class QComboBox;

namespace bc {

class ProjectController;

// Draws the timeline and turns mouse gestures into controller edits:
// drop media from the bin, select, move (also to another track), trim the
// ends, scrub the playhead, zoom. While a gesture is running, the edit is
// applied to a copy of the timeline and that copy is drawn, so the preview
// shows exactly what releasing the mouse will do.
class TimelineView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit TimelineView(ProjectController *controller, QWidget *parent = nullptr);

    // What a mouse drag on a clip does.
    enum class Tool {
        Select, // select, move, trim the ends
        Razor,  // click: split the clip there
        Roll,   // drag a cut between two clips
        Slip,   // drag inside a clip: other part of the source, same place
        Slide,  // drag a clip between its neighbours
    };
    Tool tool() const { return m_tool; }
    void setTool(Tool tool);

    QList<QUuid> selection() const { return QList<QUuid>(m_selection.begin(), m_selection.end()); }
    void setSelection(const QList<QUuid> &ids);

    bool snapping() const { return m_snapping; }
    void setSnapping(bool on) { m_snapping = on; }

    double pixelsPerFrame() const { return m_ppf; }
    void setPixelsPerFrame(double ppf, int anchorX = -1);
    void zoomIn();
    void zoomOut();
    void zoomToFit();

    // Actions shown in the right-click menu on clips and on track headers.
    void setContextActions(const QList<QAction *> &clipActions, const QList<QAction *> &trackActions)
    {
        m_clipActions = clipActions;
        m_trackActions = trackActions;
    }

    // Geometry helpers (also used by tests).
    int headerWidth() const { return kHeaderW; }
    int rulerHeight() const { return kRulerH; }
    qint64 frameAtX(int x) const;
    int xForFrame(qint64 frame) const;
    int trackAtY(int y) const;  // index into timeline().tracks, -1 if none
    QRect trackRect(int track) const;

signals:
    void selectionChanged();
    void zoomChanged(double ppf);
    void toolChanged(TimelineView::Tool tool);

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void scrollContentsBy(int dx, int dy) override;
    bool viewportEvent(QEvent *e) override;

private:
    static constexpr int kHeaderW = 96;
    static constexpr int kRulerH = 26;
    static constexpr int kVideoH = 42;
    static constexpr int kAudioH = 34;
    static constexpr int kEdgePx = 6;
    static constexpr int kSnapPx = 8;

    enum class Drag { None, Scrub, Move, Trim, Roll, Slip, Slide };

    struct Hit
    {
        QUuid clip;
        int track = -1;
        bool headEdge = false;
        bool tailEdge = false;
    };

    const Timeline &shown() const; // preview while dragging, else the project timeline
    QList<int> rows() const;       // track indices top to bottom: Vn..V1, A1..An
    int rowTop(int row) const;
    int rowHeight(int track) const;
    int contentHeight() const;
    Hit hitTest(const QPoint &p) const;
    QRect clipRect(int track, const TimelineClip &c) const;
    QRect headerButton(int track, int which) const; // 0 = lock, 1 = eye / mute
    qint64 snap(qint64 frame, const QSet<QUuid> &exclude, qint64 *snappedTo) const;
    qint64 snapDelta(qint64 delta, const QList<QUuid> &moving, qint64 *snappedTo) const;
    int kindIndex(int track) const; // n-th track of its kind
    QList<QUuid> mediaFromMime(const QMimeData *mime) const;
    void updateDropPreview(const QPoint &pos, const QMimeData *mime);
    void updateScrollBars();
    void ensurePlayheadVisible();
    void drawRuler(QPainter &p);
    void drawTracks(QPainter &p);
    void drawClip(QPainter &p, int track, const TimelineClip &c);
    void updateCursor(const QPoint &pos);
    qint64 frameUnderMouse(int x) const;

    ProjectController *m_ctl;
    QList<QAction *> m_clipActions;
    QList<QAction *> m_trackActions;
    double m_ppf = 4.0;
    bool m_snapping = true;
    Tool m_tool = Tool::Select;
    QSet<QUuid> m_selection;

    Drag m_drag = Drag::None;
    QPoint m_pressPos;
    bool m_dragStarted = false;
    QUuid m_dragClip;
    int m_dragTrack = -1;
    bool m_trimHead = false;
    std::optional<Timeline> m_preview;
    qint64 m_snapLine = -1;

    // committed on release
    qint64 m_moveDelta = 0;
    int m_moveTracks = 0;
    qint64 m_trimFrame = 0;

    // drop from the media bin
    QList<QUuid> m_dropMedia;
    int m_dropTrack = -1;
    qint64 m_dropFrame = 0;
};

// Bottom panel: tool bar (snapping, split, zoom, timecode) and the timeline.
// Owns the editing actions so the main menu and the shortcuts share them.
class TimelinePanel : public QWidget
{
    Q_OBJECT
public:
    explicit TimelinePanel(ProjectController *controller, QWidget *parent = nullptr);

    TimelineView *view() const { return m_view; }

    QAction *splitAction() const { return m_split; }
    QAction *deleteAction() const { return m_delete; }
    QAction *rippleDeleteAction() const { return m_rippleDelete; }
    QAction *snappingAction() const { return m_snapAct; }
    QAction *addVideoTrackAction() const { return m_addVideo; }
    QAction *addAudioTrackAction() const { return m_addAudio; }
    QAction *selectAllAction() const { return m_selectAll; }
    QAction *copyAction() const { return m_copy; }
    QAction *cutAction() const { return m_cut; }
    QAction *pasteAction() const { return m_paste; }
    QAction *pasteInsertAction() const { return m_pasteInsert; }
    QAction *linkAction() const { return m_link; }
    QAction *nestAction() const { return m_nest; }
    QAction *speedAction() const { return m_speed; }
    // The selected clip whose speed can change (null when none or several).
    QUuid speedClip() const;
    QAction *renameSequenceAction() const { return m_renameSeq; }
    // Select, Razor, Roll, Slip, Slide (checkable, one at a time).
    QList<QAction *> toolActions() const { return m_tools; }

    void split();
    void deleteSelection(bool ripple);

protected:
    void changeEvent(QEvent *e) override;

private:
    void retranslate();
    void updateState();

    ProjectController *m_ctl;
    TimelineView *m_view;
    QLabel *m_timecode;
    QLabel *m_duration;
    QSlider *m_zoom;
    QToolButton *m_snapBtn;
    QToolButton *m_splitBtn;
    QToolButton *m_fitBtn;

    QAction *m_split;
    QAction *m_delete;
    QAction *m_rippleDelete;
    QAction *m_snapAct;
    QAction *m_addVideo;
    QAction *m_addAudio;
    QAction *m_selectAll;
    QAction *m_copy;
    QAction *m_cut;
    QAction *m_paste;
    QAction *m_pasteInsert;
    QAction *m_link;
    QAction *m_nest;
    QAction *m_speed;
    QAction *m_renameSeq;
    QComboBox *m_sequences;
    void rebuildSequences();
    QList<QAction *> m_tools;
    QList<QToolButton *> m_toolButtons;
};

} // namespace bc
