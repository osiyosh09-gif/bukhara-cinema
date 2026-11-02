#include "core/HwAccel.h"

#include "core/MediaProbe.h"

#include <QSettings>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

#include <cstdlib>
#include <mutex>

namespace bc::HwAccel {

namespace {

AVHWDeviceType typeOf(const QString &name)
{
    return av_hwdevice_find_type_by_name(name.toLatin1().constData());
}

// get_format callback: the hardware format when the decoder offers it,
// otherwise the first software one (FFmpeg asks again without the hardware
// format when the card cannot decode this stream).
AVPixelFormat pickFormat(AVCodecContext *ctx, const AVPixelFormat *formats)
{
    const auto wanted = AVPixelFormat(reinterpret_cast<intptr_t>(ctx->opaque));
    for (const AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == wanted)
            return *p;
    for (const AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; ++p) {
        const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(*p);
        if (d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL))
            return *p;
    }
    return AV_PIX_FMT_NONE;
}

} // namespace

QString setting()
{
    const QString v = QSettings().value(QStringLiteral("gpu/decode"), QStringLiteral("auto")).toString();
    return v.isEmpty() ? QStringLiteral("auto") : v;
}

void setSetting(const QString &value)
{
    QSettings().setValue(QStringLiteral("gpu/decode"), value);
}

QStringList candidates()
{
#ifdef Q_OS_WIN
    return {QStringLiteral("d3d11va"), QStringLiteral("dxva2"), QStringLiteral("cuda")};
#elif defined(Q_OS_MACOS)
    return {QStringLiteral("videotoolbox")};
#else
    return {QStringLiteral("vaapi"), QStringLiteral("cuda")};
#endif
}

QStringList available()
{
    static QStringList cached;
    static std::once_flag once;
    std::call_once(once, [] {
        for (const QString &name : candidates()) {
            const AVHWDeviceType type = typeOf(name);
            if (type == AV_HWDEVICE_TYPE_NONE)
                continue; // FFmpeg built without it
            AVBufferRef *dev = nullptr;
            if (av_hwdevice_ctx_create(&dev, type, nullptr, nullptr, 0) >= 0) {
                cached << name;
                av_buffer_unref(&dev);
            }
        }
    });
    return cached;
}

QString decoder()
{
    const QString s = setting();
    if (s == QLatin1String("off"))
        return {};
    if (s == QLatin1String("auto")) {
        const QStringList a = available();
        return a.isEmpty() ? QString() : a.first();
    }
    return s; // forced: tried, and falls back when it does not work
}

void applyToEngine()
{
    const QString d = decoder();
    if (d.isEmpty())
        qunsetenv("MLT_AVFORMAT_HWACCEL");
    else
        qputenv("MLT_AVFORMAT_HWACCEL", d.toLatin1());
}

QStringList encoders()
{
    QStringList out;
    for (const MediaProbe::VideoEncoder &e : MediaProbe::usableVideoEncoders())
        if (e.isH264) // every H.264 encoder here runs on the video card or through Windows
            out << e.label;
    return out;
}

bool attach(AVCodecContext *ctx, const AVCodec *codec, const QString &type)
{
    const AVHWDeviceType t = typeOf(type);
    if (!ctx || !codec || t == AV_HWDEVICE_TYPE_NONE)
        return false;
    AVPixelFormat fmt = AV_PIX_FMT_NONE;
    for (int i = 0;; ++i) {
        const AVCodecHWConfig *c = avcodec_get_hw_config(codec, i);
        if (!c)
            break;
        if ((c->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && c->device_type == t) {
            fmt = c->pix_fmt;
            break;
        }
    }
    if (fmt == AV_PIX_FMT_NONE)
        return false;
    AVBufferRef *dev = nullptr;
    if (av_hwdevice_ctx_create(&dev, t, nullptr, nullptr, 0) < 0)
        return false;
    ctx->hw_device_ctx = dev; // the context owns the reference now
    ctx->opaque = reinterpret_cast<void *>(intptr_t(fmt));
    ctx->get_format = pickFormat;
    return true;
}

AVFrame *download(AVFrame *frame, AVFrame *sw)
{
    if (!frame || !frame->hw_frames_ctx)
        return frame;
    av_frame_unref(sw);
    if (av_hwframe_transfer_data(sw, frame, 0) < 0 || av_frame_copy_props(sw, frame) < 0)
        return nullptr;
    return sw;
}

} // namespace bc::HwAccel
