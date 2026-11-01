#include "engine/Exporter.h"

#include "engine/TimelinePlayer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

#include <mlt++/Mlt.h>

#include <algorithm>
#include <cmath>

namespace bc {

Exporter::Exporter(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &Exporter::poll);
}

Exporter::~Exporter()
{
    if (m_running)
        cancel();
}

int Exporter::suggestedKbps(const SequenceSettings &seq, int quality)
{
    // About 8 Mbit/s for 1080p at 25 fps as "medium", scaled by pixels and fps.
    const double pixels = double(seq.width) * seq.height / (1920.0 * 1080.0);
    const double fps = std::max(1.0, seq.fps()) / 25.0;
    const double base = 8000.0 * pixels * std::sqrt(fps);
    const double factor = quality <= 0 ? 2.0 : quality == 1 ? 1.0 : 0.5;
    return std::max(500, int(std::lround(base * factor / 100.0) * 100));
}

bool Exporter::start(const Project &project, const ExportSettings &settings, QString *error)
{
    auto fail = [&](const QString &text) {
        if (error)
            *error = text;
        return false;
    };
    if (m_running)
        return fail(tr("An export is already running."));
    if (!Engine::isReady())
        return fail(tr("The video engine (MLT) is not loaded."));
    m_total = project.timeline.duration();
    if (m_total <= 0)
        return fail(tr("The sequence is empty."));
    if (settings.path.isEmpty())
        return fail(tr("Choose a file name."));

    m_graph = std::make_unique<TimelinePlayer>();
    m_graph->setProject(project);
    if (!m_graph->m_tractor || !m_graph->m_profile)
        return fail(tr("The sequence could not be prepared for export."));

    const SequenceSettings &seq = project.sequence;
    auto consumer = std::make_unique<Mlt::Consumer>(*m_graph->m_profile, "avformat",
                                                    QFile::encodeName(settings.path).constData());
    if (!consumer->is_valid())
        return fail(tr("The MP4 writer (MLT avformat) is not available."));
    consumer->set("f", "mp4");
    consumer->set("movflags", "+faststart"); // plays while downloading
    consumer->set("vcodec", settings.encoder.codec.toLatin1().constData());
    consumer->set("pix_fmt", settings.encoder.pixFmt.toLatin1().constData());
    consumer->set("vb", QByteArray::number(settings.videoKbps) + "k");
    consumer->set("g", std::max(1, int(std::lround(seq.fps() * 2)))); // key frame every 2 s
    consumer->set("bf", 0);
    consumer->set("acodec", "aac");
    consumer->set("ab", QByteArray::number(settings.audioKbps) + "k");
    consumer->set("ar", seq.sampleRate > 0 ? seq.sampleRate : 48000);
    consumer->set("channels", seq.channels > 0 ? seq.channels : 2);
    consumer->set("width", seq.width);
    consumer->set("height", seq.height);
    consumer->set("progressive", 1);
    consumer->set("real_time", 0);          // every frame, as fast as the computer can
    consumer->set("terminate_on_pause", 1); // stop at the end of the sequence
    consumer->set("in", 0);
    consumer->set("out", int(m_total - 1));

    {
        QMutexLocker lock(&m_graph->m_mutex);
        m_graph->m_tractor->set("out", int(m_total - 1));
        m_graph->m_tractor->seek(0);
        m_graph->m_tractor->set_speed(1);
        consumer->connect(*m_graph->m_tractor);
    }
    QFile::remove(settings.path);
    if (consumer->start() != 0)
        return fail(tr("The export could not start."));
    m_consumer = std::move(consumer);
    m_settings = settings;
    m_running = true;
    m_clock.start();
    m_timer.start();
    emit progress(0, m_total, -1);
    return true;
}

void Exporter::poll()
{
    if (!m_running)
        return;
    const qint64 frame = std::clamp<qint64>(m_graph->m_tractor->position(), 0, m_total);
    const qint64 elapsed = m_clock.elapsed();
    const qint64 remaining = frame > 0 ? elapsed * (m_total - frame) / frame : -1;
    emit progress(frame, m_total, remaining);
    if (m_consumer->is_stopped())
        finish(true, QString());
}

void Exporter::finish(bool ok, const QString &error)
{
    m_timer.stop();
    if (m_consumer) {
        m_consumer->stop(); // writes the end of the file
        m_consumer.reset();
    }
    m_graph.reset();
    m_running = false;
    if (ok) {
        const QFileInfo fi(m_settings.path);
        if (!fi.exists() || fi.size() == 0) {
            ok = false;
            emit finished(false, tr("The file was not written. Check the folder and free disk space."));
            return;
        }
        emit progress(m_total, m_total, 0);
    }
    emit finished(ok, error);
}

void Exporter::cancel()
{
    if (!m_running)
        return;
    finish(false, QString());
    QFile::remove(m_settings.path);
}

} // namespace bc
