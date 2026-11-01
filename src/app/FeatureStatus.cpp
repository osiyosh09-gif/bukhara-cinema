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
        {S::Ready,   5, QCoreApplication::translate("FeatureStatus", "Audio waveforms on the timeline"), QCoreApplication::translate("FeatureStatus", "Computed in the background once per file and kept in the cache; follow the clip volume.")},
        {S::Ready,   5, QCoreApplication::translate("FeatureStatus", "Proxy files for smooth playback of big videos"), QCoreApplication::translate("FeatureStatus", "Videos larger than Full HD get a 540p proxy in the background (File > Settings); any video can get one from the media bin menu. Sequence > Use proxies for playback switches them; export always uses the originals.")},
        {S::Ready,   5, QCoreApplication::translate("FeatureStatus", "Media cache and collecting a project into one folder"), QCoreApplication::translate("FeatureStatus", "File > Media cache shows and deletes thumbnails, waveforms and proxies. File > Collect project copies the project and its media into a new folder.")},
        {S::Ready,   2, QCoreApplication::translate("FeatureStatus", "Multitrack timeline: place, move, split, trim, ripple delete, snapping"), QCoreApplication::translate("FeatureStatus", "Drag media from the bin; clips overwrite what is under them; linked video and audio move together; tracks can be locked and hidden.")},
        {S::Ready,   2, QCoreApplication::translate("FeatureStatus", "Undo/redo and keyboard shortcuts"), QCoreApplication::translate("FeatureStatus", "Ctrl+Z, Ctrl+Shift+Z, Ctrl+K split, Delete, Shift+Delete ripple delete, S snapping. Edit > Keyboard shortcuts changes any key.")},
        {S::Ready,   2, QCoreApplication::translate("FeatureStatus", "Playback in Program monitor"), QCoreApplication::translate("FeatureStatus", "Real-time playback with sound (Space), frame steps, start and end. On a slow computer frames are skipped so sound stays in sync.")},
        {S::Ready,   6, QCoreApplication::translate("FeatureStatus", "Source monitor playback, J/K/L shuttle, in and out points"), QCoreApplication::translate("FeatureStatus", "Double-click a clip in the media bin. I and O mark the part to use (kept while the program runs). J, K and L play backwards, stop and play forwards; pressing J or L again plays faster (up to 8x). Backwards playback has no sound.")},
        {S::Ready,   3, QCoreApplication::translate("FeatureStatus", "Text titles and audio volume, fades, mute and solo"), QCoreApplication::translate("FeatureStatus", "Graphics > New text (Ctrl+T); text, size, color, place and a dark box in the Clip panel. Clip volume in dB with fade in and out; M and S buttons on audio tracks. Font choice and animated titles come in later phases.")},
        {S::Ready,   4, QCoreApplication::translate("FeatureStatus", "MP4 export (H.264 + AAC) with progress and cancel"), QCoreApplication::translate("FeatureStatus", "File > Export (Ctrl+M). H.264 through Intel Quick Sync, Windows Media Foundation, NVIDIA or AMD; MPEG-4 Part 2 when none of them works. Size and frame rate follow the sequence settings; other sizes and an export queue come later.")},
        {S::Ready,   5, QCoreApplication::translate("FeatureStatus", "Autosave, crash recovery, previous versions"), QCoreApplication::translate("FeatureStatus", "A copy is written every 2 minutes while there are unsaved changes (File > Settings). After a crash the next start offers to recover it. Each save keeps the old file in the Backups folder; File > Previous versions opens one.")},
        {S::Ready,   6, QCoreApplication::translate("FeatureStatus", "Insert and overwrite edits, timeline markers"), QCoreApplication::translate("FeatureStatus", "Comma inserts the source part at the playhead and moves later clips right; period puts it over what is there. M adds a marker, Shift+M and Ctrl+Shift+M jump between markers; markers are saved in the project.")},
        {S::Ready,   6, QCoreApplication::translate("FeatureStatus", "Razor, rolling edit, slip and slide tools; copy, cut and paste; link and unlink"), QCoreApplication::translate("FeatureStatus", "Tools above the timeline or keys V, C, N, Y, U. Ctrl+C, Ctrl+X, Ctrl+V paste at the playhead (Ctrl+Shift+V pushes later clips right). Ctrl+L links or unlinks the selected video and sound.")},
        {S::Ready,   6, QCoreApplication::translate("FeatureStatus", "Nested sequences and custom keyboard shortcuts"), QCoreApplication::translate("FeatureStatus", "Select clips and choose Clip > Nest (also in the clip right-click menu): they become one clip. Double-click it to edit inside; the list above the timeline goes back to the main sequence. Edit > Keyboard shortcuts changes the key of any command; keys are kept in the settings.")},
        {S::Ready,   7, QCoreApplication::translate("FeatureStatus", "Position, scale, rotation and opacity of clips, with keyframes"), QCoreApplication::translate("FeatureStatus", "Select a video clip or title: the Motion group of the Clip panel. Animate turns keyframes on; then each change sets a keyframe at the playhead and the values change smoothly between keyframes (diamonds on the clip). Reset puts the picture back. Values are typed in the panel; dragging the picture in the monitor comes later.")},
        {S::Ready,   7, QCoreApplication::translate("FeatureStatus", "Speed changes"), QCoreApplication::translate("FeatureStatus", "Clip > Speed / duration (Ctrl+R): 10% to 1000%, or a new duration. Linked sound changes with the picture (its pitch too). Shift the clips after it moves later clips. Reverse playback, speed ramps and pitch-kept sound come later.")},
        {S::Ready,   8, QCoreApplication::translate("FeatureStatus", "Transitions: cross dissolve, dip to black or white, wipe; sound crossfade"), QCoreApplication::translate("FeatureStatus", "Ctrl+D adds a 1-second dissolve at the edit nearest the playhead; the Clip panel sets the kind and length of the start and end. Between touching clips both are mixed (using extra frames of the files when they have them); at a free end the clip fades.")},
        {S::Ready,   8, QCoreApplication::translate("FeatureStatus", "Clip effects: brightness and contrast, saturation, blur, sharpen, crop, mirror"), QCoreApplication::translate("FeatureStatus", "Select a video clip and open the Effects tab: add effects, untick to compare, reorder with the arrows, change values. Effects work from top to bottom, before the motion settings. Values do not change over time yet.")},
        {S::Ready,   8, QCoreApplication::translate("FeatureStatus", "Chroma key and masks"), QCoreApplication::translate("FeatureStatus", "In the Effects panel: Chroma key makes one colour (green or blue screen) transparent so the track below shows through; Mask keeps a rectangle or ellipse (or hides it with Invert) with a soft edge. Picking the colour with a dropper, drawn (bezier) masks and moving masks come later.")},
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
