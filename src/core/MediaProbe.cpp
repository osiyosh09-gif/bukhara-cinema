#include "core/MediaProbe.h"

#include <QFileInfo>
#include <QImageReader>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <memory>

namespace bc::MediaProbe {

namespace {

struct FormatCloser { void operator()(AVFormatContext *c) const { avformat_close_input(&c); } };
struct CodecCloser  { void operator()(AVCodecContext *c) const { avcodec_free_context(&c); } };
struct FrameCloser  { void operator()(AVFrame *f) const { av_frame_free(&f); } };
struct PacketCloser { void operator()(AVPacket *p) const { av_packet_free(&p); } };
struct SwsCloser    { void operator()(SwsContext *s) const { sws_freeContext(s); } };

using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;
using CodecPtr  = std::unique_ptr<AVCodecContext, CodecCloser>;
using FramePtr  = std::unique_ptr<AVFrame, FrameCloser>;
using PacketPtr = std::unique_ptr<AVPacket, PacketCloser>;
using SwsPtr    = std::unique_ptr<SwsContext, SwsCloser>;

QString averr(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

FormatPtr openInput(const QString &path, QString *error)
{
    AVFormatContext *ctx = nullptr;
    // FFmpeg expects UTF-8 file names on Windows as well.
    int rc = avformat_open_input(&ctx, QFileInfo(path).absoluteFilePath().toUtf8().constData(), nullptr, nullptr);
    if (rc < 0) {
        if (error)
            *error = averr(rc);
        return nullptr;
    }
    FormatPtr fmt(ctx);
    rc = avformat_find_stream_info(ctx, nullptr);
    if (rc < 0) {
        if (error)
            *error = averr(rc);
        return nullptr;
    }
    return fmt;
}

bool isImageExt(const QString &path)
{
    return imageExtensions().contains(QFileInfo(path).suffix().toLower());
}

QString codecName(AVCodecID id)
{
    const AVCodecDescriptor *d = avcodec_descriptor_get(id);
    return d ? QString::fromLatin1(d->name) : QString();
}

} // namespace

QStringList videoExtensions()
{
    return {QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("avi"),
            QStringLiteral("m4v"), QStringLiteral("mts"), QStringLiteral("m2ts"), QStringLiteral("mxf"),
            QStringLiteral("webm"), QStringLiteral("wmv"), QStringLiteral("flv"), QStringLiteral("3gp"),
            QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ts")};
}

QStringList audioExtensions()
{
    return {QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("aac"), QStringLiteral("m4a"),
            QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("opus"), QStringLiteral("wma"),
            QStringLiteral("aif"), QStringLiteral("aiff")};
}

QStringList imageExtensions()
{
    return {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("bmp"),
            QStringLiteral("gif"), QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("webp")};
}

bool isSupportedFile(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return videoExtensions().contains(ext) || audioExtensions().contains(ext) || imageExtensions().contains(ext);
}

bool probe(const QString &path, MediaInfo &out, QString *error)
{
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        if (error)
            *error = QStringLiteral("file not found");
        return false;
    }
    MediaInfo info;
    info.fileSize = fi.size();
    info.fileModified = fi.lastModified();

    if (isImageExt(path)) {
        QImageReader reader(path);
        const QSize s = reader.size();
        if (!s.isValid() && !reader.canRead()) {
            if (error)
                *error = reader.errorString();
            return false;
        }
        info.kind = MediaInfo::Kind::Image;
        info.width = s.width();
        info.height = s.height();
        info.videoCodec = QString::fromLatin1(reader.format());
        out = info;
        return true;
    }

    FormatPtr fmt = openInput(path, error);
    if (!fmt)
        return false;

    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
        info.durationUs = fmt->duration; // AV_TIME_BASE is microseconds

    const int v = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int a = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    bool attachedPicOnly = false;
    if (v >= 0) {
        AVStream *st = fmt->streams[v];
        attachedPicOnly = (st->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0; // e.g. MP3 cover art
        if (!attachedPicOnly) {
            info.width = st->codecpar->width;
            info.height = st->codecpar->height;
            info.videoCodec = codecName(st->codecpar->codec_id);
            AVRational r = av_guess_frame_rate(fmt.get(), st, nullptr);
            if (r.num > 0 && r.den > 0) {
                av_reduce(&r.num, &r.den, r.num, r.den, 1000000);
                info.fpsNum = r.num;
                info.fpsDen = r.den;
            }
            if (info.durationUs == 0 && st->duration != AV_NOPTS_VALUE)
                info.durationUs = av_rescale_q(st->duration, st->time_base, AVRational{1, 1000000});
        }
    }
    if (a >= 0) {
        AVStream *st = fmt->streams[a];
        info.audioCodec = codecName(st->codecpar->codec_id);
        info.sampleRate = st->codecpar->sample_rate;
        info.channels = st->codecpar->ch_layout.nb_channels;
    }

    if (info.hasVideo())
        info.kind = MediaInfo::Kind::Video;
    else if (info.hasAudio())
        info.kind = MediaInfo::Kind::Audio;
    else {
        if (error)
            *error = QStringLiteral("no video or audio stream");
        return false;
    }
    out = info;
    return true;
}

namespace {

// Decodes one video frame. targetUs < 0: about 10% into the file, first frame
// after the seek point (fast). Otherwise the frame shown at targetUs (exact).
QImage decodeVideoFrame(const QString &path, qint64 targetUs, int maxWidth, int maxHeight, QString *error)
{
    FormatPtr fmt = openInput(path, error);
    if (!fmt)
        return {};
    const AVCodec *dec = nullptr;
    const int vi = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (vi < 0 || !dec)
        return {}; // audio only
    AVStream *st = fmt->streams[vi];
    if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
        return {};

    CodecPtr cc(avcodec_alloc_context3(dec));
    if (!cc || avcodec_parameters_to_context(cc.get(), st->codecpar) < 0)
        return {};
    cc->thread_count = 2; // modest: keep the UI machine responsive
    if (avcodec_open2(cc.get(), dec, nullptr) < 0) {
        if (error)
            *error = QStringLiteral("cannot open decoder");
        return {};
    }

    const bool exact = targetUs >= 0;
    int64_t streamTarget = 0;
    if (exact) {
        // Seek to the key frame before the target, then decode up to it.
        const int64_t startUs = fmt->start_time != AV_NOPTS_VALUE ? fmt->start_time : 0;
        streamTarget = av_rescale_q(startUs + targetUs, AV_TIME_BASE_Q, st->time_base);
        av_seek_frame(fmt.get(), vi, streamTarget, AVSEEK_FLAG_BACKWARD);
        // Allow half a frame of rounding.
        if (st->avg_frame_rate.num > 0)
            streamTarget -= av_rescale_q(1, av_inv_q(st->avg_frame_rate), st->time_base) / 2;
    } else if (fmt->duration > 0) {
        // Seek to ~10% so the thumbnail is not a black first frame.
        const int64_t target = fmt->duration / 10;
        av_seek_frame(fmt.get(), -1, target, AVSEEK_FLAG_BACKWARD);
    }

    PacketPtr pkt(av_packet_alloc());
    FramePtr frame(av_frame_alloc());
    FramePtr last(av_frame_alloc());
    bool got = false;     // a frame that satisfies the request
    bool haveLast = false; // newest decoded frame (fallback near the end)
    auto accept = [&]() {
        av_frame_unref(last.get());
        av_frame_ref(last.get(), frame.get());
        haveLast = true;
        if (!exact)
            return true;
        const int64_t pts = frame->best_effort_timestamp;
        return pts == AV_NOPTS_VALUE || pts >= streamTarget;
    };
    int packets = 0;
    while (!got && av_read_frame(fmt.get(), pkt.get()) >= 0 && packets < 2000) {
        if (pkt->stream_index == vi) {
            ++packets;
            if (avcodec_send_packet(cc.get(), pkt.get()) >= 0)
                while (!got && avcodec_receive_frame(cc.get(), frame.get()) >= 0)
                    got = accept();
        }
        av_packet_unref(pkt.get());
    }
    if (!got) {
        avcodec_send_packet(cc.get(), nullptr); // flush
        while (!got && avcodec_receive_frame(cc.get(), frame.get()) >= 0)
            got = accept();
    }
    if (!got && haveLast) {
        av_frame_unref(frame.get());
        av_frame_ref(frame.get(), last.get());
        got = true;
    }
    if (!got) {
        if (error)
            *error = QStringLiteral("no frame decoded");
        return {};
    }

    QSize out(frame->width, frame->height);
    // Respect non-square pixels.
    if (frame->sample_aspect_ratio.num > 0 && frame->sample_aspect_ratio.den > 0)
        out.setWidth(int(out.width() * av_q2d(frame->sample_aspect_ratio)));
    out.scale(maxWidth, maxHeight, Qt::KeepAspectRatio);
    out = out.expandedTo(QSize(2, 2));

    SwsPtr sws(sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format), out.width(), out.height(),
                              AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!sws)
        return {};
    QImage img(out, QImage::Format_RGB888);
    uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    int dstStride[4] = {int(img.bytesPerLine()), 0, 0, 0};
    sws_scale(sws.get(), frame->data, frame->linesize, 0, frame->height, dst, dstStride);
    return img;
}

} // namespace

QImage thumbnail(const QString &path, int maxWidth, int maxHeight, QString *error)
{
    if (isImageExt(path)) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QSize s = reader.size();
        if (s.isValid()) {
            s.scale(maxWidth, maxHeight, Qt::KeepAspectRatio);
            reader.setScaledSize(s);
        }
        QImage img = reader.read();
        if (img.isNull() && error)
            *error = reader.errorString();
        return img;
    }

    return decodeVideoFrame(path, -1, maxWidth, maxHeight, error);
}

QImage frameAt(const QString &path, qint64 timeUs, int maxWidth, int maxHeight, QString *error)
{
    if (isImageExt(path))
        return thumbnail(path, maxWidth, maxHeight, error);
    return decodeVideoFrame(path, std::max<qint64>(0, timeUs), maxWidth, maxHeight, error);
}

QString ffmpegVersionString()
{
    return QStringLiteral("FFmpeg %1 (avformat %2.%3.%4)")
        .arg(QString::fromLatin1(av_version_info()))
        .arg(LIBAVFORMAT_VERSION_MAJOR)
        .arg(LIBAVFORMAT_VERSION_MINOR)
        .arg(LIBAVFORMAT_VERSION_MICRO);
}

QString ffmpegLicense()
{
    return QString::fromLatin1(avformat_license());
}

} // namespace bc::MediaProbe
