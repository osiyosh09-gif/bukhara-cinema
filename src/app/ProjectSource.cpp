// Source monitor, In/Out points, insert/overwrite edits, J/K/L shuttle and
// markers (phase 6, part 1). Part of ProjectController.

#include "app/ProjectController.h"

#include "core/MediaCache.h"
#include "core/MediaProbe.h"
#include "engine/TimelinePlayer.h"

#include <QPointer>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace bc {

namespace {
constexpr int kSourceW = 960;
constexpr int kSourceH = 540;
constexpr double kMaxShuttle = 8;
}

bool ProjectController::setSourceMedia(const QUuid &id)
{
    if (!id.isNull()) {
        const MediaItem *m = m_project.item(id);
        if (!m || !m->probed || !isOnline(id))
            return false;
    }
    if (id == m_sourceId)
        return true;
    sourcePause();
    m_sourceId = id;
    m_sourcePos = std::clamp<qint64>(m_inPoints.value(id, 0), 0, std::max<qint64>(0, sourceDuration() - 1));
    m_sourceKey.clear();
    syncSourcePlayer();
    emit sourceChanged();
    emit sourceInOutChanged();
    emit sourcePositionChanged(m_sourcePos);
    requestSourceFrame();
    return true;
}

qint64 ProjectController::sourceDuration() const
{
    const MediaItem *m = m_project.item(m_sourceId);
    return m ? defaultClipFrames(*m, m_project.sequence) : 0;
}

void ProjectController::setSourcePosition(qint64 frame)
{
    if (m_sourceId.isNull())
        return;
    frame = std::clamp<qint64>(frame, 0, std::max<qint64>(0, sourceDuration() - 1));
    if (frame == m_sourcePos)
        return;
    const bool wasPlaying = isSourcePlaying();
    const double speed = wasPlaying ? m_sourcePlayer->speed() : 1;
    if (wasPlaying)
        m_sourcePlayer->pause();
    m_sourcePos = frame;
    emit sourcePositionChanged(frame);
    if (wasPlaying)
        m_sourcePlayer->play(frame, speed);
    else
        requestSourceFrame();
}

bool ProjectController::isSourcePlaying() const
{
    return m_sourcePlayer && m_sourcePlayer->isPlaying();
}

void ProjectController::toggleSourcePlay()
{
    if (isSourcePlaying()) {
        sourcePause();
        return;
    }
    if (!m_sourcePlayer || m_sourceId.isNull())
        return;
    pause(); // one monitor plays at a time (one sound device)
    if (m_sourcePos >= sourceDuration() - 1) {
        m_sourcePos = 0;
        emit sourcePositionChanged(0);
    }
    m_sourcePlayer->play(m_sourcePos, 1);
}

void ProjectController::sourcePause()
{
    if (m_sourcePlayer)
        m_sourcePlayer->pause();
}

void ProjectController::setSourceIn(qint64 frame)
{
    if (m_sourceId.isNull())
        return;
    frame = std::clamp<qint64>(frame, 0, std::max<qint64>(0, sourceDuration() - 1));
    m_inPoints.insert(m_sourceId, frame);
    if (m_outPoints.contains(m_sourceId) && m_outPoints.value(m_sourceId) < frame)
        m_outPoints.remove(m_sourceId);
    emit sourceInOutChanged();
}

void ProjectController::setSourceOut(qint64 frame)
{
    if (m_sourceId.isNull())
        return;
    frame = std::clamp<qint64>(frame, 0, std::max<qint64>(0, sourceDuration() - 1));
    m_outPoints.insert(m_sourceId, frame);
    if (m_inPoints.contains(m_sourceId) && m_inPoints.value(m_sourceId) > frame)
        m_inPoints.remove(m_sourceId);
    emit sourceInOutChanged();
}

void ProjectController::clearSourceInOut()
{
    m_inPoints.remove(m_sourceId);
    m_outPoints.remove(m_sourceId);
    emit sourceInOutChanged();
}

bool ProjectController::sourceRange(const QUuid &mediaId, qint64 *in, qint64 *length) const
{
    const MediaItem *m = m_project.item(mediaId);
    if (!m || !m->probed || !isOnline(mediaId))
        return false;
    const qint64 dur = defaultClipFrames(*m, m_project.sequence);
    if (dur <= 0)
        return false;
    qint64 a = std::clamp<qint64>(m_inPoints.value(mediaId, 0), 0, dur - 1);
    qint64 b = std::clamp<qint64>(m_outPoints.value(mediaId, dur - 1), 0, dur - 1);
    if (b < a)
        b = dur - 1;
    if (in)
        *in = a;
    if (length)
        *length = b - a + 1;
    return true;
}

bool ProjectController::sourceEdit(bool insert)
{
    const MediaItem *m = m_project.item(m_sourceId);
    qint64 in = 0, len = 0;
    if (!m || !sourceRange(m_sourceId, &in, &len))
        return false;
    const Timeline &tl = timeline();
    auto firstFree = [&](Timeline::Kind kind) {
        for (int n = 0; n < tl.count(kind); ++n)
            if (!tl.tracks[tl.indexOf(kind, n)].locked)
                return n;
        return -1;
    };
    const int v = m->info.hasVideo() ? firstFree(Timeline::Kind::Video) : -1;
    const int a = m->info.hasAudio() ? firstFree(Timeline::Kind::Audio) : -1;
    if (v < 0 && a < 0)
        return false;
    const qint64 at = m_playhead;
    const MediaItem item = *m;
    const bool ok = editTimeline(insert ? tr("Insert") : tr("Overwrite"), [&](Timeline &t) {
        if (insert)
            t.insertSpace(at, len);
        return !t.place(item, v, a, at, in, len).isEmpty();
    });
    if (ok)
        setPlayhead(at + len);
    return ok;
}

bool ProjectController::insertEdit()
{
    return sourceEdit(true);
}

bool ProjectController::overwriteEdit()
{
    return sourceEdit(false);
}

double ProjectController::speed(Monitor monitor) const
{
    const TimelinePlayer *p = monitor == Monitor::Source ? m_sourcePlayer : m_player;
    return p ? p->speed() : 0;
}

void ProjectController::shuttle(Monitor monitor, int direction)
{
    TimelinePlayer *p = monitor == Monitor::Source ? m_sourcePlayer : m_player;
    if (!p)
        return;
    if (direction == 0) {
        p->pause();
        return;
    }
    const double cur = p->speed();
    const double next = direction > 0 ? (cur > 0 ? std::min(cur * 2, kMaxShuttle) : 1.0)
                                       : (cur < 0 ? std::max(cur * 2, -kMaxShuttle) : -1.0);
    if (p->isPlaying()) {
        p->setSpeed(next);
        return;
    }
    qint64 from = 0, dur = 0;
    if (monitor == Monitor::Source) {
        if (m_sourceId.isNull())
            return;
        from = m_sourcePos;
        dur = sourceDuration();
    } else {
        if (timeline().isEmpty())
            return;
        from = m_playhead;
        dur = timeline().duration();
    }
    if (next > 0 && from >= dur - 1)
        from = 0;
    if (next < 0 && from <= 0)
        return; // nothing to play backwards
    // One monitor plays at a time (one sound device).
    if (monitor == Monitor::Source)
        pause();
    else
        sourcePause();
    p->play(from, next);
}

// ---------------------------------------------------------------------------
// Source graph and frames

void ProjectController::syncSourcePlayer()
{
    const MediaItem *m = m_project.item(m_sourceId);
    if (!m_sourceId.isNull() && (!m || !m->probed || !isOnline(m_sourceId))) {
        // The item was removed or went offline: close the Source monitor.
        sourcePause();
        m_sourceId = QUuid();
        m_sourcePos = 0;
        m_sourceKey.clear();
        ++m_sourceGen;
        if (m_sourcePlayer)
            m_sourcePlayer->setProject(Project());
        emit sourceChanged();
        emit sourceInOutChanged();
        emit sourceFrameReady({});
        return;
    }
    if (!m)
        return;
    const SequenceSettings &seq = m_project.sequence;
    const QString proxy = m_useProxies ? m_proxies.value(m->id) : QString();
    const QString key = QStringLiteral("%1|%2|%3|%4x%5@%6/%7|%8|%9")
                            .arg(m->id.toString(), m->path)
                            .arg(m->info.durationUs)
                            .arg(seq.width)
                            .arg(seq.height)
                            .arg(seq.fpsNum)
                            .arg(seq.fpsDen)
                            .arg(seq.sampleRate)
                            .arg(proxy);
    if (key == m_sourceKey)
        return;
    m_sourceKey = key;
    ++m_sourceGen;
    const qint64 dur = sourceDuration();
    if (m_sourcePos > dur - 1) {
        m_sourcePos = std::max<qint64>(0, dur - 1);
        emit sourcePositionChanged(m_sourcePos);
    }
    if (!m_sourcePlayer)
        return;
    // A one-clip project: the same engine as the timeline, so the Source
    // monitor shows and sounds exactly like the clip will.
    Project p;
    p.sequence = seq;
    p.items = {*m};
    p.timeline = Timeline::makeDefault();
    p.timeline.place(*m, seq, 0, 0, 0);
    QHash<QUuid, QString> proxies;
    if (!proxy.isEmpty())
        proxies.insert(m->id, proxy);
    m_sourcePlayer->setProxies(proxies);
    m_sourcePlayer->setProject(p);
}

void ProjectController::requestSourceFrame()
{
    if (isSourcePlaying() || m_sourceId.isNull())
        return;
    if (m_sourceBusy) {
        m_sourcePending = true;
        return;
    }
    startSourceFrame();
}

void ProjectController::startSourceFrame()
{
    m_sourcePending = false;
    const MediaItem *m = m_project.item(m_sourceId);
    if (!m)
        return;
    m_sourceBusy = true;
    QPointer<ProjectController> self(this);
    TimelinePlayer *player = m_sourcePlayer;
    const QString path = m->info.hasVideo() ? m->path : QString();
    const double fps = m_project.sequence.fps();
    const qint64 frame = m_sourcePos;
    const qint64 timeUs = fps > 0 ? qint64(std::llround(frame * 1e6 / fps)) : 0;
    const quint64 gen = m_sourceGen;
    (void)QtConcurrent::run(&m_pool, [self, player, path, frame, timeUs, gen] {
        QImage img = player ? player->renderFrame(frame) : QImage();
        if (img.isNull() && !path.isEmpty())
            img = MediaProbe::frameAt(path, timeUs, kSourceW, kSourceH);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, img, frame, gen] {
            if (!self)
                return;
            self->m_sourceBusy = false;
            if (gen == self->m_sourceGen && frame == self->m_sourcePos && !self->isSourcePlaying())
                emit self->sourceFrameReady(img);
            else if (!self->isSourcePlaying())
                self->m_sourcePending = true; // stale: render the current one
            if (self->m_sourcePending)
                self->startSourceFrame();
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// Markers

QUuid ProjectController::addMarker(qint64 frame)
{
    frame = std::max<qint64>(0, frame);
    if (const TimelineMarker *m = timeline().markerAt(frame))
        return m->id;
    QUuid id;
    editTimeline(tr("Add marker"), [&](Timeline &t) {
        id = t.addMarker(frame);
        return true;
    });
    return id;
}

bool ProjectController::removeMarker(const QUuid &id)
{
    return editTimeline(tr("Delete marker"), [&](Timeline &t) { return t.removeMarker(id); });
}

bool ProjectController::renameMarker(const QUuid &id, const QString &name)
{
    return editTimeline(tr("Rename marker"), [&](Timeline &t) { return t.renameMarker(id, name); });
}

bool ProjectController::goToNextMarker()
{
    const qint64 f = timeline().nextMarker(m_playhead);
    if (f < 0)
        return false;
    setPlayhead(f);
    return true;
}

bool ProjectController::goToPreviousMarker()
{
    const qint64 f = timeline().prevMarker(m_playhead);
    if (f < 0)
        return false;
    setPlayhead(f);
    return true;
}

} // namespace bc
