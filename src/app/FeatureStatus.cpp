#include "app/FeatureStatus.h"

#include <QCoreApplication>

namespace bc::FeatureStatus {

QString stateName(FeatureEntry::State state)
{
    switch (state) {
    case FeatureEntry::State::Ready:   return QCoreApplication::translate("FeatureStatus", "Ready");
    case FeatureEntry::State::Partial: return QCoreApplication::translate("FeatureStatus", "Partial");
    case FeatureEntry::State::Planned: return QCoreApplication::translate("FeatureStatus", "Planned");
    }
    return {};
}

QString phaseTag(int phase)
{
    return QCoreApplication::translate("FeatureStatus", "[phase %1]").arg(phase);
}

QList<FeatureEntry> entries()
{
    using S = FeatureEntry::State;
    return {
        {S::Ready,   0, QCoreApplication::translate("FeatureStatus", "Main window with five resizable zones"), QCoreApplication::translate("FeatureStatus", "Panels can be resized, moved, hidden and reset; the layout is remembered.")},
        {S::Ready,   0, QCoreApplication::translate("FeatureStatus", "Dark grey theme"), {}},
        {S::Ready,   0, QCoreApplication::translate("FeatureStatus", "Interface languages: Uzbek (Cyrillic, Latin) and Russian"), QCoreApplication::translate("FeatureStatus", "Switch in File > Settings; applies immediately.")},
        {S::Ready,   0, QCoreApplication::translate("FeatureStatus", "About window and third-party licenses"), {}},
        {S::Ready,   0, QCoreApplication::translate("FeatureStatus", "Log file for problem reports"), QCoreApplication::translate("FeatureStatus", "Help > Open log folder.")},
        {S::Partial, 0, QCoreApplication::translate("FeatureStatus", "Windows installer"), QCoreApplication::translate("FeatureStatus", "Installer wizard is in Russian or English; Uzbek wizard texts are planned.")},
        {S::Ready,   1, QCoreApplication::translate("FeatureStatus", "New project, open, save, recent projects"), QCoreApplication::translate("FeatureStatus", "Project file .bcproj; warns about unsaved changes.")},
        {S::Ready,   1, QCoreApplication::translate("FeatureStatus", "Media import: files, folders, drag and drop from Explorer"), {}},
        {S::Ready,   1, QCoreApplication::translate("FeatureStatus", "Media bin: folders, search, renaming, metadata, thumbnails"), {}},
        {S::Ready,   1, QCoreApplication::translate("FeatureStatus", "Missing files: detection and relinking"), QCoreApplication::translate("FeatureStatus", "Other missing files in the same folder are relinked automatically.")},
        {S::Ready,   1, QCoreApplication::translate("FeatureStatus", "Sequence settings: size, frame rate, audio"), {}},
        {S::Planned, 5, QCoreApplication::translate("FeatureStatus", "Audio waveforms and proxy files"), {}},
        {S::Ready,   2, QCoreApplication::translate("FeatureStatus", "Multitrack timeline: place, move, split, trim, ripple delete, snapping"), QCoreApplication::translate("FeatureStatus", "Drag media from the bin; clips overwrite what is under them; linked video and audio move together; tracks can be locked and hidden.")},
        {S::Ready,   2, QCoreApplication::translate("FeatureStatus", "Undo/redo and keyboard shortcuts"), QCoreApplication::translate("FeatureStatus", "Ctrl+Z, Ctrl+Shift+Z, Ctrl+K split, Delete, Shift+Delete ripple delete, S snapping. Custom shortcuts are planned for phase 6.")},
        {S::Ready,   2, QCoreApplication::translate("FeatureStatus", "Playback in Program monitor"), QCoreApplication::translate("FeatureStatus", "Real-time playback with sound (Space), frame steps, start and end. On a slow computer frames are skipped so sound stays in sync.")},
        {S::Planned, 6, QCoreApplication::translate("FeatureStatus", "Source monitor playback, J/K/L shuttle, in and out points"), {}},
        {S::Ready,   3, QCoreApplication::translate("FeatureStatus", "Text titles and audio volume, fades, mute and solo"), QCoreApplication::translate("FeatureStatus", "Graphics > New text (Ctrl+T); text, size, color, place and a dark box in the Clip panel. Clip volume in dB with fade in and out; M and S buttons on audio tracks. Font choice and animated titles come in later phases.")},
        {S::Planned, 4, QCoreApplication::translate("FeatureStatus", "MP4 export (H.264 + AAC) with progress and cancel"), {}},
        {S::Planned, 5, QCoreApplication::translate("FeatureStatus", "Autosave, crash recovery, versions, backups"), {}},
        {S::Planned, 6, QCoreApplication::translate("FeatureStatus", "Professional editing: insert, overwrite, roll, slip, slide, markers, nesting"), {}},
        {S::Planned, 7, QCoreApplication::translate("FeatureStatus", "Transform, keyframes, speed changes"), {}},
        {S::Planned, 8, QCoreApplication::translate("FeatureStatus", "Transitions, effects, masks, chroma key"), {}},
        {S::Planned, 9, QCoreApplication::translate("FeatureStatus", "Color correction, LUT, scopes"), {}},
        {S::Planned, 10, QCoreApplication::translate("FeatureStatus", "Audio mixer, EQ, compressor, noise reduction, loudness"), {}},
        {S::Planned, 11, QCoreApplication::translate("FeatureStatus", "Subtitles and SRT import/export"), {}},
        {S::Planned, 12, QCoreApplication::translate("FeatureStatus", "Multicamera editing"), {}},
        {S::Planned, 13, QCoreApplication::translate("FeatureStatus", "Export queue, render preview, GPU acceleration"), {}},
        {S::Planned, 14, QCoreApplication::translate("FeatureStatus", "AI assistants: transcription, text-based editing, scene detection"), {}},
        {S::Planned, 15, QCoreApplication::translate("FeatureStatus", "Plugins and project interchange"), {}},
    };
}

} // namespace bc::FeatureStatus
