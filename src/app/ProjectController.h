#pragma once

#include "core/Project.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QThreadPool>

#include <functional>

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

    void setSequence(const SequenceSettings &s);

    // --- timeline (every edit is undoable)
    const Timeline &timeline() const { return m_project.timeline; }
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
    // Splits the given clips (or every clip on unlocked tracks when empty) at `frame`.
    bool splitClips(qint64 frame, const QList<QUuid> &clipIds);
    bool removeClips(const QList<QUuid> &clipIds, bool ripple);
    int addTrack(Timeline::Kind kind);
    void setTrackFlag(int track, const QString &flag, bool on); // "locked", "muted", "hidden", "solo"
    // Adds a 5-second title at `start` on the highest video track that is free
    // there (a new track when none is). Returns the new clip id.
    QUuid addTitle(qint64 start);
    // Changes a title clip. Quick successive changes of one clip (typing)
    // form a single undo step.
    bool setClipTitle(const QUuid &clipId, const TitleData &title);
    // Volume (dB) and fades (frames) of an audio clip; also one undo step
    // while a slider is dragged.
    bool setClipAudio(const QUuid &clipId, double gainDb, qint64 fadeIn, qint64 fadeOut);
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

private:
    void setModified(bool m);
    void setFilePath(const QString &path);
    void addRecent(const QString &path);
    void startProbe(const MediaItem &item);
    void startThumbnail(const MediaItem &item);
    void refreshOnlineState();
    static QString thumbCachePath(const QString &mediaPath);
    bool editTimeline(const QString &text, const std::function<bool(Timeline &)> &fn,
                      const QString &mergeKey = QString());
    void resetUndo();
    void requestProgramFrame();
    void startProgramFrame();

    friend class TimelineCommand;
    void applyTimeline(const Timeline &t);
    void syncPlayer();

    Project m_project;
    QString m_filePath;
    bool m_modified = false;
    QSet<QUuid> m_online;
    QSet<QUuid> m_pendingProbe;
    QHash<QUuid, QImage> m_thumbs;
    QThreadPool m_pool;
    quint64 m_generation = 0; // ignores results of jobs started for an older project
    QUndoStack *m_undo = nullptr;
    qint64 m_playhead = 0;
    bool m_frameBusy = false;
    bool m_framePending = false;
    QString m_lastFrameKey;
    quint64 m_graphRev = 0; // changes whenever the engine graph is rebuilt
    TimelinePlayer *m_player = nullptr;
    QString m_engineError;
};

} // namespace bc
