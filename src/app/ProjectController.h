#pragma once

#include "core/Loudness.h"
#include "core/Project.h"
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
    void cancelExport();
    bool isExporting() const;

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
};

} // namespace bc
