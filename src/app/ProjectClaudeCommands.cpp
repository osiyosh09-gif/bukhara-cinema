// Claude command assistant (phase 17a). Part of ProjectController.
//
// A natural-language command ("kameralarni sinxronlash qil", "multicamni
// kes") is turned into a tool call by the Claude Haiku model (app/ClaudeAssistant)
// and lands here as one of two actions: sync the selected recordings by sound
// into a multicamera sequence (same as Sequence > Create multicamera
// sequence, sync by sound), or cut a multicamera clip between its cameras
// (same as Sequence > Automatic multicamera edit, with the last used, or
// default, settings). Both may need to read the cameras' sound first, which
// happens in the background (ProjectAutoMulticam.cpp); the command picks up
// again from claudeCommandFinished's caller once multicamSoundReady fires.
// There is no dialog and no confirmation: the result (or why it could not
// be done) comes back as claudeCommandFinished, translated like the rest of
// the interface.

#include "app/ProjectController.h"
#include "core/AudioSync.h"

#include <QSettings>

#include <algorithm>
#include <cmath>

namespace bc {

void ProjectController::syncCamerasCommand(const QList<QUuid> &mediaIds)
{
    m_pendingSyncActive = true;
    m_pendingSyncMedia = mediaIds;
    continueSyncCamerasCommand();
}

void ProjectController::continueSyncCamerasCommand()
{
    if (!m_pendingSyncActive)
        return;
    const QList<QUuid> mediaIds = m_pendingSyncMedia;
    if (mediaIds.size() < 2) {
        m_pendingSyncActive = false;
        emit claudeCommandFinished(false, tr("At least two files are needed to sync."));
        return;
    }
    bool known = true;
    for (const QUuid &id : mediaIds) {
        const MediaItem *m = m_project.item(id);
        if (m && m->info.hasAudio() && !m_envelopes.contains(id) && !m_envelopesFailed.contains(id))
            known = false;
    }
    if (!known) {
        if (!m_soundRunning)
            analyzeMediaSound(mediaIds);
        return; // continueSyncCamerasCommand() runs again from multicamSoundReady
    }
    m_pendingSyncActive = false;

    QList<std::vector<float>> envelopes;
    for (const QUuid &id : mediaIds) {
        const auto it = m_envelopes.constFind(id);
        envelopes << (it != m_envelopes.constEnd() ? **it : std::vector<float>());
    }
    const QList<AudioSync::Match> matches = AudioSync::align(envelopes);
    const double fps = m_project.sequence.fps() > 0 ? m_project.sequence.fps() : 25.0;
    QList<qint64> starts;
    bool allFound = true;
    for (int i = 0; i < mediaIds.size(); ++i) {
        const double offset = i < matches.size() ? matches[i].offset : 0.0;
        allFound = allFound && i < matches.size() && matches[i].valid;
        starts << qint64(std::llround(offset * fps));
    }
    const QUuid nestId = createMulticam(mediaIds, starts, -1);
    if (nestId.isNull()) {
        emit claudeCommandFinished(false, tr("The multicamera sequence could not be created (the files did not match)."));
        return;
    }
    emit claudeCommandFinished(true,
        allFound ? tr("The cameras were synced by sound; the multicamera sequence was created.")
                 : tr("The multicamera sequence was created; some files' sound did not match and "
                      "were placed from the start (can be fixed by hand)."));
}

void ProjectController::autoCutCamerasCommand(const QList<QUuid> &selection)
{
    const QList<QUuid> clips = autoMulticamClips(selection);
    if (clips.isEmpty()) {
        m_pendingCutActive = false;
        emit claudeCommandFinished(false,
            tr("No multicamera clip found. First sync the cameras to create a multicamera sequence."));
        return;
    }
    m_pendingCutActive = true;
    m_pendingCutClips = clips;
    continueAutoCutCamerasCommand();
}

void ProjectController::continueAutoCutCamerasCommand()
{
    if (!m_pendingCutActive)
        return;
    const QList<QUuid> clips = m_pendingCutClips;
    if (!multicamSoundKnown(clips)) {
        if (!m_soundRunning)
            analyzeMulticamSound(clips);
        return; // continueAutoCutCamerasCommand() runs again from multicamSoundReady
    }
    m_pendingCutActive = false;

    QSettings s;
    AutoMulticam::Settings settings;
    settings.mode = s.value(QStringLiteral("autoMulticam/mode"), 0).toInt() == 1 ? AutoMulticam::Mode::Rhythm
                                                                                  : AutoMulticam::Mode::Speaker;
    settings.wideAngle = s.value(QStringLiteral("autoMulticam/wide"), -1).toInt();
    settings.minShot = s.value(QStringLiteral("autoMulticam/minShot"), 2.0).toDouble();
    settings.maxShot = s.value(QStringLiteral("autoMulticam/maxShot"), 8.0).toDouble();
    settings.margin = s.value(QStringLiteral("autoMulticam/margin"), 6.0).toDouble();

    const int cuts = applyAutoMulticam(clips, settings);
    if (cuts > 0)
        emit claudeCommandFinished(true, tr("The multicamera clip was cut %1 times between the cameras.").arg(cuts));
    else
        emit claudeCommandFinished(false,
            tr("Not enough difference was found to cut (the cameras hear almost the same sound)."));
}

} // namespace bc
