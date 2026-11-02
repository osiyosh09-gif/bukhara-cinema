#include "core/Proxy.h"

#include "core/HwAccel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <memory>

namespace bc::Proxy {

namespace {

struct InputCloser { void operator()(AVFormatContext *c) const { avformat_close_input(&c); } };
struct OutputCloser
{
    void operator()(AVFormatContext *c) const
    {
        if (c && c->pb && !(c->oformat->flags & AVFMT_NOFILE))
            avio_closep(&c->pb);
        avformat_free_context(c);
    }
};
struct CodecCloser { void operator()(AVCodecContext *c) const { avcodec_free_context(&c); } };
struct FrameCloser { void operator()(AVFrame *f) const { av_frame_free(&f); } };
struct PacketCloser { void operator()(AVPacket *p) const { av_packet_free(&p); } };
struct SwsCloser { void operator()(SwsContext *s) const { sws_freeContext(s); } };

QString averr(int rc)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(rc, buf, sizeof buf);
    return QString::fromUtf8(buf);
}

} // namespace

bool recommended(const MediaInfo &info)
{
    return info.kind == MediaInfo::Kind::Video && (info.width > 1920 || info.height > 1080);
}

namespace {

// One attempt; `hwType` empty = decode on the processor. `onCard` tells
// whether frames really came from the video card.
bool makeWith(const QString &source, const QString &target, QString *error,
              const std::function<void(double)> &progress, const std::atomic_bool *cancel,
              const QString &hwType, bool *onCard)
{
    auto fail = [error](const QString &e) {
        if (error)
            *error = e;
        return false;
    };

    // --- input
    AVFormatContext *raw = nullptr;
    int rc = avformat_open_input(&raw, QFileInfo(source).absoluteFilePath().toUtf8().constData(), nullptr, nullptr);
    if (rc < 0)
        return fail(QStringLiteral("cannot open %1: %2").arg(QDir::toNativeSeparators(source), averr(rc)));
    std::unique_ptr<AVFormatContext, InputCloser> in(raw);
    if ((rc = avformat_find_stream_info(in.get(), nullptr)) < 0)
        return fail(averr(rc));
    const AVCodec *dec = nullptr;
    const int vi = av_find_best_stream(in.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (vi < 0 || !dec || (in->streams[vi]->disposition & AV_DISPOSITION_ATTACHED_PIC))
        return fail(QStringLiteral("no video stream"));
    AVStream *ist = in->streams[vi];
    std::unique_ptr<AVCodecContext, CodecCloser> dc(avcodec_alloc_context3(dec));
    if (!dc || avcodec_parameters_to_context(dc.get(), ist->codecpar) < 0)
        return fail(QStringLiteral("cannot set up the decoder"));
    // The video card decodes when it can (one thread is enough then);
    // otherwise all cores: making proxies is a background batch job.
    const bool hw = !hwType.isEmpty() && HwAccel::attach(dc.get(), dec, hwType);
    dc->thread_count = hw ? 1 : 0;
    dc->pkt_timebase = ist->time_base;
    if ((rc = avcodec_open2(dc.get(), dec, nullptr)) < 0)
        return fail(averr(rc));

    // --- output size: 540 lines, same picture shape (sample aspect included)
    const AVRational sar = ist->codecpar->sample_aspect_ratio.num > 0 ? ist->codecpar->sample_aspect_ratio
                                                                       : AVRational{1, 1};
    const double dar = double(dc->width) * sar.num / sar.den / std::max(1, dc->height);
    const int outH = std::min(kHeight, dc->height) & ~1;
    const int outW = std::max(2, int(std::lround(outH * dar)) & ~1);

    // --- output file (written next to the target, renamed when complete)
    const QString temp = target + QStringLiteral(".part");
    QDir().mkpath(QFileInfo(target).absolutePath());
    AVFormatContext *oraw = nullptr;
    if ((rc = avformat_alloc_output_context2(&oraw, nullptr, "matroska", nullptr)) < 0)
        return fail(averr(rc));
    std::unique_ptr<AVFormatContext, OutputCloser> out(oraw);

    const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!enc)
        return fail(QStringLiteral("MPEG-4 encoder missing"));
    std::unique_ptr<AVCodecContext, CodecCloser> ec(avcodec_alloc_context3(enc));
    ec->width = outW;
    ec->height = outH;
    ec->pix_fmt = AV_PIX_FMT_YUV420P;
    ec->sample_aspect_ratio = AVRational{1, 1};
    // MPEG-4 Part 2 allows time bases up to 1/65535: use the frame rate when
    // it fits, else milliseconds.
    AVRational fr = ist->avg_frame_rate.num > 0 ? ist->avg_frame_rate : ist->r_frame_rate;
    ec->time_base = (fr.num > 0 && fr.den > 0 && fr.num <= 65535) ? av_inv_q(fr) : AVRational{1, 1000};
    if (fr.num > 0)
        ec->framerate = fr;
    ec->gop_size = 12;    // a key frame every 12 frames: quick seeking
    ec->max_b_frames = 0; // frames come out in order: simple and fast to decode
    ec->flags |= AV_CODEC_FLAG_QSCALE;
    ec->global_quality = FF_QP2LAMBDA * 5;
    ec->thread_count = 0;
    if (out->oformat->flags & AVFMT_GLOBALHEADER)
        ec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if ((rc = avcodec_open2(ec.get(), enc, nullptr)) < 0)
        return fail(averr(rc));

    // Stream map: the proxy picture plus every audio stream copied as-is.
    std::vector<int> map(in->nb_streams, -1);
    AVStream *ovs = avformat_new_stream(out.get(), nullptr);
    avcodec_parameters_from_context(ovs->codecpar, ec.get());
    ovs->time_base = ec->time_base;
    map[vi] = ovs->index;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(60, 31, 102)
    // Phone videos carry their rotation as side data; keep it.
    if (const AVPacketSideData *sd = av_packet_side_data_get(ist->codecpar->coded_side_data,
                                                             ist->codecpar->nb_coded_side_data,
                                                             AV_PKT_DATA_DISPLAYMATRIX)) {
        if (AVPacketSideData *nsd = av_packet_side_data_new(&ovs->codecpar->coded_side_data,
                                                            &ovs->codecpar->nb_coded_side_data,
                                                            AV_PKT_DATA_DISPLAYMATRIX, sd->size, 0))
            memcpy(nsd->data, sd->data, sd->size);
    }
#endif
    for (unsigned i = 0; i < in->nb_streams; ++i) {
        AVStream *s = in->streams[i];
        if (s->codecpar->codec_type != AVMEDIA_TYPE_AUDIO
            || avformat_query_codec(out->oformat, s->codecpar->codec_id, FF_COMPLIANCE_NORMAL) != 1)
            continue;
        AVStream *os = avformat_new_stream(out.get(), nullptr);
        if (avcodec_parameters_copy(os->codecpar, s->codecpar) < 0)
            return fail(QStringLiteral("cannot copy audio"));
        os->codecpar->codec_tag = 0;
        os->time_base = s->time_base;
        map[i] = os->index;
    }
    for (unsigned i = 0; i < in->nb_streams; ++i)
        if (map[i] < 0)
            in->streams[i]->discard = AVDISCARD_ALL;

    const QByteArray tempPath = QFileInfo(temp).absoluteFilePath().toUtf8();
    if ((rc = avio_open(&out->pb, tempPath.constData(), AVIO_FLAG_WRITE)) < 0) {
        // The cache folder may have been removed meanwhile (cache cleared,
        // another program): make it again and try once more.
        QDir().mkpath(QFileInfo(target).absolutePath());
        if ((rc = avio_open(&out->pb, tempPath.constData(), AVIO_FLAG_WRITE)) < 0)
            return fail(QStringLiteral("cannot write %1: %2").arg(QDir::toNativeSeparators(temp), averr(rc)));
    }
    if ((rc = avformat_write_header(out.get(), nullptr)) < 0) {
        out.reset();
        QFile::remove(temp);
        return fail(averr(rc));
    }

    std::unique_ptr<SwsContext, SwsCloser> sws;
    std::unique_ptr<AVFrame, FrameCloser> frame(av_frame_alloc()), scaled(av_frame_alloc()), cpuFrame(av_frame_alloc());
    scaled->format = AV_PIX_FMT_YUV420P;
    scaled->width = outW;
    scaled->height = outH;
    if (av_frame_get_buffer(scaled.get(), 0) < 0)
        return fail(QStringLiteral("out of memory"));
    std::unique_ptr<AVPacket, PacketCloser> pkt(av_packet_alloc()), opkt(av_packet_alloc());

    const double durationUs = in->duration > 0 ? double(in->duration) : 0;
    const int64_t startUs = in->start_time != AV_NOPTS_VALUE ? in->start_time : 0;
    int64_t lastPts = AV_NOPTS_VALUE;
    int srcW = 0, srcH = 0, srcFmt = -1; // what the scaler was made for
    QString failure;

    auto drainEncoder = [&]() -> bool {
        while ((rc = avcodec_receive_packet(ec.get(), opkt.get())) >= 0) {
            opkt->stream_index = ovs->index;
            av_packet_rescale_ts(opkt.get(), ec->time_base, ovs->time_base);
            if ((rc = av_interleaved_write_frame(out.get(), opkt.get())) < 0) {
                failure = averr(rc);
                return false;
            }
        }
        return rc == AVERROR(EAGAIN) || rc == AVERROR_EOF;
    };
    auto encodeFrame = [&](AVFrame *f) -> bool {
        if (f) {
            const int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
            if (ts == AV_NOPTS_VALUE)
                return true;
            const int64_t pts = av_rescale_q(ts, ist->time_base, ec->time_base);
            if (lastPts != AV_NOPTS_VALUE && pts <= lastPts)
                return true; // two source frames on one tick: keep the first
            lastPts = pts;
            if (!sws || f->width != srcW || f->height != srcH || f->format != srcFmt) {
                srcW = f->width;
                srcH = f->height;
                srcFmt = f->format;
                sws.reset(sws_getContext(f->width, f->height, AVPixelFormat(f->format), outW, outH,
                                         AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr));
                if (!sws) {
                    failure = QStringLiteral("cannot convert the picture");
                    return false;
                }
            }
            if (av_frame_make_writable(scaled.get()) < 0)
                return false;
            sws_scale(sws.get(), f->data, f->linesize, 0, f->height, scaled->data, scaled->linesize);
            scaled->pts = pts;
            if (progress && durationUs > 0)
                progress(std::clamp((av_rescale_q(ts, ist->time_base, AVRational{1, 1000000}) - startUs) / durationUs,
                                    0.0, 1.0));
        }
        if ((rc = avcodec_send_frame(ec.get(), f ? scaled.get() : nullptr)) < 0) {
            failure = averr(rc);
            return false;
        }
        return drainEncoder();
    };

    // A decoded frame, copied from the video card's memory first if needed.
    auto takeFrame = [&]() -> bool {
        if (frame->hw_frames_ctx && onCard)
            *onCard = true;
        AVFrame *f = HwAccel::download(frame.get(), cpuFrame.get());
        if (!f) {
            failure = QStringLiteral("cannot copy the picture from the video card");
            return false;
        }
        return encodeFrame(f);
    };

    bool ok = true;
    while (ok && av_read_frame(in.get(), pkt.get()) >= 0) {
        if (cancel && cancel->load()) {
            failure = QStringLiteral("cancelled");
            ok = false;
        } else if (pkt->stream_index == vi) {
            if (avcodec_send_packet(dc.get(), pkt.get()) >= 0)
                while (ok && avcodec_receive_frame(dc.get(), frame.get()) >= 0)
                    ok = takeFrame();
        } else if (map[pkt->stream_index] >= 0) {
            AVStream *os = out->streams[map[pkt->stream_index]];
            av_packet_rescale_ts(pkt.get(), in->streams[pkt->stream_index]->time_base, os->time_base);
            pkt->stream_index = os->index;
            pkt->pos = -1;
            if ((rc = av_interleaved_write_frame(out.get(), pkt.get())) < 0) {
                failure = averr(rc);
                ok = false;
            }
        }
        av_packet_unref(pkt.get());
    }
    if (ok) {
        avcodec_send_packet(dc.get(), nullptr);
        while (ok && avcodec_receive_frame(dc.get(), frame.get()) >= 0)
            ok = takeFrame();
    }
    if (ok)
        ok = encodeFrame(nullptr); // flush the encoder
    if (ok && lastPts == AV_NOPTS_VALUE) {
        failure = QStringLiteral("no video frames decoded");
        ok = false;
    }
    if (ok && (rc = av_write_trailer(out.get())) < 0) {
        failure = averr(rc);
        ok = false;
    }
    out.reset(); // closes the file
    if (!ok) {
        QFile::remove(temp);
        return fail(failure.isEmpty() ? QStringLiteral("proxy failed") : failure);
    }
    QFile::remove(target);
    if (!QFile::rename(temp, target)) {
        QFile::remove(temp);
        return fail(QStringLiteral("cannot write %1").arg(QDir::toNativeSeparators(target)));
    }
    if (progress)
        progress(1.0);
    return true;
}

} // namespace

bool make(const QString &source, const QString &target, QString *error,
          const std::function<void(double)> &progress, const std::atomic_bool *cancel, QString *decoder)
{
    const QString hw = HwAccel::decoder();
    bool onCard = false;
    QString err;
    bool ok = !hw.isEmpty() && makeWith(source, target, &err, progress, cancel, hw, &onCard);
    if (!ok && !(cancel && cancel->load())) {
        // No video card, or it failed on this file: the processor does it.
        onCard = false;
        err.clear();
        ok = makeWith(source, target, &err, progress, cancel, QString(), nullptr);
    }
    if (decoder)
        *decoder = ok && onCard ? hw : QString();
    if (!ok && error)
        *error = err;
    return ok;
}

} // namespace bc::Proxy
