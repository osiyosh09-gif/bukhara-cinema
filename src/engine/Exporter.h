#pragma once

#include "core/MediaProbe.h"
#include "core/Project.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>

namespace Mlt {
class Consumer;
}

namespace bc {

class TimelinePlayer;

struct ExportSettings
{
    QString path;                 // .mp4 file
    MediaProbe::VideoEncoder encoder;
    int videoKbps = 8000;
    int audioKbps = 192;
};

// Writes the timeline to an MP4 file with the same MLT graph that the
// Program monitor plays, so the file looks and sounds like the preview.
// Runs in MLT's own thread; progress and the end arrive as signals.
class Exporter : public QObject
{
    Q_OBJECT
public:
    explicit Exporter(QObject *parent = nullptr);
    ~Exporter() override;

    // Video bit rate that suits the frame size: quality 0 = high, 1 = medium, 2 = small file.
    static int suggestedKbps(const SequenceSettings &seq, int quality);

    bool start(const Project &project, const ExportSettings &settings, QString *error);
    void cancel(); // stops and deletes the unfinished file
    bool isRunning() const { return m_running; }
    qint64 totalFrames() const { return m_total; }

signals:
    void progress(qint64 frame, qint64 total, qint64 remainingMs);
    void finished(bool ok, const QString &error); // error is empty after cancel()

private:
    void poll();
    void finish(bool ok, const QString &error);

    std::unique_ptr<TimelinePlayer> m_graph;
    std::unique_ptr<Mlt::Consumer> m_consumer;
    QTimer m_timer;
    QElapsedTimer m_clock;
    ExportSettings m_settings;
    qint64 m_total = 0;
    bool m_running = false;
};

} // namespace bc
