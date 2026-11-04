#include "engine/EngineServices.h"

#include "core/AudioFx.h"
#include "core/Color.h"
#include "core/Effects.h"
#include "core/Motion.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>

#include <framework/mlt.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

namespace bc::EngineServices {

namespace {

QMutex s_pendingMutex;
QHash<QByteArray, QImage> s_pending;
quint64 s_counter = 0;

// ---------------------------------------------------------------------------
// bc_still producer

struct Still
{
    QImage source; // RGBA8888, straight alpha
    QMutex mutex;  // guards the scaled copy (frames may be rendered in parallel)
    QImage scaled;
};

QImage fitted(const QImage &src, int w, int h)
{
    QImage out(w, h, QImage::Format_RGBA8888_Premultiplied);
    out.fill(Qt::transparent);
    if (!src.isNull()) {
        const QSize s = src.size().scaled(w, h, Qt::KeepAspectRatio);
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRect((w - s.width()) / 2, (h - s.height()) / 2, s.width(), s.height()),
                    src.convertToFormat(QImage::Format_RGBA8888_Premultiplied));
    }
    return out.convertToFormat(QImage::Format_RGBA8888);
}

int stillGetImage(mlt_frame frame, uint8_t **buffer, mlt_image_format *format, int *width, int *height,
                  int /*writable*/)
{
    auto *producer = static_cast<mlt_producer>(mlt_frame_pop_service(frame));
    auto *still = static_cast<Still *>(producer->child);
    mlt_profile profile = mlt_service_profile(MLT_PRODUCER_SERVICE(producer));
    int w = *width > 0 ? *width : (profile ? profile->width : still->source.width());
    int h = *height > 0 ? *height : (profile ? profile->height : still->source.height());
    w = std::max(2, w);
    h = std::max(2, h);

    QImage img;
    {
        QMutexLocker lock(&still->mutex);
        if (still->scaled.width() != w || still->scaled.height() != h)
            still->scaled = still->source.size() == QSize(w, h) ? still->source : fitted(still->source, w, h);
        img = still->scaled;
    }
    const int size = w * h * 4;
    auto *out = static_cast<uint8_t *>(mlt_pool_alloc(size));
    for (int y = 0; y < h; ++y)
        std::memcpy(out + y * w * 4, img.constScanLine(y), size_t(w) * 4);
    mlt_frame_set_image(frame, out, size, mlt_pool_release);
    mlt_properties props = MLT_FRAME_PROPERTIES(frame);
    mlt_properties_set_int(props, "width", w);
    mlt_properties_set_int(props, "height", h);
    mlt_properties_set_int(props, "format", mlt_image_rgba);
    *buffer = out;
    *format = mlt_image_rgba;
    *width = w;
    *height = h;
    return 0;
}

int stillGetFrame(mlt_producer producer, mlt_frame_ptr frame, int /*index*/)
{
    *frame = mlt_frame_init(MLT_PRODUCER_SERVICE(producer));
    if (!*frame)
        return 1;
    mlt_frame_set_position(*frame, mlt_producer_position(producer));
    mlt_properties props = MLT_FRAME_PROPERTIES(*frame);
    mlt_profile profile = mlt_service_profile(MLT_PRODUCER_SERVICE(producer));
    mlt_properties_set_int(props, "progressive", 1);
    mlt_properties_set_double(props, "aspect_ratio", profile ? mlt_profile_sar(profile) : 1.0);
    if (profile) {
        // Already the frame size: the scalers leave it alone.
        mlt_properties_set_int(props, "meta.media.width", profile->width);
        mlt_properties_set_int(props, "meta.media.height", profile->height);
    }
    mlt_frame_push_service(*frame, producer);
    mlt_frame_push_get_image(*frame, stillGetImage);
    mlt_producer_prepare_next(producer);
    return 0;
}

void stillClose(mlt_producer producer)
{
    delete static_cast<Still *>(producer->child);
    producer->child = nullptr;
    producer->close = nullptr;
    mlt_producer_close(producer);
    free(producer);
}

void *stillInit(mlt_profile profile, mlt_service_type, const char *, const void *arg)
{
    const QByteArray key(arg ? static_cast<const char *>(arg) : "");
    QImage image;
    {
        QMutexLocker lock(&s_pendingMutex);
        image = s_pending.take(key);
    }
    if (image.isNull())
        return nullptr;
    auto *producer = static_cast<mlt_producer>(calloc(1, sizeof(struct mlt_producer_s)));
    if (!producer || mlt_producer_init(producer, nullptr) != 0) {
        free(producer);
        return nullptr;
    }
    auto *still = new Still;
    still->source = image.convertToFormat(QImage::Format_RGBA8888);
    producer->child = still;
    producer->get_frame = stillGetFrame;
    producer->close = reinterpret_cast<mlt_destructor>(stillClose);
    mlt_properties props = MLT_PRODUCER_PROPERTIES(producer);
    mlt_properties_set(props, "resource", key.constData());
    mlt_properties_set(props, "mlt_service", "bc_still");
    mlt_properties_set_int(props, "length", INT_MAX);
    mlt_properties_set_int(props, "out", INT_MAX - 1);
    mlt_properties_set_int(props, "seekable", 1);
    mlt_properties_set_int(props, "meta.media.width", still->source.width());
    mlt_properties_set_int(props, "meta.media.height", still->source.height());
    mlt_properties_set_int(props, "meta.media.progressive", 1);
    (void)profile;
    return producer;
}

// ---------------------------------------------------------------------------
// bc_gain filter

double envelope(double pos, double length, double fadeIn, double fadeOut)
{
    double g = 1.0;
    if (fadeIn > 0)
        g *= std::clamp(pos / fadeIn, 0.0, 1.0);
    if (fadeOut > 0)
        g *= std::clamp((length - pos) / fadeOut, 0.0, 1.0);
    return g;
}

int gainGetAudio(mlt_frame frame, void **buffer, mlt_audio_format *format, int *frequency, int *channels,
                 int *samples)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_audio(frame));
    *format = mlt_audio_f32le;
    const int err = mlt_frame_get_audio(frame, buffer, format, frequency, channels, samples);
    if (err || !*buffer || *samples <= 0 || *channels <= 0)
        return err;
    mlt_properties props = MLT_FILTER_PROPERTIES(filter);
    const double gain = std::pow(10.0, mlt_properties_get_double(props, "gain_db") / 20.0);
    const double fadeIn = mlt_properties_get_double(props, "fade_in");
    const double fadeOut = mlt_properties_get_double(props, "fade_out");
    // `lead`: frames of the cut before the clip itself (handles of a crossfade).
    const double pos = double(mlt_filter_get_position(filter, frame)) - mlt_properties_get_double(props, "lead");
    const double length = mlt_properties_get(props, "length") ? mlt_properties_get_double(props, "length")
                                                              : double(mlt_filter_get_length2(filter, frame));
    const double g0 = gain * envelope(pos, length, fadeIn, fadeOut);
    const double g1 = gain * envelope(pos + 1, length, fadeIn, fadeOut);
    const int n = *samples;
    const int ch = *channels;
    // Balance (track pan): the other side gets quieter, the centre stays at full volume.
    const double pan = std::clamp(mlt_properties_get_double(props, "pan"), -1.0, 1.0);
    double side[8];
    for (int c = 0; c < 8; ++c)
        side[c] = 1.0;
    if (ch >= 2) {
        side[0] = pan > 0 ? 1 - pan : 1;
        side[1] = pan < 0 ? 1 + pan : 1;
    }
    if (*format == mlt_audio_f32le) {
        auto *s = static_cast<float *>(*buffer);
        for (int i = 0; i < n; ++i) {
            const double g = g0 + (g1 - g0) * i / n;
            for (int c = 0; c < ch; ++c)
                s[i * ch + c] *= float(g * side[std::min(c, 7)]);
        }
    } else if (*format == mlt_audio_s16) {
        auto *s = static_cast<int16_t *>(*buffer);
        for (int i = 0; i < n; ++i) {
            const double g = g0 + (g1 - g0) * i / n;
            for (int c = 0; c < ch; ++c)
                s[i * ch + c] = int16_t(
                    std::clamp(std::lround(s[i * ch + c] * g * side[std::min(c, 7)]), -32768L, 32767L));
        }
    }
    return 0;
}

mlt_frame gainProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_audio(frame, filter);
    mlt_frame_push_audio(frame, reinterpret_cast<void *>(gainGetAudio));
    return frame;
}

void *gainInit(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_filter filter = mlt_filter_new();
    if (!filter)
        return nullptr;
    filter->process = gainProcess;
    mlt_properties_set_double(MLT_FILTER_PROPERTIES(filter), "gain_db", 0.0);
    return filter;
}

// ---------------------------------------------------------------------------
// bc_audiofx filter: sound effects of an audio clip (equalizer, compressor).
// The filter keeps the effect state between frames; a jump resets it.

struct AudioFxState
{
    AudioChain chain;
    QByteArray json;
    ClipEffects effects;    // parsed `json`
    mlt_position next = -1; // position expected next
};

int audioFxGetAudio(mlt_frame frame, void **buffer, mlt_audio_format *format, int *frequency, int *channels,
                    int *samples)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_audio(frame));
    *format = mlt_audio_f32le;
    const int err = mlt_frame_get_audio(frame, buffer, format, frequency, channels, samples);
    if (err || !*buffer || *samples <= 0 || *channels <= 0 || *format != mlt_audio_f32le)
        return err;
    mlt_properties props = MLT_FILTER_PROPERTIES(filter);
    auto *state = static_cast<AudioFxState *>(mlt_properties_get_data(props, "_state", nullptr));
    if (!state)
        return 0;
    mlt_service_lock(MLT_FILTER_SERVICE(filter));
    const char *json = mlt_properties_get(props, "effects");
    const QByteArray text = json ? QByteArray(json) : QByteArray();
    if (text != state->json) {
        state->json = text;
        state->effects = effectsFromEngineString(text);
    }
    state->chain.configure(state->effects, *frequency, *channels);
    const mlt_position at = mlt_frame_get_position(frame);
    if (at != state->next)
        state->chain.reset();
    state->next = at + 1;
    state->chain.process(static_cast<float *>(*buffer), *samples);
    mlt_service_unlock(MLT_FILTER_SERVICE(filter));
    return 0;
}

mlt_frame audioFxProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_audio(frame, filter);
    mlt_frame_push_audio(frame, reinterpret_cast<void *>(audioFxGetAudio));
    return frame;
}

void *audioFxInit(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_filter filter = mlt_filter_new();
    if (!filter)
        return nullptr;
    filter->process = audioFxProcess;
    mlt_properties_set_data(MLT_FILTER_PROPERTIES(filter), "_state", new AudioFxState, 0,
                            [](void *p) { delete static_cast<AudioFxState *>(p); }, nullptr);
    return filter;
}

// ---------------------------------------------------------------------------
// bc_meter filter: remembers the peak of each channel for the mixer meters.

struct MeterState
{
    float peak[2] = {0, 0};
    qint64 serial = 0; // how many frames were measured
};

QMutex s_meterMutex;
QHash<QByteArray, MeterState> s_meters;

int meterGetAudio(mlt_frame frame, void **buffer, mlt_audio_format *format, int *frequency, int *channels,
                  int *samples)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_audio(frame));
    *format = mlt_audio_f32le;
    const int err = mlt_frame_get_audio(frame, buffer, format, frequency, channels, samples);
    if (err || !*buffer || *samples <= 0 || *channels <= 0)
        return err;
    float peak[2] = {0, 0};
    const int n = *samples;
    const int ch = *channels;
    if (*format == mlt_audio_f32le) {
        const auto *s = static_cast<const float *>(*buffer);
        for (int i = 0; i < n * ch; ++i) {
            float &p = peak[std::min(i % ch, 1)];
            p = std::max(p, std::abs(s[i]));
        }
    } else if (*format == mlt_audio_s16) {
        const auto *s = static_cast<const int16_t *>(*buffer);
        for (int i = 0; i < n * ch; ++i) {
            float &p = peak[std::min(i % ch, 1)];
            p = std::max(p, std::abs(s[i]) / 32768.0f);
        }
    }
    if (ch == 1)
        peak[1] = peak[0];
    const char *key = mlt_properties_get(MLT_FILTER_PROPERTIES(filter), "meter");
    if (key && *key) {
        QMutexLocker lock(&s_meterMutex);
        MeterState &m = s_meters[QByteArray(key)];
        m.peak[0] = peak[0];
        m.peak[1] = peak[1];
        ++m.serial;
    }
    return 0;
}

mlt_frame meterProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_audio(frame, filter);
    mlt_frame_push_audio(frame, reinterpret_cast<void *>(meterGetAudio));
    return frame;
}

void *meterInit(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_filter filter = mlt_filter_new();
    if (!filter)
        return nullptr;
    filter->process = meterProcess;
    return filter;
}

// The picture inside the letterbox bars of a w x h frame (aspect <= 0: all of it).
QRectF contentRect(int w, int h, double aspect)
{
    QRectF content(0, 0, w, h);
    if (aspect > 0) {
        const double frameAspect = double(w) / h;
        if (aspect > frameAspect + 1e-3) {
            const double ch = w / aspect;
            content = QRectF(0, (h - ch) / 2, w, ch);
        } else if (aspect < frameAspect - 1e-3) {
            const double cw = h * aspect;
            content = QRectF((w - cw) / 2, 0, cw, h);
        }
    }
    return content;
}

// ---------------------------------------------------------------------------
// bc_transform filter: position, scale, rotation and opacity of a clip's
// picture (keyframes are evaluated per frame). Drawn by Qt, so it needs no
// GPL module.

int transformGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height,
                      int /*writable*/)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_service(frame));
    mlt_properties props = MLT_FILTER_PROPERTIES(filter);
    // Keys are in source frames; a speed-changed clip uses `speed` of them per frame.
    double speed = mlt_properties_get_double(props, "speed");
    if (speed <= 0)
        speed = 1.0;
    const qint64 pos = qint64(std::llround(double(mlt_filter_get_position(filter, frame)) * speed));
    *format = mlt_image_rgba;
    const int err = mlt_frame_get_image(frame, image, format, width, height, 1);
    if (err || !*image || *format != mlt_image_rgba || *width <= 0 || *height <= 0)
        return err;
    const char *motionText = mlt_properties_get(props, "motion");
    const ClipTransform t = ClipMotion::fromEngineString(QByteArray(motionText ? motionText : "")).at(pos);
    if (t.isIdentity())
        return 0;
    const int w = *width;
    const int h = *height;
    // Video is letterboxed to the frame by the loader; only the picture
    // itself moves (the bars are left out).
    const QRectF content = contentRect(w, h, mlt_properties_get_double(props, "source_aspect"));
    const QImage src = QImage(*image, w, h, QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    QImage out(w, h, QImage::Format_RGBA8888_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(std::clamp(t.opacity / 100.0, 0.0, 1.0));
        p.translate(w / 2.0 + t.x * w, h / 2.0 + t.y * h);
        p.rotate(t.rotation);
        const double s = std::max(0.0, t.scale / 100.0);
        p.scale(s, s);
        p.translate(-w / 2.0, -h / 2.0);
        p.drawImage(content, src, content);
    }
    const QImage straight = out.convertToFormat(QImage::Format_RGBA8888);
    const int size = w * h * 4;
    auto *buf = static_cast<uint8_t *>(mlt_pool_alloc(size));
    for (int y = 0; y < h; ++y)
        std::memcpy(buf + y * w * 4, straight.constScanLine(y), size_t(w) * 4);
    mlt_frame_set_image(frame, buf, size, mlt_pool_release);
    *image = buf;
    return 0;
}

mlt_frame transformProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_service(frame, filter);
    mlt_frame_push_get_image(frame, transformGetImage);
    return frame;
}

void *transformInit(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_filter filter = mlt_filter_new();
    if (!filter)
        return nullptr;
    filter->process = transformProcess;
    return filter;
}

// ---------------------------------------------------------------------------
// bc_effects filter (phase 8b): the picture effects of a clip (core/Effects),
// applied before bc_transform. Pure pixel work, no GPL module.

int effectsGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height,
                    int /*writable*/)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_service(frame));
    mlt_properties props = MLT_FILTER_PROPERTIES(filter);
    *format = mlt_image_rgba;
    const int err = mlt_frame_get_image(frame, image, format, width, height, 1);
    if (err || !*image || *format != mlt_image_rgba || *width <= 0 || *height <= 0)
        return err;
    // Colour correction first, then the effects in their order.
    const char *colorText = mlt_properties_get(props, "color");
    if (colorText && *colorText)
        applyColor(*image, size_t(*width) * *height, ClipColor::fromEngineString(QByteArray(colorText)));
    const char *text = mlt_properties_get(props, "effects");
    const ClipEffects effects = effectsFromEngineString(QByteArray(text ? text : ""));
    if (effects.isEmpty())
        return 0;
    applyEffects(*image, *width, *height, effects,
                 contentRect(*width, *height, mlt_properties_get_double(props, "source_aspect")));
    return 0;
}

mlt_frame effectsProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_service(frame, filter);
    mlt_frame_push_get_image(frame, effectsGetImage);
    return frame;
}

void *effectsInit(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_filter filter = mlt_filter_new();
    if (!filter)
        return nullptr;
    filter->process = effectsProcess;
    return filter;
}

// ---------------------------------------------------------------------------
// Transitions (phase 8a): bc_fade filter (a free end of a clip fades in or
// out) and bc_xfade transition (a cut between two clips). Kinds follow
// ClipTransition::Kind: 1 dissolve, 2 dip to black, 3 dip to white,
// 4 wipe from the left, 5 wipe from the right.

inline uint8_t mixByte(int a, int b, double t)
{
    return uint8_t(std::clamp(int(std::lround(a + (b - a) * t)), 0, 255));
}

// Shows the clip with visibility `p` (0 = gone, 1 = fully there).
void applyEdge(uint8_t *img, int w, int h, int kind, double p)
{
    p = std::clamp(p, 0.0, 1.0);
    if (p >= 1.0)
        return;
    const int edge = int(std::lround(p * w));
    for (int y = 0; y < h; ++y) {
        uint8_t *row = img + size_t(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            uint8_t *px = row + x * 4;
            switch (kind) {
            case 2:
            case 3: {
                const int c = kind == 2 ? 0 : 255;
                px[0] = mixByte(c, px[0], p);
                px[1] = mixByte(c, px[1], p);
                px[2] = mixByte(c, px[2], p);
                px[3] = mixByte(255, px[3], p);
                break;
            }
            case 4:
                if (x >= edge)
                    px[3] = 0;
                break;
            case 5:
                if (x < w - edge)
                    px[3] = 0;
                break;
            default:
                px[3] = uint8_t(std::lround(px[3] * p));
            }
        }
    }
}

int fadeGetImage(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int /*writable*/)
{
    auto filter = static_cast<mlt_filter>(mlt_frame_pop_service(frame));
    mlt_properties props = MLT_FILTER_PROPERTIES(filter);
    const double pos = double(mlt_filter_get_position(filter, frame)) - mlt_properties_get_double(props, "lead");
    *format = mlt_image_rgba;
    const int err = mlt_frame_get_image(frame, image, format, width, height, 1);
    if (err || !*image || *format != mlt_image_rgba || *width <= 0 || *height <= 0)
        return err;
    const double length = mlt_properties_get_double(props, "length");
    const double headLen = mlt_properties_get_double(props, "head_len");
    const double tailLen = mlt_properties_get_double(props, "tail_len");
    if (headLen > 0 && pos < headLen)
        applyEdge(*image, *width, *height, mlt_properties_get_int(props, "head_kind"), (pos + 0.5) / headLen);
    if (tailLen > 0 && pos >= length - tailLen)
        applyEdge(*image, *width, *height, mlt_properties_get_int(props, "tail_kind"), (length - pos - 0.5) / tailLen);
    return 0;
}

mlt_frame fadeProcess(mlt_filter filter, mlt_frame frame)
{
    mlt_frame_push_service(frame, filter);
    mlt_frame_push_get_image(frame, fadeGetImage);
    return frame;
}

void *fadeInit(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_filter filter = mlt_filter_new();
    if (!filter)
        return nullptr;
    filter->process = fadeProcess;
    return filter;
}

int xfadeGetImage(mlt_frame a, uint8_t **image, mlt_image_format *format, int *width, int *height, int /*writable*/)
{
    auto b = static_cast<mlt_frame>(mlt_frame_pop_frame(a));
    auto transition = static_cast<mlt_transition>(mlt_frame_pop_service(a));
    const double p = std::clamp(mlt_transition_get_progress(transition, a), 0.0, 1.0);
    const int kind = mlt_properties_get_int(MLT_TRANSITION_PROPERTIES(transition), "kind");
    *format = mlt_image_rgba;
    int err = mlt_frame_get_image(a, image, format, width, height, 1);
    if (err || !*image || *format != mlt_image_rgba || *width <= 0 || *height <= 0)
        return err;
    const int w = *width;
    const int h = *height;
    uint8_t *bImage = nullptr;
    mlt_image_format bFormat = mlt_image_rgba;
    int bw = w;
    int bh = h;
    if (!b || mlt_frame_get_image(b, &bImage, &bFormat, &bw, &bh, 0) || !bImage || bFormat != mlt_image_rgba
        || bw != w || bh != h) {
        return 0; // keep the first picture
    }
    const int edge = int(std::lround(p * w));
    for (int y = 0; y < h; ++y) {
        uint8_t *ra = *image + size_t(y) * w * 4;
        const uint8_t *rb = bImage + size_t(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            uint8_t *pa = ra + x * 4;
            const uint8_t *pb = rb + x * 4;
            switch (kind) {
            case 2:
            case 3: {
                const int c = kind == 2 ? 0 : 255;
                if (p < 0.5) {
                    const double t = p * 2;
                    for (int k = 0; k < 3; ++k)
                        pa[k] = mixByte(pa[k], c, t);
                    pa[3] = mixByte(pa[3], 255, t);
                } else {
                    const double t = p * 2 - 1;
                    for (int k = 0; k < 3; ++k)
                        pa[k] = mixByte(c, pb[k], t);
                    pa[3] = mixByte(255, pb[3], t);
                }
                break;
            }
            case 4:
                if (x < edge)
                    std::memcpy(pa, pb, 4);
                break;
            case 5:
                if (x >= w - edge)
                    std::memcpy(pa, pb, 4);
                break;
            default:
                for (int k = 0; k < 4; ++k)
                    pa[k] = mixByte(pa[k], pb[k], p);
            }
        }
    }
    return 0;
}

mlt_frame xfadeProcess(mlt_transition transition, mlt_frame a, mlt_frame b)
{
    mlt_frame_push_service(a, transition);
    mlt_frame_push_frame(a, b);
    mlt_frame_push_get_image(a, xfadeGetImage);
    return a;
}

void *xfadeInit(mlt_profile, mlt_service_type, const char *, const void *)
{
    mlt_transition transition = mlt_transition_new();
    if (!transition)
        return nullptr;
    transition->process = xfadeProcess;
    mlt_properties_set_int(MLT_TRANSITION_PROPERTIES(transition), "_transition_type", 1); // video only
    return transition;
}

} // namespace

void registerAll(mlt_repository_s *repository)
{
    if (!repository)
        return;
    mlt_repository_register(repository, mlt_service_producer_type, "bc_still", stillInit);
    mlt_repository_register(repository, mlt_service_filter_type, "bc_gain", gainInit);
    mlt_repository_register(repository, mlt_service_filter_type, "bc_meter", meterInit);
    mlt_repository_register(repository, mlt_service_filter_type, "bc_audiofx", audioFxInit);
    mlt_repository_register(repository, mlt_service_filter_type, "bc_transform", transformInit);
    mlt_repository_register(repository, mlt_service_filter_type, "bc_fade", fadeInit);
    mlt_repository_register(repository, mlt_service_filter_type, "bc_effects", effectsInit);
    mlt_repository_register(repository, mlt_service_transition_type, "bc_xfade", xfadeInit);
}

QByteArray stillResource(const QImage &image)
{
    QMutexLocker lock(&s_pendingMutex);
    const QByteArray key = "bc_still:" + QByteArray::number(++s_counter);
    s_pending.insert(key.mid(9), image);
    return key;
}

MeterLevel meterLevel(const QByteArray &key)
{
    QMutexLocker lock(&s_meterMutex);
    const auto it = s_meters.constFind(key);
    if (it == s_meters.constEnd())
        return {};
    return {it->peak[0], it->peak[1], it->serial};
}

void clearMeters(const QByteArray &prefix)
{
    QMutexLocker lock(&s_meterMutex);
    for (auto it = s_meters.begin(); it != s_meters.end();)
        it = it.key().startsWith(prefix) ? s_meters.erase(it) : std::next(it);
}

} // namespace bc::EngineServices
