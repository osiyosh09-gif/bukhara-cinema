#include "core/Loudness.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
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

namespace bc::Loudness {

// ---------------------------------------------------------------------------
// Meter

Meter::Meter(int channels, int sampleRate)
    : m_channels(std::max(1, channels))
    , m_partSize(std::max(1, sampleRate / kPartsPerSecond))
{
    const double rate = std::max(1, sampleRate);
    // K-weighting for any sample rate (the BS.1770 filters re-derived from
    // their analogue prototypes; equal to the published 48 kHz coefficients).
    Biquad shelf{}, hp{};
    {
        const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan(M_PI * f0 / rate);
        const double vh = std::pow(10.0, g / 20.0), vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        shelf.b0 = (vh + vb * k / q + k * k) / a0;
        shelf.b1 = 2.0 * (k * k - vh) / a0;
        shelf.b2 = (vh - vb * k / q + k * k) / a0;
        shelf.a1 = 2.0 * (k * k - 1.0) / a0;
        shelf.a2 = (1.0 - k / q + k * k) / a0;
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan(M_PI * f0 / rate);
        const double a0 = 1.0 + k / q + k * k;
        hp.b0 = 1.0;
        hp.b1 = -2.0;
        hp.b2 = 1.0;
        hp.a1 = 2.0 * (k * k - 1.0) / a0;
        hp.a2 = (1.0 - k / q + k * k) / a0;
    }
    m_shelf.assign(size_t(m_channels), shelf);
    m_highpass.assign(size_t(m_channels), hp);
    // Channel weights: 1 for front channels; in 5.1 the LFE is left out and
    // the surround channels count 1.41.
    m_weight.assign(size_t(m_channels), 1.0);
    if (m_channels == 6) {
        m_weight[3] = 0.0;
        m_weight[4] = m_weight[5] = 1.41;
    }
}

void Meter::add(const float *samples, int frames)
{
    for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < m_channels; ++c) {
            const float x = samples[size_t(i) * size_t(m_channels) + size_t(c)];
            m_peak = std::max(m_peak, std::fabs(x));
            if (m_weight[size_t(c)] == 0.0)
                continue;
            const double y = m_highpass[size_t(c)].run(m_shelf[size_t(c)].run(x));
            m_sum += m_weight[size_t(c)] * y * y;
        }
        if (++m_fill == m_partSize) {
            m_profile.power.push_back(float(m_sum / m_partSize));
            m_profile.peak.push_back(m_peak);
            m_fill = 0;
            m_sum = 0;
            m_peak = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Measuring

namespace {

double toLufs(double power) { return power > 0 ? -0.691 + 10.0 * std::log10(power) : -1e9; }

std::pair<qsizetype, qsizetype> partRange(const Profile &p, double fromSec, double toSec)
{
    const qsizetype n = qsizetype(p.power.size());
    const qsizetype a = std::clamp<qsizetype>(qsizetype(std::floor(fromSec * kPartsPerSecond + 1e-9)), 0, n);
    const qsizetype b = std::clamp<qsizetype>(qsizetype(std::floor(toSec * kPartsPerSecond + 1e-9)), a, n);
    return {a, b};
}

} // namespace

std::vector<double> blocks(const Profile &p, double fromSec, double toSec)
{
    const auto [a, b] = partRange(p, fromSec, toSec);
    std::vector<double> out;
    for (qsizetype i = a; i + 4 <= b; ++i)
        out.push_back((double(p.power[size_t(i)]) + p.power[size_t(i + 1)] + p.power[size_t(i + 2)]
                       + p.power[size_t(i + 3)]) / 4.0);
    return out;
}

double integrate(const std::vector<double> &blockPowers)
{
    // Absolute gate, then a relative gate 10 LU below the mean of what is left.
    double sum = 0;
    int n = 0;
    for (double b : blockPowers)
        if (toLufs(b) > -70.0) {
            sum += b;
            ++n;
        }
    if (n == 0)
        return kSilence;
    const double gate = toLufs(sum / n) - 10.0;
    sum = 0;
    n = 0;
    for (double b : blockPowers)
        if (toLufs(b) > -70.0 && toLufs(b) > gate) {
            sum += b;
            ++n;
        }
    return n > 0 ? std::max(kSilence, toLufs(sum / n)) : kSilence;
}

double peakDb(const Profile &p, double fromSec, double toSec)
{
    const auto [a, b] = partRange(p, fromSec, std::max(toSec, fromSec + 1.0 / kPartsPerSecond));
    float best = 0;
    for (qsizetype i = a; i < b; ++i)
        best = std::max(best, p.peak[size_t(i)]);
    return best > 0 ? 20.0 * std::log10(best) : -120.0;
}

Result measure(const Profile &p, double fromSec, double toSec)
{
    Result r;
    const std::vector<double> b = blocks(p, fromSec, toSec);
    r.lufs = integrate(b);
    r.peakDb = peakDb(p, fromSec, toSec);
    r.valid = !b.empty() && r.lufs > kSilence;
    return r;
}

// ---------------------------------------------------------------------------
// Decoding

namespace {

struct FormatCloser { void operator()(AVFormatContext *c) const { avformat_close_input(&c); } };
struct CodecCloser { void operator()(AVCodecContext *c) const { avcodec_free_context(&c); } };
struct FrameCloser { void operator()(AVFrame *f) const { av_frame_free(&f); } };
struct PacketCloser { void operator()(AVPacket *p) const { av_packet_free(&p); } };

float sampleAt(const AVFrame *f, int i, int ch, int channels)
{
    const auto fmt = AVSampleFormat(f->format);
    const bool planar = av_sample_fmt_is_planar(fmt);
    const uint8_t *base = planar ? f->extended_data[ch] : f->extended_data[0];
    const int idx = planar ? i : i * channels + ch;
    switch (av_get_packed_sample_fmt(fmt)) {
    case AV_SAMPLE_FMT_U8: return (reinterpret_cast<const uint8_t *>(base)[idx] - 128) / 128.f;
    case AV_SAMPLE_FMT_S16: return reinterpret_cast<const int16_t *>(base)[idx] / 32768.f;
    case AV_SAMPLE_FMT_S32: return float(reinterpret_cast<const int32_t *>(base)[idx] / 2147483648.0);
    case AV_SAMPLE_FMT_S64: return float(double(reinterpret_cast<const int64_t *>(base)[idx]) / 9.2233720368547758e18);
    case AV_SAMPLE_FMT_FLT: return reinterpret_cast<const float *>(base)[idx];
    case AV_SAMPLE_FMT_DBL: return float(reinterpret_cast<const double *>(base)[idx]);
    default: return 0;
    }
}

} // namespace

bool compute(const QString &path, Profile &out, QString *error, const std::atomic_bool *cancel)
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
            raw->streams[i]->discard = AVDISCARD_ALL;

    std::unique_ptr<AVFrame, FrameCloser> frame(av_frame_alloc());
    std::unique_ptr<AVPacket, PacketCloser> pkt(av_packet_alloc());
    const int64_t fileStart = raw->start_time != AV_NOPTS_VALUE ? raw->start_time : 0; // microseconds
    std::unique_ptr<Meter> meter;
    int meterChannels = 0;
    std::vector<float> buf;

    auto consume = [&] {
        const int channels = frame->ch_layout.nb_channels;
        const int rate = frame->sample_rate;
        if (channels <= 0 || rate <= 0)
            return;
        if (!meter) {
            meter = std::make_unique<Meter>(channels, rate);
            meterChannels = channels;
            // Audio that starts after the file start (video first): silence before it,
            // so the profile's times are file times.
            if (frame->pts != AV_NOPTS_VALUE) {
                const int64_t us = av_rescale_q(frame->pts, st->time_base, AVRational{1, 1000000}) - fileStart;
                int64_t lead = std::clamp<int64_t>(av_rescale(us, rate, 1000000), 0, int64_t(rate) * 3600);
                std::vector<float> zeros(size_t(4096) * size_t(channels), 0.0f);
                while (lead > 0) {
                    const int n = int(std::min<int64_t>(lead, 4096));
                    meter->add(zeros.data(), n);
                    lead -= n;
                }
            }
        }
        if (channels != meterChannels)
            return; // a channel layout change in the middle: those frames are left out
        buf.resize(size_t(frame->nb_samples) * size_t(channels));
        for (int i = 0; i < frame->nb_samples; ++i)
            for (int c = 0; c < channels; ++c)
                buf[size_t(i) * size_t(channels) + size_t(c)] = sampleAt(frame.get(), i, c, channels);
        meter->add(buf.data(), frame->nb_samples);
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
    if (!meter || meter->profile().isEmpty())
        return fail(QStringLiteral("no audio decoded"));
    out = meter->profile();
    return true;
}

// ---------------------------------------------------------------------------
// Cache file

namespace {
constexpr quint32 kVersion = 1;
}

bool save(const QString &file, const Profile &p)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream s(&f);
    s.setFloatingPointPrecision(QDataStream::SinglePrecision);
    s.writeRawData("BCLD", 4);
    s << kVersion << quint32(kPartsPerSecond) << quint32(p.power.size());
    for (size_t i = 0; i < p.power.size(); ++i)
        s << p.power[i] << p.peak[i];
    return s.status() == QDataStream::Ok && f.commit();
}

bool load(const QString &file, Profile &p)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QDataStream s(&f);
    s.setFloatingPointPrecision(QDataStream::SinglePrecision);
    char magic[4];
    quint32 version = 0, rate = 0, n = 0;
    if (s.readRawData(magic, 4) != 4 || std::string(magic, 4) != "BCLD")
        return false;
    s >> version >> rate >> n;
    if (version != kVersion || rate != quint32(kPartsPerSecond) || n == 0 || n > 24u * 3600u * kPartsPerSecond)
        return false;
    Profile r;
    r.power.resize(n);
    r.peak.resize(n);
    for (quint32 i = 0; i < n; ++i)
        s >> r.power[i] >> r.peak[i];
    if (s.status() != QDataStream::Ok)
        return false;
    p = std::move(r);
    return true;
}

} // namespace bc::Loudness
