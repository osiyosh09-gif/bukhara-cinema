#include "engine/TimelinePlayer.h"

#include "engine/EngineServices.h"
#include "engine/TitleRenderer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QImageReader>
#include <QMetaObject>
#include <QMutexLocker>
#include <QStringList>

#include <mlt++/Mlt.h>

#include <algorithm>
#include <cmath>

namespace bc {

// ---------------------------------------------------------------------------
// Engine

namespace {
bool s_ready = false;
Mlt::Repository *s_repo = nullptr;
}

bool Engine::init(QString *error)
{
    if (s_ready)
        return true;
    if (s_repo) { // loaded before, but parts were missing
        if (error)
            *error = QStringLiteral("MLT modules are missing: %1").arg(missingServices());
        return false;
    }
    // BC_MLT_PREFIX points at an MLT installation (used by tests and CI).
    // The Windows package keeps MLT next to the program: modules in lib/mlt
    // and data in share/mlt. Elsewhere MLT finds its system installation.
    QDir prefix(QCoreApplication::applicationDirPath());
    const QByteArray envPrefix = qgetenv("BC_MLT_PREFIX");
    if (!envPrefix.isEmpty())
        prefix.setPath(QString::fromLocal8Bit(envPrefix));
    QByteArray repo;
    for (const QString &sub : {QStringLiteral("mlt"), QStringLiteral("mlt-7")}) {
        if (!prefix.exists(QStringLiteral("lib/") + sub))
            continue;
        repo = QDir::toNativeSeparators(prefix.filePath(QStringLiteral("lib/") + sub)).toLocal8Bit();
        const QString data = prefix.filePath(QStringLiteral("share/") + sub);
        qputenv("MLT_REPOSITORY", repo); // Linux and macOS read this, Windows needs the argument
        qputenv("MLT_DATA", QDir::toNativeSeparators(data).toLocal8Bit());
        qputenv("MLT_PROFILES_PATH", QDir::toNativeSeparators(data + QStringLiteral("/profiles/")).toLocal8Bit());
        qputenv("MLT_PRESETS_PATH", QDir::toNativeSeparators(data + QStringLiteral("/presets/")).toLocal8Bit());
        break;
    }
    mlt_repository raw = mlt_factory_init(repo.isEmpty() ? nullptr : repo.constData());
    Mlt::Repository *r = raw ? new Mlt::Repository(raw) : nullptr;
    if (!r) {
        if (error)
            *error = QStringLiteral("MLT modules could not be loaded");
        return false;
    }
    s_repo = r;
    EngineServices::registerAll(raw);
    const QString missing = missingServices();
    if (!missing.isEmpty()) {
        if (error)
            *error = QStringLiteral("MLT modules are missing: %1").arg(missing);
        return false;
    }
    s_ready = true;
    return true;
}

QString Engine::missingServices()
{
    if (!s_repo)
        return QStringLiteral("MLT");
    QStringList missing;
    std::unique_ptr<Mlt::Properties> producers(s_repo->producers());
    std::unique_ptr<Mlt::Properties> consumers(s_repo->consumers());
    std::unique_ptr<Mlt::Properties> transitions(s_repo->transitions());
    for (const char *name : {"avformat", "color", "bc_still"}) {
        if (!producers || !producers->property_exists(name))
            missing << QString::fromLatin1(name);
    }
    for (const char *name : {"composite", "mix"}) {
        if (!transitions || !transitions->property_exists(name))
            missing << QString::fromLatin1(name);
    }
    if (!consumers || !consumers->property_exists("sdl2_audio"))
        missing << QStringLiteral("sdl2_audio");
    return missing.join(QStringLiteral(", "));
}

bool Engine::isReady()
{
    return s_ready;
}

QString Engine::versionString()
{
    return QStringLiteral("MLT %1").arg(QString::fromLatin1(mlt_version_get_string()));
}

// ---------------------------------------------------------------------------
// TimelinePlayer

namespace {

void onFrameShow(mlt_properties, void *self, mlt_event_data data)
{
    auto *player = static_cast<TimelinePlayer *>(self);
    mlt_frame raw = mlt_event_data_to_frame(data);
    if (!raw)
        return;
    Mlt::Frame frame(raw);
    const QSize size = player->previewSize();
    mlt_image_format fmt = mlt_image_rgba;
    int w = size.width();
    int h = size.height();
    const uint8_t *img = frame.get_image(fmt, w, h);
    QImage out;
    if (img && w > 0 && h > 0)
        out = QImage(img, w, h, QImage::Format_RGBA8888).copy();
    player->handleShownFrame(out, frame.get_position());
}

} // namespace

TimelinePlayer::TimelinePlayer(QObject *parent)
    : QObject(parent)
{
}

TimelinePlayer::~TimelinePlayer()
{
    stopConsumer();
    QMutexLocker lock(&m_mutex);
    m_tractor.reset();
    m_producers.clear();
    m_profile.reset();
}

QSize TimelinePlayer::previewSize() const
{
    QSize s(std::max(2, m_seq.width), std::max(2, m_seq.height));
    if (s.width() > m_previewLimit.width() || s.height() > m_previewLimit.height())
        s.scale(m_previewLimit, Qt::KeepAspectRatio);
    // Even sizes keep the scalers happy.
    return QSize(std::max(2, s.width() & ~1), std::max(2, s.height() & ~1));
}

Mlt::Producer *TimelinePlayer::producerFor(const MediaItem &item, bool audio)
{
    Cached &c = m_producers[item.id];
    if (c.path != item.path) {
        c = Cached();
        c.path = item.path;
    }
    std::shared_ptr<Mlt::Producer> &p = audio ? c.audio : c.video;
    if (!p) {
        // MLT's loader adds the normalizing filters (scaling, colour conversion,
        // resampling). Video and sound go through FFmpeg ("avformat:"); still
        // pictures through our own "bc_still" producer, which keeps their
        // transparency and shows them for the whole clip.
        QByteArray resource;
        if (item.info.kind == MediaInfo::Kind::Image) {
            if (audio)
                return nullptr;
            QImageReader reader(item.path);
            reader.setAutoTransform(true); // camera rotation (EXIF)
            const QImage image = reader.read();
            if (image.isNull())
                return nullptr;
            resource = EngineServices::stillResource(image);
        } else {
            resource = "avformat:" + item.path.toUtf8();
        }
        p = std::make_shared<Mlt::Producer>(*m_profile, nullptr, resource.constData());
        if (!p->is_valid()) {
            p.reset();
            return nullptr;
        }
        // Each producer decodes only what it is used for.
        if (audio)
            p->set("video_index", -1);
        else
            p->set("audio_index", -1);
    }
    return p.get();
}

Mlt::Producer *TimelinePlayer::titleProducer(const TitleData &title)
{
    const QByteArray key = QCryptographicHash::hash(
        title.text.toUtf8() + '\0' + QByteArray::number(title.size) + '\0' + title.color.toUtf8() + '\0'
            + QByteArray::number(int(title.position)) + QByteArray::number(int(title.box)),
        QCryptographicHash::Sha1);
    std::shared_ptr<Mlt::Producer> &p = m_titles[key];
    if (!p) {
        const QImage img = renderTitle(title, QSize(m_seq.width, m_seq.height));
        p = std::make_shared<Mlt::Producer>(*m_profile, nullptr, EngineServices::stillResource(img).constData());
        if (!p->is_valid()) {
            m_titles.remove(key);
            return nullptr;
        }
    }
    m_usedTitles.insert(key);
    return p.get();
}

void TimelinePlayer::setProject(const Project &project)
{
    pause();
    QMutexLocker lock(&m_mutex);
    if (!Engine::isReady())
        return;

    const SequenceSettings &seq = project.sequence;
    if (!m_profile || seq != m_seq) {
        m_tractor.reset();
        m_producers.clear();
        m_titles.clear();
        m_profile = std::make_unique<Mlt::Profile>();
        m_profile->set_width(seq.width);
        m_profile->set_height(seq.height);
        m_profile->set_frame_rate(seq.fpsNum, seq.fpsDen);
        m_profile->set_sample_aspect(1, 1);
        m_profile->set_display_aspect(seq.width, seq.height);
        m_profile->set_progressive(1);
        m_profile->set_colorspace(709);
        m_profile->set_explicit(1);
        m_seq = seq;
    }
    // Forget producers of removed media.
    for (auto it = m_producers.begin(); it != m_producers.end();) {
        const MediaItem *m = project.item(it.key());
        if (!m || m->path != it->path)
            it = m_producers.erase(it);
        else
            ++it;
    }

    const Timeline &tl = project.timeline;
    m_duration = tl.duration();
    m_usedTitles.clear();
    const int bgLength = int(std::max<qint64>(1, m_duration));

    auto tractor = std::make_unique<Mlt::Tractor>(*m_profile);
    // Track 0: black picture and silence under everything.
    Mlt::Playlist bgList(*m_profile);
    Mlt::Producer bg(*m_profile, "color:black");
    if (!bg.is_valid())
        return;
    bg.set("length", bgLength);
    bg.set("out", bgLength - 1);
    bgList.append(bg, 0, bgLength - 1);
    tractor->set_track(bgList, 0);

    auto fill = [&](Mlt::Playlist &pl, const TimelineTrack &track, bool audio) {
        qint64 pos = 0;
        for (const TimelineClip &c : track.clips) {
            if (c.start > pos)
                pl.blank(int(c.start - pos - 1));
            Mlt::Producer *p = nullptr;
            if (c.isTitle()) {
                p = audio ? nullptr : titleProducer(c.title);
            } else if (const MediaItem *m = project.item(c.mediaId)) {
                p = producerFor(*m, audio);
            }
            if (p) {
                const int in = c.isTitle() ? 0 : int(c.in);
                std::unique_ptr<Mlt::Producer> cut(p->cut(in, int(in + c.length - 1)));
                if (audio && (c.gainDb != 0 || c.fadeIn > 0 || c.fadeOut > 0)) {
                    Mlt::Filter gain(*m_profile, "bc_gain");
                    if (gain.is_valid()) {
                        gain.set("gain_db", c.gainDb);
                        gain.set("fade_in", double(c.fadeIn));
                        gain.set("fade_out", double(c.fadeOut));
                        gain.set_in_and_out(in, int(in + c.length - 1));
                        cut->attach(gain);
                    }
                }
                pl.append(*cut);
            } else {
                pl.blank(int(c.length - 1)); // missing file: gap
            }
            pos = c.end();
        }
    };

    // Sound of every audio track is mixed onto track 0; pictures of the video
    // tracks are composited onto track 0 from V1 upwards, so V2 lies over V1
    // and transparent parts (titles, PNG) show what is below.
    int index = 1;
    for (int ti = 0; ti < tl.tracks.size(); ++ti) {
        const TimelineTrack &t = tl.tracks[ti];
        if (t.kind != Timeline::Kind::Audio)
            continue;
        Mlt::Playlist pl(*m_profile);
        fill(pl, t, true);
        pl.set("hide", tl.isAudible(ti) ? 1 : 3); // 1 = no picture, 3 = nothing
        tractor->set_track(pl, index);
        Mlt::Transition mix(*m_profile, "mix");
        mix.set("always_active", 1);
        mix.set("sum", 1);
        tractor->plant_transition(mix, 0, index);
        ++index;
    }
    for (const TimelineTrack &t : tl.tracks) {
        if (t.kind != Timeline::Kind::Video)
            continue;
        Mlt::Playlist pl(*m_profile);
        fill(pl, t, false);
        pl.set("hide", t.hidden ? 3 : 2); // 2 = no sound (it lives on the A tracks)
        tractor->set_track(pl, index);
        if (!t.hidden) {
            Mlt::Transition over(*m_profile, "composite");
            over.set("always_active", 1);
            // Every layer already has the frame size (the loader letterboxes
            // video, bc_still fits pictures), so it is laid 1:1 over the frame.
            over.set("fill", 1);
            over.set("distort", 1);
            over.set("geometry", "0%/0%:100%x100%:100");
            tractor->plant_transition(over, 0, index);
        }
        ++index;
    }
    m_tractor = std::move(tractor);
    for (auto it = m_titles.begin(); it != m_titles.end();) {
        if (m_usedTitles.contains(it.key()))
            ++it;
        else
            it = m_titles.erase(it);
    }
}

QImage TimelinePlayer::renderFrame(qint64 frame)
{
    if (m_playing)
        return {};
    QMutexLocker lock(&m_mutex);
    if (!m_tractor)
        return {};
    m_tractor->seek(int(frame));
    std::unique_ptr<Mlt::Frame> f(m_tractor->get_frame());
    if (!f)
        return {};
    const QSize size = previewSize();
    mlt_image_format fmt = mlt_image_rgba;
    int w = size.width();
    int h = size.height();
    const uint8_t *img = f->get_image(fmt, w, h);
    if (!img || w <= 0 || h <= 0)
        return {};
    return QImage(img, w, h, QImage::Format_RGBA8888).copy();
}

double TimelinePlayer::audioLevel(qint64 frame)
{
    if (m_playing)
        return -1;
    QMutexLocker lock(&m_mutex);
    if (!m_tractor)
        return -1;
    m_tractor->seek(int(frame));
    std::unique_ptr<Mlt::Frame> f(m_tractor->get_frame());
    if (!f)
        return -1;
    mlt_audio_format fmt = mlt_audio_f32le;
    int freq = m_seq.sampleRate > 0 ? m_seq.sampleRate : 48000;
    int channels = 2;
    int samples = mlt_audio_calculate_frame_samples(float(m_profile->fps()), freq, frame);
    auto *pcm = static_cast<float *>(f->get_audio(fmt, freq, channels, samples));
    if (!pcm || fmt != mlt_audio_f32le || samples <= 0)
        return -1;
    double sum = 0;
    for (int i = 0; i < samples * channels; ++i)
        sum += double(pcm[i]) * pcm[i];
    return std::sqrt(sum / (samples * channels));
}

bool TimelinePlayer::play(qint64 frame)
{
    if (m_playing)
        return true;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_tractor)
            return false;
        auto consumer = std::make_unique<Mlt::Consumer>(*m_profile, "sdl2_audio");
        if (!consumer->is_valid())
            return false;
        const QSize size = previewSize();
        consumer->set("width", size.width());
        consumer->set("height", size.height());
        consumer->set("real_time", 1);       // drop frames rather than fall behind (slow PCs)
        consumer->set("terminate_on_pause", 0);
        consumer->set("volume", 1.0);
        consumer->connect(*m_tractor);
        m_showEvent.reset(consumer->listen("consumer-frame-show", this, (mlt_listener)onFrameShow));
        m_tractor->seek(int(frame));
        m_tractor->set_speed(1);
        if (consumer->start() != 0) {
            m_showEvent.reset();
            m_tractor->set_speed(0);
            return false;
        }
        m_consumer = std::move(consumer);
        m_playing = true;
    }
    emit playingChanged(true);
    return true;
}

void TimelinePlayer::stopConsumer()
{
    if (!m_consumer)
        return;
    m_consumer->stop(); // waits for the consumer thread
    m_showEvent.reset();
    m_consumer.reset();
    if (m_tractor)
        m_tractor->set_speed(0);
}

void TimelinePlayer::pause()
{
    if (!m_playing)
        return;
    {
        QMutexLocker lock(&m_mutex);
        stopConsumer();
        m_playing = false;
    }
    emit playingChanged(false);
}

void TimelinePlayer::handleShownFrame(const QImage &image, qint64 frame)
{
    QMetaObject::invokeMethod(this, [this, image, frame] {
        if (!m_playing)
            return;
        emit frameShown(image, frame);
        if (frame >= m_duration - 1) {
            pause();
            emit reachedEnd();
        }
    }, Qt::QueuedConnection);
}

} // namespace bc
