#include "engine/TimelinePlayer.h"

#include "engine/EngineServices.h"
#include "engine/TitleRenderer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QStringList>
#include <QTimer>

#include <mlt++/Mlt.h>

#include <algorithm>
#include <atomic>
#include <clocale>
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
    // MLT reads numbers in text (like the speed in "timewarp:1.5:...") with the
    // C library's locale; with a Russian or Uzbek locale "1.5" would not parse.
    std::setlocale(LC_NUMERIC, "C");
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
    for (const char *name : {"avformat", "color", "bc_still", "timewarp"}) {
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
    static std::atomic<int> s_players{0};
    m_meterPrefix = "p" + QByteArray::number(++s_players) + ':';
    m_revPool.setMaxThreadCount(1);
}

EngineServices::MeterLevel TimelinePlayer::meterLevel(const QUuid &trackId) const
{
    return EngineServices::meterLevel(m_meterPrefix
                                      + (trackId.isNull() ? QByteArray("master") : trackId.toByteArray()));
}

TimelinePlayer::~TimelinePlayer()
{
    ++m_revGen;
    m_reverse = false;
    m_playing = false;
    m_revPool.waitForDone();
    stopConsumer();
    EngineServices::clearMeters(m_meterPrefix);
    QMutexLocker lock(&m_mutex);
    m_tractor.reset();
    m_nestCache.clear();
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

void TimelinePlayer::setProxies(const QHash<QUuid, QString> &proxies)
{
    pause();
    QMutexLocker lock(&m_mutex);
    m_proxies = proxies;
    // Close files that are no longer wanted (a deleted proxy must not stay open).
    for (auto it = m_producers.begin(); it != m_producers.end(); ++it)
        if (it->video && it->videoSource != m_proxies.value(it.key(), it->path))
            it->video.reset();
}

void TimelinePlayer::setPreviews(const QList<PreviewSegment> &previews)
{
    pause();
    QMutexLocker lock(&m_mutex);
    m_previews = previews;
    std::sort(m_previews.begin(), m_previews.end(),
              [](const PreviewSegment &a, const PreviewSegment &b) { return a.start < b.start; });
    QSet<QString> wanted;
    for (const PreviewSegment &p : m_previews)
        wanted.insert(p.path);
    for (auto it = m_previewProducers.begin(); it != m_previewProducers.end();)
        it = wanted.contains(it.key()) ? std::next(it) : m_previewProducers.erase(it);
}

Mlt::Producer *TimelinePlayer::producerFor(const MediaItem &item, bool audio, double speed)
{
    Cached &c = m_producers[item.id];
    if (c.path != item.path) {
        c = Cached();
        c.path = item.path;
    }
    const QString videoSource = m_proxies.value(item.id, item.path);
    if (!audio && c.videoSource != videoSource) {
        c.video.reset(); // proxy switched on or off
        c.warpedVideo.clear();
        c.videoSource = videoSource;
    }
    const int warp = int(std::lround(speed * 1000));
    if (warp != 1000 && item.info.kind != MediaInfo::Kind::Image) {
        std::shared_ptr<Mlt::Producer> &w = audio ? c.warpedAudio[warp] : c.warpedVideo[warp];
        if (!w) {
            const QByteArray resource = "timewarp:" + QByteArray::number(warp / 1000.0, 'f', 3) + ":avformat:"
                                        + (audio ? item.path : videoSource).toUtf8();
            w = std::make_shared<Mlt::Producer>(*m_profile, nullptr, resource.constData());
            if (!w->is_valid()) {
                w.reset();
                return nullptr;
            }
            if (audio)
                w->set("video_index", -1);
            else
                w->set("audio_index", -1);
        }
        return w.get();
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
            resource = "avformat:" + (audio ? item.path : videoSource).toUtf8();
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

// Builds the graph of one timeline (the project's, or a nested sequence's,
// down to kMaxNestDepth levels). `length` is at least the timeline duration;
// nested sequences get more, so a clip trimmed past their end shows black.
std::unique_ptr<Mlt::Tractor> TimelinePlayer::buildTractor(const Project &project, const Timeline &tl, qint64 length,
                                                           int depth)
{
    const int bgLength = int(std::max<qint64>(1, length));
    auto tractor = std::make_unique<Mlt::Tractor>(*m_profile);
    // Rendered previews (main graph only, phase 13b): the video tracks are
    // blank there and track 0 plays the preview file instead of black. On
    // track 0 the picture is not composited again, so it looks exactly like
    // the computed one (each composite converts the colours once).
    QList<PreviewSegment> previews;
    if (depth == 0)
        for (const PreviewSegment &p : m_previews)
            if (p.length > 0 && p.start >= 0 && p.start + p.length <= bgLength
                && (previews.isEmpty() || p.start >= previews.last().start + previews.last().length))
                previews << p;
    auto lay = [](qint64 count) { return int(std::max<qint64>(0, count) - 1); };
    // Track 0: black picture and silence under everything. Inside a nested
    // sequence it is transparent, so tracks below the nested clip show.
    Mlt::Playlist bgList(*m_profile);
    Mlt::Producer bg(*m_profile, depth == 0 ? "color:black" : "color:#00000000");
    if (!bg.is_valid())
        return nullptr;
    bg.set("length", bgLength);
    bg.set("out", bgLength - 1);
    {
        qint64 pos = 0;
        for (const PreviewSegment &p : previews) {
            std::shared_ptr<Mlt::Producer> &prod = m_previewProducers[p.path];
            if (!prod) {
                // Through MLT's loader, which adds the format converters.
                prod = std::make_shared<Mlt::Producer>(*m_profile, nullptr,
                                                       ("bc_preview:" + p.path.toUtf8()).constData());
                if (!prod->is_valid()) {
                    prod.reset();
                    continue;
                }
            }
            if (p.start > pos)
                bgList.append(bg, 0, lay(p.start - pos));
            const qint64 len = std::min<qint64>(p.length, prod->get_length());
            bgList.append(*prod, 0, lay(len));
            if (p.length > len)
                bgList.append(bg, 0, lay(p.length - len));
            pos = p.start + p.length;
        }
        if (bgLength > pos)
            bgList.append(bg, 0, lay(bgLength - pos));
    }
    tractor->set_track(bgList, 0);

    auto fill = [&](Mlt::Playlist &pl, const TimelineTrack &track, bool audio) {
        const QList<TimelineClip> &clips = track.clips;
        const int n = int(clips.size());
        QVector<Mlt::Producer *> prod(n, nullptr);
        QVector<qint64> baseIn(n, 0);   // first frame of the clip in the producer's frames
        QVector<bool> still(n, false);  // every frame looks the same (titles, pictures)
        for (int i = 0; i < n; ++i) {
            const TimelineClip &c = clips[i];
            if (c.isNested()) {
                prod[i] = nestProducer(project, c.nestId, audio, depth, c.angle);
            } else if (c.isTitle()) {
                prod[i] = audio ? nullptr : titleProducer(c.title);
                still[i] = true;
            } else if (const MediaItem *m = project.item(c.mediaId)) {
                prod[i] = producerFor(*m, audio, c.speed);
                still[i] = m->info.kind == MediaInfo::Kind::Image;
            }
            // A speed-changed producer counts its own (warped) frames.
            baseIn[i] = still[i] ? 0 : c.speed != 1.0 ? qint64(std::llround(c.in / c.speed)) : c.in;
        }

        // Cuts with a transition: the clip before plays `b` frames past the
        // cut and the clip after starts `a` frames early (centred when the
        // sources have the frames), and MLT mixes the a + b frames.
        QVector<qint64> headHandle(n, 0), tailHandle(n, 0), mixLen(n, 0);
        auto touching = [&](int i) { return i > 0 && clips[i - 1].end() == clips[i].start; };
        for (int i = 1; i < n; ++i) {
            const TimelineClip &A = clips[i - 1];
            const TimelineClip &B = clips[i];
            if (!touching(i) || B.head.isNone() || !prod[i - 1] || !prod[i])
                continue;
            const qint64 L = std::min({B.head.length, (A.length - 1) / 2, (B.length - 1) / 2});
            const qint64 hB = still[i] ? L : baseIn[i];
            const qint64 hA = still[i - 1] ? L : qint64(prod[i - 1]->get_length()) - (baseIn[i - 1] + A.length);
            qint64 a = L / 2, b = L - a;
            if (a > hB) {
                a = std::max<qint64>(0, hB);
                b = std::min(L - a, hA);
            }
            if (b > hA) {
                b = std::max<qint64>(0, hA);
                a = std::min(L - b, std::max<qint64>(0, hB));
            }
            if (a + b < 2)
                continue; // no frames to mix: a plain cut
            headHandle[i] = a;
            tailHandle[i - 1] = b;
            mixLen[i] = a + b;
        }

        QVector<int> index(n, -1);
        qint64 pos = 0;
        for (int i = 0; i < n; ++i) {
            const TimelineClip &c = clips[i];
            if (c.start > pos)
                pl.blank(int(c.start - pos - 1));
            pos = c.end();
            Mlt::Producer *p = prod[i];
            if (!p) {
                pl.blank(int(c.length - 1)); // missing file: gap
                continue;
            }
            const qint64 a = headHandle[i];
            const qint64 total = c.length + a + tailHandle[i];
            const int in = int(still[i] ? 0 : baseIn[i] - a);
            std::unique_ptr<Mlt::Producer> cut(p->cut(in, int(in + total - 1)));
            const bool nextTouches = i + 1 < n && clips[i + 1].start == c.end();
            const ClipTransition headFade = mixLen[i] == 0 && !c.head.isNone() ? c.head : ClipTransition();
            const ClipTransition tailFade = !nextTouches && !c.tail.isNone() ? c.tail : ClipTransition();
            const MediaItem *media = c.mediaId.isNull() ? nullptr : project.item(c.mediaId);
            const double sourceAspect = media && media->info.kind == MediaInfo::Kind::Video && media->info.hasVideo()
                                            ? double(media->info.width) / media->info.height
                                            : 0.0;
            if (!audio && (effectsActive(c.effects) || !c.color.isNeutral())) {
                Mlt::Filter effects(*m_profile, "bc_effects");
                if (effects.is_valid()) {
                    effects.set("color", c.color.toEngineString().constData());
                    effects.set("effects", effectsToEngineString(c.effects).constData());
                    if (sourceAspect > 0)
                        effects.set("source_aspect", sourceAspect);
                    effects.set_in_and_out(in, int(in + total - 1));
                    cut->attach(effects);
                }
            }
            if (!audio && (c.motion.animated() || !c.motion.isIdentity())) {
                Mlt::Filter motion(*m_profile, "bc_transform");
                if (motion.is_valid()) {
                    // Keys are in source frames; the filter counts from the cut's first frame.
                    motion.set("motion", c.motion.toEngineString(c.in - c.src(a)).constData());
                    motion.set("speed", c.speed);
                    if (sourceAspect > 0)
                        motion.set("source_aspect", sourceAspect);
                    motion.set_in_and_out(in, int(in + total - 1));
                    cut->attach(motion);
                }
            }
            if (!audio && (!headFade.isNone() || !tailFade.isNone())) {
                Mlt::Filter fade(*m_profile, "bc_fade");
                if (fade.is_valid()) {
                    fade.set("lead", double(a));
                    fade.set("length", double(c.length));
                    fade.set("head_kind", int(headFade.kind));
                    fade.set("head_len", double(std::min(headFade.length, c.length)));
                    fade.set("tail_kind", int(tailFade.kind));
                    fade.set("tail_len", double(std::min(tailFade.length, c.length)));
                    fade.set_in_and_out(in, int(in + total - 1));
                    cut->attach(fade);
                }
            }
            const qint64 fadeIn = std::max(c.fadeIn, std::min(headFade.length, c.length));
            const qint64 fadeOut = std::max(c.fadeOut, std::min(tailFade.length, c.length));
            // Sound effects (equalizer, compressor) come before the clip volume.
            if (audio && effectsActive(c.effects)) {
                Mlt::Filter fx(*m_profile, "bc_audiofx");
                if (fx.is_valid()) {
                    fx.set("effects", effectsToEngineString(c.effects).constData());
                    cut->attach(fx);
                }
            }
            if (audio && (c.gainDb != 0 || fadeIn > 0 || fadeOut > 0)) {
                Mlt::Filter gain(*m_profile, "bc_gain");
                if (gain.is_valid()) {
                    gain.set("gain_db", c.gainDb);
                    gain.set("fade_in", double(fadeIn));
                    gain.set("fade_out", double(fadeOut));
                    gain.set("lead", double(a));
                    gain.set("length", double(c.length));
                    gain.set_in_and_out(in, int(in + total - 1));
                    cut->attach(gain);
                }
            }
            index[i] = pl.count();
            if (mixLen[i] > 0 || (i + 1 < n && mixLen[i + 1] > 0)) {
                // MLT mixes new cuts of the clip's producer, which would lose the
                // filters of this cut: a one-clip playlist keeps them together.
                Mlt::Playlist wrap(*m_profile);
                wrap.append(*cut);
                pl.append(wrap);
            } else {
                pl.append(*cut);
            }
        }
        // From the end, so earlier indexes stay valid.
        for (int i = n - 1; i >= 1; --i) {
            if (mixLen[i] <= 0 || index[i - 1] < 0)
                continue;
            if (pl.mix(index[i - 1], int(mixLen[i])) != 0)
                continue;
            if (audio) {
                Mlt::Transition mix(*m_profile, "mix");
                mix.set("start", 0.0);
                mix.set("end", 1.0);
                pl.mix_add(index[i - 1] + 1, &mix);
            } else {
                Mlt::Transition xfade(*m_profile, "bc_xfade");
                xfade.set("kind", int(clips[i].head.kind));
                pl.mix_add(index[i - 1] + 1, &xfade);
            }
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
        // Mixer strip: fader and balance, then the meter (after the fader).
        if (t.gainDb != 0 || t.pan != 0) {
            Mlt::Filter fader(*m_profile, "bc_gain");
            if (fader.is_valid()) {
                fader.set("gain_db", t.gainDb);
                fader.set("pan", t.pan);
                pl.attach(fader);
            }
        }
        if (depth == 0) {
            Mlt::Filter meter(*m_profile, "bc_meter");
            if (meter.is_valid()) {
                meter.set("meter", (m_meterPrefix + t.id.toByteArray()).constData());
                pl.attach(meter);
            }
        }
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
        Mlt::Playlist inner(*m_profile);
        fill(inner, t, false);
        Mlt::Playlist outer(*m_profile);
        Mlt::Playlist *pl = &inner;
        if (!previews.isEmpty() && !t.hidden) {
            // The track as one producer, cut around the previews (keeps its
            // filters and transitions intact).
            const qint64 have = inner.get_playtime();
            qint64 pos = 0;
            auto take = [&](qint64 to) {
                const qint64 upTo = std::min(to, have);
                if (upTo > pos) {
                    std::unique_ptr<Mlt::Producer> cut(inner.cut(int(pos), int(upTo - 1)));
                    outer.append(*cut);
                    pos = upTo;
                }
                if (to > pos) {
                    outer.blank(lay(to - pos));
                    pos = to;
                }
            };
            for (const PreviewSegment &p : previews) {
                take(p.start);
                if (p.start + p.length > pos) {
                    outer.blank(lay(p.start + p.length - pos));
                    pos = p.start + p.length;
                }
            }
            if (have > pos)
                take(have);
            pl = &outer;
        }
        pl->set("hide", t.hidden ? 3 : 2); // 2 = no sound (it lives on the A tracks)
        tractor->set_track(*pl, index);
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
    // Subtitles drawn into the picture (phase 11).
    if (tl.subtitleStyle.show && !tl.subtitles.isEmpty()) {
        QJsonArray cues;
        for (const Subtitle &st : tl.subtitles)
            if (!st.text.trimmed().isEmpty() && st.length > 0)
                cues.append(QJsonArray{double(st.start), double(st.end()), st.text});
        Mlt::Filter subs(*m_profile, "bc_subtitles");
        if (subs.is_valid() && !cues.isEmpty()) {
            const QJsonObject root{{QStringLiteral("style"), tl.subtitleStyle.toJson()},
                                   {QStringLiteral("cues"), cues}};
            subs.set("subtitles", QJsonDocument(root).toJson(QJsonDocument::Compact).constData());
            tractor->attach(subs);
        }
    }
    // Master fader and meter on the mixed result.
    if (tl.masterGainDb != 0) {
        Mlt::Filter master(*m_profile, "bc_gain");
        if (master.is_valid()) {
            master.set("gain_db", tl.masterGainDb);
            tractor->attach(master);
        }
    }
    if (depth == 0) {
        Mlt::Filter meter(*m_profile, "bc_meter");
        if (meter.is_valid()) {
            meter.set("meter", (m_meterPrefix + "master").constData());
            tractor->attach(meter);
        }
    }
    return tractor;
}

// A nested sequence as a producer. Picture and sound get separate graphs
// (like media files), so the two tracks never share one position.
Mlt::Producer *TimelinePlayer::nestProducer(const Project &project, const QUuid &nestId, bool audio, int depth, int angle)
{
    if (depth >= kMaxNestDepth)
        return nullptr; // a sequence inside itself: shown as a gap
    const NestedSequence *n = project.nest(nestId);
    if (!n)
        return nullptr;
    QString key = nestId.toString() + (audio ? QStringLiteral(":a") : QStringLiteral(":v"));
    if (n->multicam)
        key += QStringLiteral(":%1").arg(angle);
    if (auto it = m_nestCache.find(key); it != m_nestCache.end())
        return it->get();
    // Long enough for every use of it, even past its end.
    qint64 length = n->timeline.duration();
    for (const Timeline *tl : const_cast<Project &>(project).allTimelines())
        for (const TimelineTrack &t : tl->tracks)
            for (const TimelineClip &c : t.clips)
                if (c.nestId == nestId)
                    length = std::max(length, c.in + c.length + c.tail.length + 250); // room for transition frames
    // A multicamera angle: only that angle's track of the needed kind.
    Timeline angleTimeline;
    const Timeline *tl = &n->timeline;
    if (n->multicam) {
        angleTimeline = n->timeline;
        int index[2] = {0, 0};
        for (TimelineTrack &t : angleTimeline.tracks) {
            const bool isAudio = t.kind == Timeline::Kind::Audio;
            const bool keep = isAudio == audio && index[isAudio] == angle;
            ++index[isAudio];
            t.solo = false;
            if (!keep)
                t.clips.clear(); // other angles (and the other kind) are not decoded at all
        }
        tl = &angleTimeline;
    }
    auto tractor = buildTractor(project, *tl, length, depth + 1);
    if (!tractor)
        return nullptr;
    std::shared_ptr<Mlt::Producer> p(tractor.release());
    m_nestCache.insert(key, p);
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
        m_nestCache.clear();
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
    m_nestCache.clear();
    auto tractor = buildTractor(project, tl, std::max<qint64>(1, m_duration), 0);
    if (!tractor)
        return;
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
    return renderFrameLocked(frame);
}

QImage TimelinePlayer::renderFrameLocked(qint64 frame)
{
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

bool TimelinePlayer::renderAudio(qint64 from, qint64 to,
                                 const std::function<bool(const float *samples, int count)> &sink)
{
    if (m_playing)
        return false;
    QMutexLocker lock(&m_mutex);
    if (!m_tractor || from >= to)
        return false;
    const int freq = m_seq.sampleRate > 0 ? m_seq.sampleRate : 48000;
    const double fps = m_profile->fps();
    m_tractor->seek(int(from));
    for (qint64 frame = from; frame < to; ++frame) {
        // Frames come one after another (no seek), so sound effects keep their state.
        std::unique_ptr<Mlt::Frame> f(m_tractor->get_frame());
        if (!f)
            return false;
        mlt_audio_format fmt = mlt_audio_f32le;
        int channels = 2;
        int rate = freq;
        int samples = mlt_audio_calculate_frame_samples(float(fps), freq, frame);
        auto *pcm = static_cast<float *>(f->get_audio(fmt, rate, channels, samples));
        if (!pcm || fmt != mlt_audio_f32le || channels != 2 || samples <= 0)
            return false;
        if (!sink(pcm, samples))
            return false;
    }
    return true;
}

bool TimelinePlayer::play(qint64 frame, double speed)
{
    if (speed == 0)
        return false;
    if (m_playing) {
        setSpeed(speed);
        return true;
    }
    if (speed < 0)
        return startReverse(frame, speed);
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
        m_tractor->set_speed(speed);
        if (consumer->start() != 0) {
            m_showEvent.reset();
            m_tractor->set_speed(0);
            return false;
        }
        m_consumer = std::move(consumer);
        m_speed = speed;
        m_lastShown = frame;
        m_playing = true;
    }
    emit playingChanged(true);
    return true;
}

// Backwards: MLT's real-time consumer jumps when it starts in reverse, so
// frames are rendered one by one on a worker thread instead, as fast as the
// computer allows, and skipped to keep time. There is no sound backwards.
bool TimelinePlayer::startReverse(qint64 frame, double speed)
{
    {
        QMutexLocker lock(&m_mutex);
        if (!m_tractor)
            return false;
    }
    m_reverse = true;
    m_speed = speed;
    m_revOrigin = std::clamp<qint64>(frame, 0, std::max<qint64>(0, m_duration - 1));
    m_revShown = m_revOrigin;
    m_revClock.start();
    m_playing = true;
    ++m_revGen;
    emit playingChanged(true);
    reverseStep();
    return true;
}

void TimelinePlayer::reverseStep()
{
    if (!m_playing || !m_reverse)
        return;
    const double fps = std::max(1.0, m_seq.fps());
    const qint64 target = std::max<qint64>(
        0, m_revOrigin + qint64(std::llround(m_speed * fps * m_revClock.elapsed() / 1000.0)));
    const quint64 gen = m_revGen;
    m_revPool.start([this, target, gen] {
        QImage img = renderFrameLocked(target);
        QMetaObject::invokeMethod(this, [this, img, target, gen] {
            if (!m_playing || !m_reverse || gen != m_revGen)
                return;
            m_revShown = target;
            emit frameShown(img, target);
            if (target <= 0) {
                pause();
                emit reachedEnd();
                return;
            }
            // Next frame when its time comes (at once if rendering is slow).
            const double fps = std::max(1.0, m_seq.fps());
            const qint64 dueMs = qint64((m_revOrigin - target + 1) * 1000.0 / (fps * -m_speed));
            QTimer::singleShot(int(std::max<qint64>(0, dueMs - m_revClock.elapsed())), this,
                               [this, gen] { if (gen == m_revGen) reverseStep(); });
        }, Qt::QueuedConnection);
    });
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

void TimelinePlayer::setSpeed(double speed)
{
    if (!m_playing || speed == 0 || speed == m_speed)
        return;
    if ((speed < 0) != (m_speed < 0)) {
        // Direction change: continue from the frame on screen.
        const qint64 from = m_reverse ? m_revShown : m_lastShown;
        pause();
        play(from, speed);
        return;
    }
    if (m_reverse) {
        m_revOrigin = m_revShown;
        m_revClock.restart();
        m_speed = speed;
        return;
    }
    QMutexLocker lock(&m_mutex);
    if (m_tractor)
        m_tractor->set_speed(speed);
    m_speed = speed;
}

void TimelinePlayer::pause()
{
    if (!m_playing)
        return;
    if (m_reverse) {
        m_reverse = false;
        m_playing = false;
        ++m_revGen;
        m_revPool.waitForDone(); // the graph may change right after a pause
        emit playingChanged(false);
        return;
    }
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
        if (!m_playing || m_reverse)
            return;
        m_lastShown = frame;
        emit frameShown(image, frame);
        if (frame >= m_duration - 1) {
            pause();
            emit reachedEnd();
        }
    }, Qt::QueuedConnection);
}

} // namespace bc
