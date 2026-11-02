#include "core/AudioSync.h"

#include "core/AudioFx.h"

#include <QFileInfo>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>

namespace bc::AudioSync {

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

// Level changes: dB level minus its average over about one second, zero
// where there is no sound at all (padding, digital silence).
std::vector<double> features(const std::vector<float> &env)
{
    const size_t n = env.size();
    std::vector<double> db(n);
    for (size_t i = 0; i < n; ++i)
        db[i] = 20.0 * std::log10(std::max(1e-5, double(env[i])));
    std::vector<double> prefix(n + 1, 0.0);
    for (size_t i = 0; i < n; ++i)
        prefix[i + 1] = prefix[i] + db[i];
    const size_t half = kRate / 2;
    std::vector<double> out(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        if (env[i] <= 1e-5f)
            continue;
        const size_t a = i > half ? i - half : 0;
        const size_t b = std::min(n, i + half + 1);
        const double avg = (prefix[b] - prefix[a]) / double(b - a);
        out[i] = std::clamp(db[i] - avg, -40.0, 40.0);
    }
    return out;
}

} // namespace

bool envelope(const QString &path, std::vector<float> &env, QString *error, const std::atomic_bool *cancel)
{
    auto fail = [error](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    env.clear();
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
    int rate = 0;
    int step = 0;      // samples per envelope value
    int count = 0;     // samples in the current step
    double sum = 0;    // sum of squares of the current step
    bool started = false;

    auto consume = [&] {
        const int channels = frame->ch_layout.nb_channels;
        if (channels <= 0 || frame->sample_rate <= 0)
            return;
        if (!started) {
            started = true;
            rate = frame->sample_rate;
            step = std::max(1, rate / kRate);
            // Sound that starts after the file start: silence before it, so
            // the envelope uses file times (as the timeline does).
            if (frame->pts != AV_NOPTS_VALUE) {
                const int64_t us = av_rescale_q(frame->pts, st->time_base, AVRational{1, 1000000}) - fileStart;
                const int64_t lead = std::clamp<int64_t>(us * kRate / 1000000, 0, int64_t(kRate) * 3600);
                env.assign(size_t(lead), 0.0f);
            }
        }
        if (frame->sample_rate != rate)
            return;
        for (int i = 0; i < frame->nb_samples; ++i) {
            float mono = 0;
            for (int c = 0; c < channels; ++c)
                mono += sampleAt(frame.get(), i, c, channels);
            mono /= float(channels);
            sum += double(mono) * mono;
            if (++count == step) {
                env.push_back(float(std::sqrt(sum / count)));
                sum = 0;
                count = 0;
            }
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
    if (count > 0)
        env.push_back(float(std::sqrt(sum / count)));
    if (env.empty())
        return fail(QStringLiteral("no audio decoded"));
    return true;
}

Match match(const std::vector<float> &refEnv, const std::vector<float> &otherEnv, double maxLagSec)
{
    Match m;
    const std::vector<double> a = features(refEnv);
    const std::vector<double> b = features(otherEnv);
    const qint64 la = qint64(a.size()), lb = qint64(b.size());
    if (la < kRate || lb < kRate)
        return m; // under a second: nothing to match

    size_t n = 1;
    while (n < size_t(la + lb))
        n <<= 1;
    std::vector<std::complex<float>> fa(n), fb(n);
    for (qint64 i = 0; i < la; ++i)
        fa[size_t(i)] = float(a[size_t(i)]);
    for (qint64 i = 0; i < lb; ++i)
        fb[size_t(i)] = float(b[size_t(i)]);
    fft(fa, false);
    fft(fb, false);
    for (size_t i = 0; i < n; ++i)
        fa[i] *= std::conj(fb[i]);
    fft(fa, true); // fa[d] = sum_k a[k + d] * b[k] (negative d wrapped to the end)

    // Energy of each overlap, so the score is a correlation coefficient and
    // short overlaps at the ends do not win by luck.
    std::vector<double> pa(size_t(la) + 1, 0.0), pb(size_t(lb) + 1, 0.0);
    for (qint64 i = 0; i < la; ++i)
        pa[size_t(i) + 1] = pa[size_t(i)] + a[size_t(i)] * a[size_t(i)];
    for (qint64 i = 0; i < lb; ++i)
        pb[size_t(i) + 1] = pb[size_t(i)] + b[size_t(i)] * b[size_t(i)];
    const qint64 minOverlap = std::max<qint64>(kRate, std::min<qint64>(10 * kRate, std::min(la, lb) / 2));
    qint64 lo = -(lb - minOverlap), hi = la - minOverlap;
    if (maxLagSec > 0) {
        const qint64 lim = qint64(std::ceil(maxLagSec * kRate));
        lo = std::max(lo, -lim);
        hi = std::min(hi, lim);
    }
    if (lo > hi)
        return m;
    std::vector<double> score(size_t(hi - lo + 1), 0.0);
    for (qint64 d = lo; d <= hi; ++d) {
        const qint64 a0 = std::max<qint64>(0, d), a1 = std::min(la, lb + d);
        const qint64 b0 = a0 - d, b1 = a1 - d;
        const double ea = pa[size_t(a1)] - pa[size_t(a0)];
        const double eb = pb[size_t(b1)] - pb[size_t(b0)];
        const double c = fa[size_t(d >= 0 ? d : qint64(n) + d)].real();
        score[size_t(d - lo)] = (ea > 0 && eb > 0) ? c / std::sqrt(ea * eb) : 0.0;
    }
    const auto best = std::max_element(score.begin(), score.end());
    const qint64 bi = qint64(best - score.begin());
    const double peak = *best;
    // The best place more than half a second away from it.
    double second = 0;
    for (qint64 i = 0; i < qint64(score.size()); ++i)
        if (std::abs(i - bi) > kRate / 2)
            second = std::max(second, score[size_t(i)]);
    double frac = 0;
    if (bi > 0 && bi + 1 < qint64(score.size())) {
        const double y0 = score[size_t(bi - 1)], y2 = score[size_t(bi + 1)];
        const double den = y0 - 2 * peak + y2;
        if (den < 0)
            frac = std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5);
    }
    m.offset = (double(bi + lo) + frac) / kRate;
    // How far the best place stands out from the next best one (1 = alone).
    m.confidence = peak > 0 ? std::clamp((peak - second) / std::max(1e-9, 1.0 - second), 0.0, 1.0) : 0.0;
    m.valid = peak >= 0.4 && m.confidence >= 0.3;
    return m;
}

QList<Match> align(const QList<std::vector<float>> &envelopes, double maxLagSec)
{
    QList<Match> out(envelopes.size());
    int ref = -1;
    for (int i = 0; i < envelopes.size() && ref < 0; ++i)
        if (!envelopes[i].empty())
            ref = i;
    if (ref < 0)
        return out;
    out[ref].valid = true;
    out[ref].confidence = 1;
    for (int i = 0; i < envelopes.size(); ++i) {
        if (i == ref || envelopes[i].empty())
            continue;
        const Match m = match(envelopes[ref], envelopes[i], maxLagSec);
        if (m.valid)
            out[i] = m;
        else
            out[i].confidence = m.confidence;
    }
    return out;
}

} // namespace bc::AudioSync
