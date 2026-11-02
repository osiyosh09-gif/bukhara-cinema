// Loudness of the whole mix (phase 13, part 3). Part of ProjectController.
//
// The open sequence's sound is rendered by its own engine graph (every
// track with its effects and fader, and the master) on a worker thread and
// measured as in EBU R128. The master volume can then be set so the mix
// reaches a target loudness.

#include "app/ProjectController.h"
#include "engine/TimelinePlayer.h"

#include <QPointer>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace bc {

bool ProjectController::measureMixLoudness(QString *error)
{
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    if (m_mixBusy)
        return fail(tr("The mix is being measured already."));
    if (!canPlay())
        return fail(tr("The video engine is not available."));
    const qint64 total = timeline().duration();
    if (total <= 0)
        return fail(tr("The sequence is empty."));

    // Only the sound is needed: the picture tracks and subtitles are left out
    // (nested clips on audio tracks still bring their sound).
    Project view = m_project;
    view.timeline = timeline();
    for (TimelineTrack &t : view.timeline.tracks)
        if (t.kind == Timeline::Kind::Video)
            t.clips.clear();
    view.timeline.subtitles.clear();
    auto player = std::make_shared<TimelinePlayer>();
    player->setProject(view);
    const int rate = m_project.sequence.sampleRate > 0 ? m_project.sequence.sampleRate : 48000;
    const double fps = std::max(1.0, m_project.sequence.fps());
    const quint64 rev = m_graphRev;

    m_mixBusy = true;
    m_mixCancel = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = m_mixCancel;
    QPointer<ProjectController> self(this);
    emit mixLoudnessProgress(0, total);
    (void)QtConcurrent::run(&m_mixPool, [self, player, total, rate, fps, rev, cancel]() mutable {
        Loudness::Meter meter(2, rate);
        qint64 done = 0;
        const bool ok = player->renderAudio(0, total, [&](const float *samples, int count) {
            if (cancel->load())
                return false;
            meter.add(samples, count);
            if (++done % 50 == 0)
                QMetaObject::invokeMethod(QCoreApplication::instance(), [self, done, total] {
                    if (self)
                        emit self->mixLoudnessProgress(done, total);
                }, Qt::QueuedConnection);
            return true;
        });
        const Loudness::Result result = Loudness::measure(meter.profile(), 0, double(total) / fps);
        const bool cancelled = cancel->load();
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  [self, ok, cancelled, result, total, fps, rev, player = std::move(player)] {
            if (!self)
                return;
            self->m_mixBusy = false;
            if (ok) {
                self->m_mixResult = result;
                self->m_mixSeconds = double(total) / fps;
                self->m_mixRev = rev;
                emit self->mixLoudnessProgress(total, total);
            }
            emit self->mixLoudnessMeasured(ok, ok || cancelled ? QString() : tr("The sound could not be rendered."));
        }, Qt::QueuedConnection);
    });
    return true;
}

void ProjectController::cancelMixLoudness()
{
    if (m_mixCancel)
        m_mixCancel->store(true);
}

Loudness::Result ProjectController::mixLoudness(bool *current) const
{
    if (current)
        *current = m_mixRev == m_graphRev;
    return m_mixResult;
}

double ProjectController::mixGain(double targetLufs, double ceilingDb) const
{
    const double master = timeline().masterGainDb;
    double gain = Loudness::gainToTarget(m_mixResult, targetLufs, ceilingDb);
    // The master fader has a range; round to its 0.1 dB steps.
    gain = std::clamp(master + gain, TimelineTrack::kMinGainDb, TimelineTrack::kMaxGainDb) - master;
    return std::round(gain * 10) / 10;
}

bool ProjectController::normalizeMix(double targetLufs, double ceilingDb)
{
    bool current = false;
    mixLoudness(&current);
    if (!current || !m_mixResult.valid)
        return false;
    const double gain = mixGain(targetLufs, ceilingDb);
    if (gain == 0)
        return false;
    const double master = std::round((timeline().masterGainDb + gain) * 10) / 10;
    const bool changed = editTimeline(tr("Normalize mix loudness"), [&](Timeline &t) {
        if (t.masterGainDb == master)
            return false;
        t.masterGainDb = master;
        return true;
    });
    if (changed) {
        // The master fader scales the whole mix: the new loudness is known.
        m_mixResult.lufs += gain;
        m_mixResult.peakDb += gain;
        m_mixRev = m_graphRev;
    }
    return changed;
}

} // namespace bc
