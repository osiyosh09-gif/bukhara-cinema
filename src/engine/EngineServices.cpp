#include "engine/EngineServices.h"

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
    const double pos = double(mlt_filter_get_position(filter, frame));
    const double length = double(mlt_filter_get_length2(filter, frame));
    const double g0 = gain * envelope(pos, length, fadeIn, fadeOut);
    const double g1 = gain * envelope(pos + 1, length, fadeIn, fadeOut);
    const int n = *samples;
    const int ch = *channels;
    if (*format == mlt_audio_f32le) {
        auto *s = static_cast<float *>(*buffer);
        for (int i = 0; i < n; ++i) {
            const float g = float(g0 + (g1 - g0) * i / n);
            for (int c = 0; c < ch; ++c)
                s[i * ch + c] *= g;
        }
    } else if (*format == mlt_audio_s16) {
        auto *s = static_cast<int16_t *>(*buffer);
        for (int i = 0; i < n; ++i) {
            const double g = g0 + (g1 - g0) * i / n;
            for (int c = 0; c < ch; ++c)
                s[i * ch + c] = int16_t(std::clamp(std::lround(s[i * ch + c] * g), -32768L, 32767L));
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

} // namespace

void registerAll(mlt_repository_s *repository)
{
    if (!repository)
        return;
    mlt_repository_register(repository, mlt_service_producer_type, "bc_still", stillInit);
    mlt_repository_register(repository, mlt_service_filter_type, "bc_gain", gainInit);
}

QByteArray stillResource(const QImage &image)
{
    QMutexLocker lock(&s_pendingMutex);
    const QByteArray key = "bc_still:" + QByteArray::number(++s_counter);
    s_pending.insert(key.mid(9), image);
    return key;
}

} // namespace bc::EngineServices
