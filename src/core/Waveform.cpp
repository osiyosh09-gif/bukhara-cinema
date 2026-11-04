#include "core/Waveform.h"

#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <algorithm>
#include <cmath>
#include <memory>

namespace bc::Waveform {

namespace {

struct FormatCloser { void operator()(AVFormatContext *c) const { avformat_close_input(&c); } };
struct CodecCloser { void operator()(AVCodecContext *c) const { avcodec_free_context(&c); } };
struct FrameCloser { void operator()(AVFrame *f) const { av_frame_free(&f); } };
struct PacketCloser { void operator()(AVPacket *p) const { av_packet_free(&p); } };

// Loudest absolute sample (0..1) of sample `i` over all channels.
float framePeak(const AVFrame *f, int i, int channels)
{
    const auto fmt = AVSampleFormat(f->format);
    const bool planar = av_sample_fmt_is_planar(fmt);
    float best = 0;
    for (int ch = 0; ch < channels; ++ch) {
        const uint8_t *base = planar ? f->extended_data[ch] : f->extended_data[0];
        const int idx = planar ? i : i * channels + ch;
        float v = 0;
        switch (av_get_packed_sample_fmt(fmt)) {
        case AV_SAMPLE_FMT_U8: v = (reinterpret_cast<const uint8_t *>(base)[idx] - 128) / 128.f; break;
        case AV_SAMPLE_FMT_S16: v = reinterpret_cast<const int16_t *>(base)[idx] / 32768.f; break;
        case AV_SAMPLE_FMT_S32: v = float(reinterpret_cast<const int32_t *>(base)[idx] / 2147483648.0); break;
        case AV_SAMPLE_FMT_S64: v = float(double(reinterpret_cast<const int64_t *>(base)[idx]) / 9.2233720368547758e18); break;
        case AV_SAMPLE_FMT_FLT: v = reinterpret_cast<const float *>(base)[idx]; break;
        case AV_SAMPLE_FMT_DBL: v = float(reinterpret_cast<const double *>(base)[idx]); break;
        default: return 0;
        }
        best = std::max(best, std::fabs(v));
    }
    return best;
}

} // namespace

bool compute(const QString &path, QByteArray &peaks, QString *error, const std::atomic_bool *cancel)
{
    auto fail = [error](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    AVFormatContext *raw = nullptr;
    if (avformat_open_input(&raw, QFileInfo(path).absoluteFilePath().toUtf8().constData(), nullptr, nullptr) < 0)
        return fail(QStringLiteral("cannot open file"));
    std::unique_ptr<AVFormatContext, FormatCloser> fmt(raw);
    if (avformat_find_stream_info(raw, nullptr) < 0)
        return fail(QStringLiteral("cannot read stream info"));
    const AVCodec *dec = nullptr;
    const int ai = av_find_best_stream(raw, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
    if (ai < 0 || !dec)
        return fail(QStringLiteral("no audio stream"));
    AVStream *st = raw->streams[ai];
    std::unique_ptr<AVCodecContext, CodecCloser> cc(avcodec_alloc_context3(dec));
    if (!cc || avcodec_parameters_to_context(cc.get(), st->codecpar) < 0 || avcodec_open2(cc.get(), dec, nullptr) < 0)
        return fail(QStringLiteral("cannot open audio decoder"));
    for (unsigned i = 0; i < raw->nb_streams; ++i)
        if (int(i) != ai)
            raw->streams[i]->discard = AVDISCARD_ALL; // read audio packets only

    std::unique_ptr<AVFrame, FrameCloser> frame(av_frame_alloc());
    std::unique_ptr<AVPacket, PacketCloser> pkt(av_packet_alloc());
    const int64_t fileStart = raw->start_time != AV_NOPTS_VALUE ? raw->start_time : 0; // microseconds

    QByteArray out;
    int64_t sampleIndex = -1; // running position in samples from the file start
    int rate = 0;

    auto consume = [&] {
        const int channels = frame->ch_layout.nb_channels;
        rate = frame->sample_rate;
        if (channels <= 0 || rate <= 0)
            return;
        if (sampleIndex < 0) {
            // The first frame may start later than the file (video first).
            sampleIndex = 0;
            if (frame->pts != AV_NOPTS_VALUE) {
                const int64_t us = av_rescale_q(frame->pts, st->time_base, AVRational{1, 1000000}) - fileStart;
                sampleIndex = std::max<int64_t>(0, av_rescale(us, rate, 1000000));
            }
        }
        for (int i = 0; i < frame->nb_samples; ++i, ++sampleIndex) {
            const qsizetype bucket = qsizetype(sampleIndex * kPeaksPerSecond / rate);
            if (bucket >= out.size()) {
                if (bucket > 24 * 3600 * kPeaksPerSecond)
                    return; // corrupt timestamps: stop growing
                out.resize(bucket + 1, char(0));
            }
            const int v = std::clamp(int(std::lround(framePeak(frame.get(), i, channels) * 255)), 0, 255);
            auto &cell = reinterpret_cast<uint8_t &>(out[bucket]);
            cell = uint8_t(std::max<int>(cell, v));
        }
    };

    while (av_read_frame(raw, pkt.get()) >= 0) {
        if (cancel && cancel->load())
            return fail(QStringLiteral("cancelled"));
        if (pkt->stream_index == ai && avcodec_send_packet(cc.get(), pkt.get()) >= 0)
            while (avcodec_receive_frame(cc.get(), frame.get()) >= 0)
                consume();
        av_packet_unref(pkt.get());
    }
    avcodec_send_packet(cc.get(), nullptr);
    while (avcodec_receive_frame(cc.get(), frame.get()) >= 0)
        consume();
    if (out.isEmpty())
        return fail(QStringLiteral("no audio decoded"));
    peaks = out;
    return true;
}

int peakIn(const QByteArray &peaks, double fromSec, double toSec)
{
    if (peaks.isEmpty())
        return 0;
    qsizetype a = qsizetype(std::floor(fromSec * kPeaksPerSecond));
    qsizetype b = qsizetype(std::ceil(toSec * kPeaksPerSecond));
    a = std::clamp<qsizetype>(a, 0, peaks.size());
    b = std::clamp<qsizetype>(std::max(b, a + 1), 0, peaks.size());
    int best = 0;
    for (qsizetype i = a; i < b; ++i)
        best = std::max(best, int(uint8_t(peaks[i])));
    return best;
}

bool save(const QString &file, const QByteArray &peaks)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream s(&f);
    s.writeRawData("BCWF", 4);
    s << qint32(1) << qint32(kPeaksPerSecond) << peaks;
    return f.commit();
}

bool load(const QString &file, QByteArray &peaks)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QDataStream s(&f);
    char magic[4];
    qint32 version = 0, rate = 0;
    if (s.readRawData(magic, 4) != 4 || memcmp(magic, "BCWF", 4) != 0)
        return false;
    s >> version >> rate;
    if (version != 1 || rate != kPeaksPerSecond)
        return false;
    QByteArray data;
    s >> data;
    if (s.status() != QDataStream::Ok || data.isEmpty())
        return false;
    peaks = data;
    return true;
}

} // namespace bc::Waveform
