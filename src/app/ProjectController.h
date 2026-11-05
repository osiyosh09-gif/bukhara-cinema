#pragma once

#include "core/AutoMulticam.h"
#include "core/Interchange.h"
#include "core/Loudness.h"
#include "core/Project.h"
#include "core/SceneDetect.h"
#include "core/Speech.h"
#include "engine/EngineServices.h"
#include "engine/Exporter.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

class QUndoStack;

namespace bc { class TimelinePlayer; }

namespace bc {

// Owns the open project and every change to it. Widgets never modify the
// Project directly; they call the controller, which marks the project as
// modified and notifies the views.
class ProjectController : public QObject
{
    Q_OBJECT
public:
    explicit ProjectController(QObject *parent = nullptr);
    ~ProjectController() override;

    const Project &project() const { return m_project; }
    QString filePath() const { return m_filePath; }
    bool isModified() const { return m_modified; }
    QString displayName() const;

    // --- file operations
    void newProject();
    bool open(const QString &path, QString *error);
    bool save(QString *error);                       // requires filePath()
    bool saveAs(const QString &path, QString *error);
    // Opens a copy of a project (an autosave or an earlier version) as if it
    // were the file at originalPath (empty = untitled). The project counts as
    // modified, so the next save writes it to originalPath.
    bool openCopy(const QString &dataPath, const QString &originalPath, QString *error);
    // Grows with every change; autosave writes only when it moved.
    quint64 changeSerial() const { return m_changeSerial; }

    static QStringList recentProjects();

    // --- media bin
    // Imports supported files (folders are scanned recursively). Files
    // already in the project are skipped. Returns the number added.
    int importFiles(const QStringList &paths, const QUuid &folder = {});
    QUuid addFolder(const QString &name, const QUuid &parent = {});
    void renameFolder(const QUuid &id, const QString &name);
    void renameItem(const QUuid &id, const QString &name);
    void removeFolder(const QUuid &id);
    void removeItems(const QList<QUuid> &ids);
    void moveItems(const QList<QUuid> &ids, const QUuid &folder);
    void moveFolder(const QUuid &id, const QUuid &newParent);

    bool isOnline(const QUuid &id) const;
    int offlineCount() const;
    bool isProbing(const QUuid &id) const { return m_pendingProbe.contains(id); }

    // Points an item at a different file. Returns false if the new file
    // cannot be read. Also relinks other offline items whose file with the
    // same name exists in the new file's folder; their count goes to *alsoRelinked.
    bool relink(const QUuid &id, const QString &newPath, int *alsoRelinked, QString *error);
    // Relinks every offline item whose file name exists in `folder`
    // (searched recursively). Returns the number relinked.
    int relinkFromFolder(const QString &folder);

    // Checks that newPath plausibly is the same media as the item had
    // (kind and duration within 1 s). Empty string = fine, else a reason.
    QString relinkMismatch(const QUuid &id, const QString &newPath) const;

    QImage thumbnail(const QUuid &id) const { return m_thumbs.value(id); }
    // Audio peaks (see Waveform), empty until computed in the background.
    QByteArray waveform(const QUuid &id) const { return m_waves.value(id); }

    void setSequence(const SequenceSettings &s);

    // --- scene changes (phase 14a, ProjectScenes.cpp)
    // The video clips (of video files, not photos) among `clipIds` and the
    // video linked to audio clips among them.
    QList<QUuid> sceneClips(const QList<QUuid> &clipIds) const;
    // Analyses the media of these clips one after another in the background
    // (results are cached); sceneProgress and scenesAnalyzed report.
    void analyzeScenes(const QList<QUuid> &clipIds);
    void cancelScenes();
    bool isAnalyzingScenes() const { return !m_scenesQueue.isEmpty() || !m_scenesRunning.isNull(); }
    // True when every media of these clips was analysed (or failed).
    bool scenesKnown(const QList<QUuid> &clipIds) const;
    // Timeline frames inside the clips where a new shot begins.
    QList<qint64> sceneCuts(const QUuid &clipId, double sensitivity) const;
    int sceneCutCount(const QList<QUuid> &clipIds, double sensitivity) const;
    // Splits the clips (with their linked sound) at every cut, or puts a
    // timeline marker there. One undo step; returns the number of cuts.
    int splitAtScenes(const QList<QUuid> &clipIds, double sensitivity);
    int markScenes(const QList<QUuid> &clipIds, double sensitivity);

    // --- loudness (phase 10d, ProjectLoudness.cpp)
    // The audio clips among `clipIds` and the audio linked to the video clips among them.
    QList<QUuid> loudnessClips(const QList<QUuid> &clipIds) const;
    // Measures the media of these clips in the background (results are cached);
    // loudnessAnalyzed is emitted for each media item when it is done.
    void analyzeLoudness(const QList<QUuid> &clipIds);
    bool loudnessKnown(const QUuid &clipId) const;
    // Loudness of the clip's source range, before its volume and effects;
    // valid = false while not measured, or for silence.
    Loudness::Result clipLoudness(const QUuid &clipId) const;
    // All clips measured as one recording.
    Loudness::Result clipsLoudness(const QList<QUuid> &clipIds) const;
    // The clip volume (dB) that brings each clip to `targetLufs` with no sample
    // above `ceilingDb`; `together` gives every clip the same change (keeps their
    // balance). Clips not measured or silent are left out.
    QHash<QUuid, double> loudnessGains(const QList<QUuid> &clipIds, double targetLufs, double ceilingDb,
                                       bool together) const;
    // Sets those volumes in one undo step; returns the number of clips changed.
    int normalizeLoudness(const QList<QUuid> &clipIds, double targetLufs, double ceilingDb, bool together);

    // --- timeline (every edit is undoable)
    // The timeline being edited: the main sequence or an open nested one.
    const Timeline &timeline() const;
    QUndoStack *undoStack() const { return m_undo; }

    // Places media items one after another starting at `start` on track
    // `track` (index into timeline().tracks). Video and audio parts go to the
    // tracks with the same number. Items not read yet are skipped.
    // Returns the new clip ids.
    QList<QUuid> placeMedia(const QList<QUuid> &mediaIds, int track, qint64 start);
    // What placeMedia would do, without changing the project (drop preview).
    bool previewPlacement(Timeline &t, const QList<QUuid> &mediaIds, int track, qint64 start,
                          QList<QUuid> *created = nullptr) const;
    bool moveClips(const QList<QUuid> &clipIds, qint64 deltaFrames, Timeline::Kind kind, int deltaTracks);
    bool trimClip(const QUuid &clipId, bool head, qint64 frame);
    // Phase 6b tools (see Timeline::roll / slip / slide); undoable.
    bool rollClip(const QUuid &clipId, bool head, qint64 frame);
    bool slipClip(const QUuid &clipId, qint64 deltaFrames);
    bool slideClip(const QUuid &clipId, qint64 deltaFrames);
    // Links the clips when none of them is linked, otherwise unlinks them.
    bool toggleLink(const QList<QUuid> &clipIds);
    // Clipboard of clips (kept while the program runs). Paste puts them at
    // the playhead on the same tracks; with insert, later clips move right.
    // The playhead moves to the end of the pasted clips.
    bool copyClips(const QList<QUuid> &clipIds);
    bool cutClips(const QList<QUuid> &clipIds);
    QList<QUuid> pasteClips(bool insert = false);
    bool canPaste() const { return !m_clipboard.isEmpty(); }
    // Splits the given clips (or every clip on unlocked tracks when empty) at `frame`.
    bool splitClips(qint64 frame, const QList<QUuid> &clipIds);
    bool removeClips(const QList<QUuid> &clipIds, bool ripple);
    int addTrack(Timeline::Kind kind);
    void setTrackFlag(int track, const QString &flag, bool on); // "locked", "muted", "hidden", "solo"
    // Audio mixer (phase 10a), on the open sequence; a fader drag is one undo step.
    bool setTrackGain(int track, double db);   // audio tracks, TimelineTrack::kMinGainDb .. kMaxGainDb
    bool setTrackPan(int track, double pan);   // audio tracks, -1 .. 1
    bool setMasterGain(double db);
    // Last peaks of an audio track (or the master: null id) of the main sequence
    // while the Program monitor plays.
    EngineServices::MeterLevel meterLevel(const QUuid &trackId) const;
    // Adds a 5-second title at `start` on the highest video track that is free
    // there (a new track when none is). Returns the new clip id.
    QUuid addTitle(qint64 start);
    // Changes a title clip. Quick successive changes of one clip (typing)
    // form a single undo step.
    bool setClipTitle(const QUuid &clipId, const TitleData &title);
    // Volume (dB) and fades (frames) of an audio clip; also one undo step
    // while a slider is dragged.
    bool setClipAudio(const QUuid &clipId, double gainDb, qint64 fadeIn, qint64 fadeOut);
    // --- motion of video clips (phase 7a). `frame` is a timeline frame
    // (usually the playhead); it picks the keyframe of an animated clip.
    ClipTransform clipTransformAt(const QUuid &clipId, qint64 frame) const;
    // Sets the value (a key at `frame` when the clip is animated). Quick
    // successive changes of one clip form one undo step.
    bool setClipTransform(const QUuid &clipId, const ClipTransform &value, qint64 frame);
    bool setClipAnimated(const QUuid &clipId, bool on, qint64 frame);
    bool hasClipKey(const QUuid &clipId, qint64 frame) const;
    // Adds a key at `frame` (an animated clip) or removes the one there.
    bool toggleClipKey(const QUuid &clipId, qint64 frame);
    bool resetClipMotion(const QUuid &clipId);
    // Timeline frame of the next (or previous) key of the clip, or -1.
    qint64 clipKeyNear(const QUuid &clipId, qint64 frame, bool next) const;
    // --- speed (phase 7b): video and audio file clips only.
    bool canChangeSpeed(const QUuid &clipId) const;
    // Sets the speed of the clip and its linked clips (0.1 .. 10). With
    // `length` <= 0 the clip keeps the same part of its source (its length
    // changes); otherwise it gets that length. Without ripple a longer clip
    // stops at the next clip; with ripple later clips on its tracks move.
    bool setClipSpeed(const QUuid &clipId, double speed, qint64 length = 0, bool ripple = false);
    // --- transitions (phase 8a)
    ClipTransition clipTransition(const QUuid &clipId, bool head) const;
    // Quick successive changes of one end form one undo step.
    bool setClipTransition(const QUuid &clipId, bool head, const ClipTransition &t);
    // Ctrl+D: a 1-second dissolve (crossfade for sound) at the end of each
    // clip (and its linked clips) nearest to `frame`, where there is none.
    // One undo step; returns how many were added.
    int addDefaultTransitions(const QList<QUuid> &clipIds, qint64 frame);
    // --- picture effects of video clips (phase 8b). `index` is the place in
    // the clip's list (applied top to bottom).
    ClipEffects clipEffects(const QUuid &clipId) const;
    // Adds an effect with default values at the end; returns its index or -1.
    int addClipEffect(const QUuid &clipId, ClipEffect::Kind kind);
    // Adds a VST 3 plug-in (phase 15c) to an audio clip with the plug-in's
    // default values; returns its index, or -1 (also when it does not start).
    int addClipPlugin(const QUuid &clipId, const QString &path, const QString &uid, const QString &name,
                      QString *error = nullptr);
    bool removeClipEffect(const QUuid &clipId, int index);
    // Moves the effect one place up (delta -1) or down (+1).
    bool moveClipEffect(const QUuid &clipId, int index, int delta);
    bool setClipEffectEnabled(const QUuid &clipId, int index, bool on);
    // Quick successive changes of one value form one undo step.
    bool setClipEffectValue(const QUuid &clipId, int index, const QString &key, double value);
    // --- colour correction of video clips (phase 9a)
    ClipColor clipColor(const QUuid &clipId) const;
    // Quick successive changes of one value form one undo step.
    bool setClipColorValue(const QUuid &clipId, const QString &key, double value);
    // Moves the point of a colour wheel ("lift", "gamma", "gain"); a drag is one undo step.
    bool setClipColorWheel(const QUuid &clipId, const QString &wheel, double x, double y);
    bool resetClipColorWheels(const QUuid &clipId); // all three wheels and their masters
    bool resetClipColor(const QUuid &clipId);
    // A .cube file for the clip's creative look (empty removes it).
    bool setClipLut(const QUuid &clipId, const QString &path);
    // --- nested sequences (phase 6c)
    // Moves the clips (and their linked clips) into a new nested sequence and
    // puts one clip of it in their place (picture and sound, linked).
    // Returns the id of the nested sequence (null when nothing was nested).
    QUuid nestClips(const QList<QUuid> &clipIds);
    // Opens a nested sequence on the timeline (null id = the main sequence).
    // The Program monitor plays the open one; export is always the main one.
    bool openSequence(const QUuid &nestId);
    QUuid openNest() const { return m_openNest; }
    QString sequenceName(const QUuid &nestId) const; // null = main sequence
    bool renameSequence(const QUuid &nestId, const QString &name);
    // --- multicamera (phase 12a, ProjectMulticam.cpp)
    // Makes a multicamera sequence of the media (angle n = video track n and
    // audio track n, angle n starting at starts[n] frames; the smallest start
    // becomes 0) and places one clip of it at the playhead of the open
    // sequence (V1 + A1, overwriting). `audioAngle` is the sound the clip
    // uses. Images are not allowed. One undo step; returns the sequence id.
    QUuid createMulticam(const QList<QUuid> &mediaIds, const QList<qint64> &starts, int audioAngle = 0);
    bool isMulticam(const QUuid &nestId) const;
    // Names of the angles of that kind (the media on each track, or "Angle n").
    QStringList multicamAngles(const QUuid &nestId, Timeline::Kind kind) const;
    // Shows (video clip) or plays (audio clip) another angle of its multicamera sequence.
    bool setClipAngle(const QUuid &clipId, int angle);
    // --- multicamera monitor and live switching (phase 12b)
    // The multicamera clip under `frame` in the open sequence (picture: the
    // lowest video track that has one; sound: the audio clip linked to that
    // one, else the lowest audio track that has one). Null when none.
    QUuid multicamClipAt(qint64 frame, Timeline::Kind kind = Timeline::Kind::Video) const;
    // Shows (or plays) `angle` from the playhead on: the clip is cut at the
    // playhead (with its linked clip) and the part after it gets the angle.
    // One undo step; works while playing. False when nothing changes.
    bool switchMulticamAngle(int angle, Timeline::Kind kind = Timeline::Kind::Video);
    // Renders every angle of the multicamera clip at the playhead in a grid
    // (in the background, the newest request wins); emits multicamGridReady.
    void requestMulticamGrid();
    // --- subtitles from speech (phase 14b, ProjectSpeech.cpp)
    // The sound clips it listens to: the selected ones and those linked to
    // selected pictures; with nothing selected, the clips of the first audio
    // track that has any. Media clips and nested / multicamera clips.
    QList<QUuid> speechClips(const QList<QUuid> &selection) const;
    // Whether those clips were heard with `modelDir` (results are cached);
    // transcribeClips listens to the rest in the background and reports with
    // speechProgress / speechReady.
    bool speechKnown(const QList<QUuid> &clipIds, const QString &modelDir) const;
    void transcribeClips(const QList<QUuid> &clipIds, const QString &modelDir);
    void cancelSpeech();
    bool isTranscribing() const { return m_speechRunning; }
    // The subtitle lines (sequence frames) of what was heard in the clips.
    QList<Subtitle> speechSubtitles(const QList<QUuid> &clipIds, const QString &modelDir,
                                    const Speech::CueSettings &settings, bool cyrillic) const;
    // Puts them on the open sequence (one undo step); `replace` removes the
    // subtitles there were where the clips are. Returns the number added.
    int addSpeechSubtitles(const QList<QUuid> &clipIds, const QString &modelDir, const Speech::CueSettings &settings,
                           bool cyrillic, bool replace);
    // --- automatic multicamera edit (phase 16a, ProjectAutoMulticam.cpp)
    // The multicamera picture clips of the open sequence it works on: the
    // selected ones, else the one at the playhead.
    QList<QUuid> autoMulticamClips(const QList<QUuid> &selection) const;
    // Whether the sound of every camera of those clips has been read (results
    // are cached); analyzeMulticamSound reads the rest in the background and
    // reports with multicamSoundProgress / multicamSoundReady.
    bool multicamSoundKnown(const QList<QUuid> &clipIds) const;
    void analyzeMulticamSound(const QList<QUuid> &clipIds);
    bool isAnalyzingMulticamSound() const { return m_soundRunning; }
    QStringList multicamClipAngles(const QUuid &clipId) const; // camera names of a clip's sequence
    // Which camera is shown when, in sequence frames (needs the sound read).
    AutoMulticam::Plan autoMulticamPlan(const QUuid &clipId, const AutoMulticam::Settings &settings) const;
    // Cuts the clips at those frames (with their linked sound, which keeps its
    // camera) and sets the cameras: one undo step. Returns the cuts made.
    int applyAutoMulticam(const QList<QUuid> &clipIds, const AutoMulticam::Settings &settings);

    // --- loudness of the whole mix (phase 13c, ProjectMixLoudness.cpp)
    // Measures the mixed sound of the open sequence as the export hears it
    // (every track, effects, faders and master) in the background;
    // mixLoudnessProgress and mixLoudnessMeasured follow.
    bool measureMixLoudness(QString *error = nullptr);
    void cancelMixLoudness();
    bool isMeasuringMix() const { return m_mixBusy; }
    // The last measurement; `current` is false once the sequence has changed since.
    Loudness::Result mixLoudness(bool *current = nullptr) const;
    double mixSeconds() const { return m_mixSeconds; }
    // Master volume change (dB) that brings the measured mix to the target.
    double mixGain(double targetLufs, double ceilingDb) const;
    // Changes the master volume by mixGain (one undo step); false when there
    // is no current measurement or nothing to change.
    bool normalizeMix(double targetLufs, double ceilingDb);

    // --- render preview (phase 13b, ProjectPreview.cpp)
    // Parts of the open sequence with a picture: heavy ones (effects, colour,
    // motion, speed, transitions, nested, several layers) are worth rendering;
    // `rendered` says a preview file is ready (playback then uses it).
    struct PreviewPart
    {
        qint64 start = 0;
        qint64 length = 0;
        bool heavy = false;
        bool rendered = false;
        qint64 end() const { return start + length; }
    };
    QList<PreviewPart> previewParts() const;
    // Renders the heavy, not yet rendered parts touching from..to (exclusive)
    // in the background; previewProgress and previewFinished follow. False
    // (with a reason) when there is nothing to render or it cannot start.
    bool renderPreview(qint64 from, qint64 to, QString *error = nullptr);
    void cancelPreviewRender();
    bool isRenderingPreview() const { return m_previewBusy; }
    // Deletes every preview file (all projects); returns how many.
    int deletePreviews();

    // Length of a media item in sequence frames, -1 = unlimited (images).
    qint64 mediaFrames(const QUuid &mediaId) const;

    // Playhead (not saved in the project).
    qint64 playhead() const { return m_playhead; }
    void setPlayhead(qint64 frame);

    // Real-time playback of the timeline with sound (MLT engine).
    bool canPlay() const;
    QString engineError() const { return m_engineError; }
    bool isPlaying() const;
    bool play();
    void pause();
    void togglePlay();

    // MP4 export of the whole sequence (same picture and sound as playback).
    bool startExport(const ExportSettings &settings, QString *error);
    // The same for a copy of a project (the export queue keeps one per job).
    bool startExport(const Project &project, const ExportSettings &settings, QString *error);
    void cancelExport();
    bool isExporting() const;
    // Why the last export used another video codec (empty when it did not).
    QString exportNote() const;

    // --- proxies (light copies of big videos for smooth playback)
    // Global settings (QSettings "proxy/use", "proxy/auto"), both on by default.
    static bool proxiesEnabledSetting();
    static bool autoProxySetting();
    bool useProxies() const { return m_useProxies; }
    void setUseProxies(bool on);
    bool hasProxy(const QUuid &id) const { return m_proxies.contains(id); }
    bool isMakingProxy(const QUuid &id) const { return m_proxyQueue.contains(id) || m_proxyRunning == id; }
    int proxyJobsLeft() const { return int(m_proxyQueue.size()) + (m_proxyRunning.isNull() ? 0 : 1); }
    // Queues proxies for the given video items (skips ones that have one).
    // Returns the number queued.
    int createProxies(const QList<QUuid> &ids);
    void cancelProxies();
    void deleteProxies(const QList<QUuid> &ids);
    // Forgets proxies whose files are gone (after the cache was cleared).
    void refreshProxies();
    void waitForProxies(); // tests

    // --- Source monitor: one media item, watched before it goes to the
    // timeline. Positions are in sequence frames from the start of the media.
    enum class Monitor { Source, Program };
    QUuid sourceMedia() const { return m_sourceId; }
    // Opens a probed, online item in the Source monitor (null id closes it).
    bool setSourceMedia(const QUuid &id);
    qint64 sourceDuration() const;   // frames; images count as 5 seconds
    qint64 sourcePosition() const { return m_sourcePos; }
    void setSourcePosition(qint64 frame);
    bool isSourcePlaying() const;
    void toggleSourcePlay();
    void sourcePause();
    // In and Out points of the source (Out is the last used frame). They are
    // kept per media item while the program runs.
    void setSourceIn(qint64 frame);
    void setSourceOut(qint64 frame);
    void clearSourceInOut();
    bool hasSourceIn() const { return m_inPoints.contains(m_sourceId); }
    bool hasSourceOut() const { return m_outPoints.contains(m_sourceId); }
    // Part of a media item that an edit uses: from its In point (or 0) to its
    // Out point (or the end). False when the item cannot be placed.
    bool sourceRange(const QUuid &mediaId, qint64 *in, qint64 *length) const;
    // Insert (pushes later clips right) and Overwrite edits of the source
    // range at the playhead, on the first unlocked video and audio tracks.
    // The playhead moves to the end of the new clip.
    bool insertEdit();
    bool overwriteEdit();

    // J / K / L: play backwards, stop, play forwards; pressing J or L again
    // doubles the speed (up to 8x).
    void shuttle(Monitor monitor, int direction); // -1 J, 0 K, +1 L
    double speed(Monitor monitor) const;

    // --- subtitles (phase 11, ProjectSubtitles.cpp; undoable, on the open sequence)
    // Adds a subtitle at `frame`: 3 s long, shorter when the next one starts sooner.
    QUuid addSubtitle(qint64 frame, const QString &text = {});
    bool setSubtitleText(const QUuid &id, const QString &text); // typing merges into one step
    // merge: typing in the panel joins one undo step; a timeline drag is its own step.
    bool setSubtitleTiming(const QUuid &id, qint64 start, qint64 length, bool merge = true);
    int removeSubtitles(const QList<QUuid> &ids);
    bool setSubtitleStyle(const SubtitleStyle &style); // dragging merges into one step
    // Reads an .srt file and places its subtitles `offset` frames later (one
    // undo step); `replace` removes the subtitles there were. Returns the count.
    int importSrt(const QString &path, qint64 offset, bool replace, QString *error, int *skipped = nullptr);
    bool exportSrt(const QString &path, QString *error) const;

    // --- export for other editors (phase 15a, ProjectInterchange.cpp); always the main sequence
    // What would be written, and what the format cannot carry.
    Interchange::Result interchangeResult(Interchange::Format format) const;
    bool exportInterchange(Interchange::Format format, const QString &path, QString *error,
                           Interchange::Result *result = nullptr) const;

    // --- import from other editors (phase 15b, ProjectInterchange.cpp)
    // Reads the file without changing the project; false with a message when
    // it is not a timeline or holds no clips.
    bool readInterchange(const QString &path, Interchange::ReadResult *result, QString *error) const;
    // How many distinct media files of the result exist (in the bin or on the disk).
    void interchangeMediaCount(const Interchange::ReadResult &r, const QString &baseDir, int *found, int *missing) const;
    // Adds the media to the bin (new files in a folder named after the file;
    // missing ones as offline items to relink later), then in one undo step
    // either a nested sequence placed at the playhead of the open sequence
    // (`nested`) or the clips themselves at the playhead. Returns the number
    // of clips, 0 on failure.
    int importInterchange(const QString &path, bool nested, QString *error);

    // --- markers (undoable)
    QUuid addMarker(qint64 frame);
    bool removeMarker(const QUuid &id);
    bool renameMarker(const QUuid &id, const QString &name);
    bool goToNextMarker();
    bool goToPreviousMarker();

    // Blocks until background probing/thumbnails are done (tests, closing).
    void waitForBackgroundWork();

signals:
    void projectReset();                  // whole project replaced / reloaded
    void binChanged();                    // folders / items added, removed, moved, renamed
    void itemUpdated(const QUuid &id);    // info, thumbnail or online state changed
    void modifiedChanged(bool modified);
    void fileNameChanged(const QString &path);
    void sequenceChanged();
    void timelineChanged();
    void playheadChanged(qint64 frame);
    // Frame of the top visible video clip at the playhead; null image = nothing there.
    void programFrameReady(const QImage &image);
    void playingChanged(bool playing);
    void exportProgress(qint64 frame, qint64 total, qint64 remainingMs);
    void exportFinished(bool ok, const QString &error);
    // Proxy work: progress of the running file (0..1) and when each ends.
    void proxyProgress(const QUuid &id, double fraction, int jobsLeft);
    void proxyFinished(const QUuid &id, bool ok, const QString &error);
    // Source monitor
    void sourceChanged();                     // another item opened (or closed)
    void sourcePositionChanged(qint64 frame);
    void sourceInOutChanged();
    void sourceFrameReady(const QImage &image);
    void sourcePlayingChanged(bool playing);
    void clipboardChanged();
    void openSequenceChanged(); // another sequence is shown on the timeline
    void loudnessAnalyzed(const QUuid &mediaId, bool ok);
    void sceneProgress(const QUuid &mediaId, double fraction);
    void scenesAnalyzed(const QUuid &mediaId, bool ok);
    // Grid of all angles (null image when no multicamera clip is at the playhead).
    void multicamGridReady(const QImage &image, const QUuid &clipId);
    void speechProgress(double fraction);
    void speechReady(bool ok, const QString &error);
    void multicamSoundProgress(int done, int total);
    void multicamSoundReady(bool ok);
    void mixLoudnessProgress(qint64 frame, qint64 total);
    void mixLoudnessMeasured(bool ok, const QString &error); // error empty after cancel
    void previewProgress(qint64 frame, qint64 total);
    void previewFinished(bool ok, const QString &error); // error empty after cancel
    void previewsChanged(); // parts rendered or deleted

private:
    void setModified(bool m);
    void setFilePath(const QString &path);
    void addRecent(const QString &path);
    void startProbe(const MediaItem &item);
    void startThumbnail(const MediaItem &item);
    void startWaveform(const MediaItem &item);
    static QString waveCachePath(const QString &mediaPath);
    void adopt(Project &&p, const QString &filePath, bool modified);
    void checkProxy(const MediaItem &item); // after probing: known proxy, or auto-create
    void startNextProxy();
    void refreshOnlineState();
    static QString thumbCachePath(const QString &mediaPath);
    // Phase 15b: the file a clip of another editor means: an item already in
    // the bin, a file on the disk (also next to the timeline file), or, when
    // none exists, the path it should have. *exists tells which.
    QString resolveInterchangePath(const Interchange::ImportClip &c, const QString &baseDir, bool *exists,
                                   QUuid *binItem) const;
public:
    // Everything an edit can change (see TimelineCommand).
    struct EditState
    {
        Timeline main;
        QList<NestedSequence> nested;
        QUuid open;
        bool operator==(const EditState &o) const { return main == o.main && nested == o.nested && open == o.open; }
    };

private:
    EditState editState() const;
    Timeline &current();
    void applyState(const EditState &s);
    void switchSequence(const QUuid &nestId);
    bool editTimeline(const QString &text, const std::function<bool(Timeline &)> &fn,
                      const QString &mergeKey = QString());
    bool editProject(const QString &text, const std::function<bool(EditState &)> &fn,
                     const QString &mergeKey = QString());
    void resetUndo();
    void requestProgramFrame();
    void startProgramFrame();

    friend class TimelineCommand;
    void syncPlayer();
    void syncSourcePlayer(); // rebuilds the source graph when its media changed
    void requestSourceFrame();
    void startSourceFrame();
    bool sourceEdit(bool insert);

    Project m_project;
    QString m_filePath;
    bool m_modified = false;
    QSet<QUuid> m_online;
    QSet<QUuid> m_pendingProbe;
    QHash<QUuid, QImage> m_thumbs;
    QHash<QUuid, QByteArray> m_waves;
    QHash<QUuid, std::shared_ptr<const Loudness::Profile>> m_loudness; // by media id
    QSet<QUuid> m_loudnessPending;
    QHash<QUuid, std::shared_ptr<const SceneDetect::Profile>> m_scenes; // by media id
    QSet<QUuid> m_scenesFailed;
    QList<QUuid> m_scenesQueue;
    QUuid m_scenesRunning;
    std::shared_ptr<std::atomic_bool> m_scenesCancel;
    void startNextScenes();
    quint64 m_changeSerial = 0;
    QHash<QUuid, QString> m_proxies;  // media id -> ready proxy file
    QList<QUuid> m_proxyQueue;
    QUuid m_proxyRunning;
    std::shared_ptr<std::atomic_bool> m_proxyCancel;
    QThreadPool m_proxyPool;          // one proxy at a time
    bool m_useProxies = true;
    QThreadPool m_pool;
    quint64 m_generation = 0; // ignores results of jobs started for an older project
    QUndoStack *m_undo = nullptr;
    qint64 m_playhead = 0;
    bool m_frameBusy = false;
    bool m_framePending = false;
    QString m_lastFrameKey;
    quint64 m_graphRev = 0; // changes whenever the engine graph is rebuilt
    Exporter *m_exporter = nullptr;
    TimelinePlayer *m_player = nullptr;
    QString m_engineError;

    // Copied clips: the clip, its kind of track and the n-th track of that kind.
    struct CopiedClip
    {
        TimelineClip clip;
        Timeline::Kind kind;
        int n;
    };
    QList<CopiedClip> m_clipboard; // starts relative to the first copied clip
    QUuid m_openNest;                       // null = main sequence on the timeline
    QHash<QUuid, qint64> m_nestPlayheads;   // playhead of each sequence while another is open

    // Source monitor
    QUuid m_sourceId;
    qint64 m_sourcePos = 0;
    QHash<QUuid, qint64> m_inPoints;
    QHash<QUuid, qint64> m_outPoints;
    TimelinePlayer *m_sourcePlayer = nullptr;
    QString m_sourceKey;      // what the source graph was built from
    quint64 m_sourceGen = 0;  // changes with every source graph
    bool m_sourceBusy = false;
    bool m_sourcePending = false;

    // Multicamera monitor (ProjectMulticam.cpp)
    std::shared_ptr<TimelinePlayer> m_gridPlayer; // created on first use
    QString m_gridGraphKey;  // what the grid player's graph was built from
    QString m_gridLastKey;   // last frame asked for
    bool m_gridBusy = false;
    bool m_gridPending = false;
    void applyPreviews(); // tells m_player which preview files to use
    mutable QList<PreviewPart> m_previewParts;
    mutable quint64 m_previewPartsRev = ~quint64(0);
    bool m_previewBusy = false;
    bool m_previewsOff = false;
    std::shared_ptr<std::atomic_bool> m_previewCancel;
    QThreadPool m_previewPool; // the preview renderer
    Loudness::Result m_mixResult;
    double m_mixSeconds = 0;
    quint64 m_mixRev = ~quint64(0); // graph the measurement belongs to
    bool m_mixBusy = false;
    std::shared_ptr<std::atomic_bool> m_mixCancel;
    QThreadPool m_mixPool; // the mix measurement
    QThreadPool m_scenePool; // one scene analysis at a time
    // Automatic multicamera edit (ProjectAutoMulticam.cpp): sound envelopes by media id.
    QHash<QUuid, std::shared_ptr<const std::vector<float>>> m_envelopes;
    QSet<QUuid> m_envelopesFailed;
    bool m_soundRunning = false;
    std::shared_ptr<std::atomic_bool> m_soundCancel;
    QThreadPool m_soundPool; // reads the cameras' sound
    // Subtitles from speech (ProjectSpeech.cpp): what was heard, by "media id|model dir".
    QHash<QString, Speech::Transcript> m_transcripts;
    bool m_speechRunning = false;
    std::shared_ptr<std::atomic_bool> m_speechCancel;
    QThreadPool m_speechPool;
    QThreadPool m_gridPool;  // one grid frame at a time; declared last, so it is destroyed (and waited for) first
};

} // namespace bc
