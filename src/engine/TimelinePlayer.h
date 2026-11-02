#pragma once

#include "engine/EngineServices.h"

#include "core/Project.h"

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QElapsedTimer>
#include <QSize>
#include <QThreadPool>
#include <memory>

namespace Mlt {
class Consumer;
class Event;
class Producer;
class Profile;
class Tractor;
} // namespace Mlt

namespace bc {

// MLT (LGPL) is the video engine. It turns the project's timeline into an
// MLT graph; the same graph will be used for export in phase 4, so preview
// and export always agree.
namespace Engine {
// Loads MLT modules (next to the program on Windows). Safe to call twice.
bool init(QString *error = nullptr);
bool isReady();
// Names of required MLT services that are not available ("" when all are).
QString missingServices();
QString versionString(); // "MLT 7.22.0"
} // namespace Engine

// Plays the timeline in real time with sound, and renders single frames
// while paused.
class TimelinePlayer : public QObject
{
    Q_OBJECT
public:
    explicit TimelinePlayer(QObject *parent = nullptr);
    ~TimelinePlayer() override;

    // Rebuilds the graph from the project (stops playback first).
    void setProject(const Project &project);
    // Light copies of media used for the picture (media id -> proxy file).
    // Takes effect at the next setProject(). Sound always comes from the
    // original. The exporter never sets any.
    void setProxies(const QHash<QUuid, QString> &proxies);
    // Rendered previews (phase 13b): in each segment the picture comes from
    // the file instead of the video tracks (sound and subtitles as usual).
    // Takes effect at the next setProject(). The exporter never sets any.
    struct PreviewSegment
    {
        qint64 start = 0;
        qint64 length = 0;
        QString path;
    };
    void setPreviews(const QList<PreviewSegment> &previews);
    QList<PreviewSegment> previews() const { return m_previews; }

    bool isPlaying() const { return m_playing; }
    // Starts playback at `frame`; frames arrive through frameShown().
    // `speed` 1 = normal, 2/4/8 = faster, negative = backwards (J/K/L keys).
    bool play(qint64 frame, double speed = 1);
    void pause();
    // Changes the speed while playing.
    void setSpeed(double speed);
    double speed() const { return m_playing ? m_speed : 0; }

    // Size of the preview images (keeps the sequence aspect ratio).
    void setPreviewLimit(const QSize &limit) { m_previewLimit = limit; }
    QSize previewSize() const;

    // Renders the composed frame at `frame`. Thread-safe, but returns a null
    // image while playing. Used for the paused Program monitor.
    QImage renderFrame(qint64 frame);
    // Loudness (RMS, 0..1) of the mixed sound of one frame; -1 when not
    // available. Used by tests now and by audio meters later.
    double audioLevel(qint64 frame);
    // Mixer meters (phase 10a): the last peaks of an audio track of the main
    // sequence after its fader, or of the master (null id). Thread-safe.
    EngineServices::MeterLevel meterLevel(const QUuid &trackId) const;

signals:
    // Emitted in the GUI thread for each frame shown during playback.
    void frameShown(const QImage &image, qint64 frame);
    void playingChanged(bool playing);
    void reachedEnd(); // or the start, when playing backwards

public:
    // Called from the MLT consumer thread for every displayed frame.
    void handleShownFrame(const QImage &image, qint64 frame);

private:
    friend class Exporter; // renders the same graph into a file
    void stopConsumer();
    QImage renderFrameLocked(qint64 frame);
    bool startReverse(qint64 frame, double speed);
    void reverseStep();
    // `speed` != 1 gives MLT's core "timewarp" producer (sound changes pitch).
    Mlt::Producer *producerFor(const MediaItem &item, bool audio, double speed = 1.0);
    Mlt::Producer *titleProducer(const TitleData &title);
    static constexpr int kMaxNestDepth = 8;
    std::unique_ptr<Mlt::Tractor> buildTractor(const Project &project, const Timeline &tl, qint64 length, int depth);
    // `angle`: for a multicamera sequence, the one video (or audio) track used.
    Mlt::Producer *nestProducer(const Project &project, const QUuid &nestId, bool audio, int depth, int angle = 0);

    mutable QMutex m_mutex; // guards the graph
    std::unique_ptr<Mlt::Profile> m_profile;
    std::unique_ptr<Mlt::Tractor> m_tractor;
    std::unique_ptr<Mlt::Consumer> m_consumer;
    std::unique_ptr<Mlt::Event> m_showEvent;
    // Separate producers for picture and sound, so the two tracks never fight
    // over one decoder position.
    struct Cached
    {
        QString path;
        QString videoSource; // path or its proxy
        std::shared_ptr<Mlt::Producer> video;
        std::shared_ptr<Mlt::Producer> audio;
        // Speed-changed producers by speed in thousandths.
        QHash<int, std::shared_ptr<Mlt::Producer>> warpedVideo;
        QHash<int, std::shared_ptr<Mlt::Producer>> warpedAudio;
    };
    QHash<QUuid, Cached> m_producers;
    QHash<QUuid, QString> m_proxies;
    QList<PreviewSegment> m_previews;                                // sorted by start
    QHash<QString, std::shared_ptr<Mlt::Producer>> m_previewProducers; // by file
    // Rendered titles by content, so unchanged titles are not drawn again.
    QHash<QByteArray, std::shared_ptr<Mlt::Producer>> m_titles;
    QSet<QByteArray> m_usedTitles;
    QHash<QString, std::shared_ptr<Mlt::Producer>> m_nestCache; // graphs of nested sequences
    SequenceSettings m_seq;
    qint64 m_duration = 0;
    QSize m_previewLimit{960, 540};
    QByteArray m_meterPrefix; // unique per player, keys of its bc_meter filters
    bool m_playing = false;
    double m_speed = 1;
    qint64 m_lastShown = 0;
    // Backwards playback (frames rendered one by one, see startReverse).
    bool m_reverse = false;
    qint64 m_revOrigin = 0;
    qint64 m_revShown = 0;
    QElapsedTimer m_revClock;
    quint64 m_revGen = 0;
    QThreadPool m_revPool;
};

} // namespace bc
