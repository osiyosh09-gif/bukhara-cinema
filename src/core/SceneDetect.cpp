#include "core/SceneDetect.h"

#include "core/HwAccel.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace bc::SceneDetect {

namespace {

constexpr quint32 kVersion = 1;
constexpr int kBins = 16;
constexpr int kPixels = kWidth * kHeight;

struct FormatCloser { void operator()(AVFormatContext *c) const { avformat_close_input(&c); } };
struct CodecCloser { void operator()(AVCodecContext *c) const { avcodec_free_context(&c); } };
struct FrameCloser { void operator()(AVFrame *f) const { av_frame_free(&f); } };
struct PacketCloser { void operator()(AVPacket *p) const { av_packet_free(&p); } };
struct SwsCloser { void operator()(SwsContext *s) const { sws_freeContext(s); } };

} // namespace

float difference(const unsigned char *a, const unsigned char *b)
{
    // Histogram distance per channel (0..1) and the mean pixel change (0..1).
    std::array<int, 3 * kBins> ha{}, hb{};
    qint64 pix = 0;
    for (int i = 0; i < kPixels * 3; ++i) {
        const int c = i % 3;
        ++ha[size_t(c * kBins + a[i] * kBins / 256)];
        ++hb[size_t(c * kBins + b[i] * kBins / 256)];
        pix += std::abs(int(a[i]) - int(b[i]));
    }
    qint64 hist = 0;
    for (size_t i = 0; i < ha.size(); ++i)
        hist += std::abs(ha[i] - hb[i]);
    const double h = double(hist) / (2.0 * 3 * kPixels);
    const double p = double(pix) / (255.0 * 3 * kPixels);
    return float(0.5 * h + 0.5 * std::min(1.0, p * 2));
}

bool compute(const QString &path, Profile &out, QString *error,
             const std::function<void(double)> &progress, const std::atomic_bool *cancel)
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
    const int vi = av_find_best_stream(raw, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (vi < 0 || !dec || (raw->streams[vi]->disposition & AV_DISPOSITION_ATTACHED_PIC))
        return fail(QStringLiteral("no video stream"));
    AVStream *st = raw->streams[vi];
    for (unsigned i = 0; i < raw->nb_streams; ++i)
        if (int(i) != vi)
            raw->streams[i]->discard = AVDISCARD_ALL;

    // The video card first when allowed; the processor if it does not open.
    std::unique_ptr<AVCodecContext, CodecCloser> cc;
    const QString hw = HwAccel::decoder();
    for (int attempt = hw.isEmpty() ? 1 : 0; attempt < 2 && !cc; ++attempt) {
        std::unique_ptr<AVCodecContext, CodecCloser> c(avcodec_alloc_context3(dec));
        if (!c || avcodec_parameters_to_context(c.get(), st->codecpar) < 0)
            return fail(QStringLiteral("cannot set up the decoder"));
        c->pkt_timebase = st->time_base;
        const bool onCard = attempt == 0 && HwAccel::attach(c.get(), dec, hw);
        c->thread_count = onCard ? 1 : 0;
        if (attempt == 0 && !onCard)
            continue;
        if (avcodec_open2(c.get(), dec, nullptr) >= 0)
            cc = std::move(c);
    }
    if (!cc)
        return fail(QStringLiteral("cannot open the video decoder"));

    std::unique_ptr<AVFrame, FrameCloser> frame(av_frame_alloc()), cpu(av_frame_alloc());
    std::unique_ptr<AVPacket, PacketCloser> pkt(av_packet_alloc());
    std::unique_ptr<SwsContext, SwsCloser> sws;
    int srcW = 0, srcH = 0, srcFmt = -1;
    std::vector<unsigned char> prev(size_t(kPixels) * 3), cur(size_t(kPixels) * 3);
    bool havePrev = false;
    const int64_t fileStart = raw->start_time != AV_NOPTS_VALUE ? raw->start_time : 0;
    const double durationUs = raw->duration > 0 ? double(raw->duration) : 0;
    int lastPercent = -1;
    Profile p;
    QString failure;

    auto take = [&]() -> bool {
        AVFrame *f = HwAccel::download(frame.get(), cpu.get());
        if (!f) {
            failure = QStringLiteral("cannot copy the picture from the video card");
            return false;
        }
        const int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
        if (ts == AV_NOPTS_VALUE)
            return true;
        const qint64 us = av_rescale_q(ts, st->time_base, AVRational{1, 1000000}) - fileStart;
        if (!p.timeUs.empty() && us <= p.timeUs.back())
            return true;
        if (!sws || f->width != srcW || f->height != srcH || f->format != srcFmt) {
            srcW = f->width;
            srcH = f->height;
            srcFmt = f->format;
            sws.reset(sws_getContext(f->width, f->height, AVPixelFormat(f->format), kWidth, kHeight,
                                     AV_PIX_FMT_RGB24, SWS_AREA, nullptr, nullptr, nullptr));
            if (!sws) {
                failure = QStringLiteral("cannot convert the picture");
                return false;
            }
        }
        uint8_t *dst[4] = {cur.data(), nullptr, nullptr, nullptr};
        int lines[4] = {kWidth * 3, 0, 0, 0};
        sws_scale(sws.get(), f->data, f->linesize, 0, f->height, dst, lines);
        p.timeUs.push_back(us);
        p.score.push_back(havePrev ? difference(prev.data(), cur.data()) : 0.f);
        std::swap(prev, cur);
        havePrev = true;
        if (progress && durationUs > 0) {
            const int percent = int(std::clamp(us / durationUs, 0.0, 1.0) * 100);
            if (percent != lastPercent) {
                lastPercent = percent;
                progress(percent / 100.0);
            }
        }
        return true;
    };

    bool ok = true;
    while (ok && av_read_frame(raw, pkt.get()) >= 0) {
        if (cancel && cancel->load()) {
            failure = QStringLiteral("cancelled");
            ok = false;
        } else if (pkt->stream_index == vi && avcodec_send_packet(cc.get(), pkt.get()) >= 0) {
            while (ok && avcodec_receive_frame(cc.get(), frame.get()) >= 0)
                ok = take();
        }
        av_packet_unref(pkt.get());
    }
    if (ok) {
        avcodec_send_packet(cc.get(), nullptr);
        while (ok && avcodec_receive_frame(cc.get(), frame.get()) >= 0)
            ok = take();
    }
    if (!ok)
        return fail(failure);
    if (p.timeUs.empty())
        return fail(QStringLiteral("no video frames decoded"));
    if (progress)
        progress(1.0);
    out = std::move(p);
    return true;
}

QList<qint64> cuts(const Profile &p, double sensitivity, qint64 minGapUs)
{
    // A cut stands out from the frames around it: its score is above a
    // floor (lower with a higher sensitivity) and well above the typical
    // change of the neighbouring frames.
    const double s = std::clamp(sensitivity, 0.0, 1.0);
    const double floor = 0.40 - 0.30 * s;  // 0.40 .. 0.10
    const double ratio = 4.0 - 2.0 * s;    // 4x .. 2x the neighbours
    constexpr int kWindow = 6;
    const int n = int(p.score.size());
    QList<qint64> out;
    float lastScore = 0;
    std::vector<float> around;
    for (int i = 1; i < n; ++i) {
        const float v = p.score[size_t(i)];
        if (v < floor)
            continue;
        around.clear();
        for (int j = std::max(1, i - kWindow); j <= std::min(n - 1, i + kWindow); ++j)
            if (j != i)
                around.push_back(p.score[size_t(j)]);
        float typical = 0;
        if (!around.empty()) {
            std::nth_element(around.begin(), around.begin() + around.size() / 2, around.end());
            typical = around[around.size() / 2];
        }
        if (v < ratio * typical + 0.02)
            continue;
        const qint64 t = p.timeUs[size_t(i)];
        if (t < minGapUs / 2)
            continue; // right at the start: nothing to cut
        if (!out.isEmpty() && t - out.last() < minGapUs) {
            // Two candidates close together: keep the stronger one.
            if (v > lastScore) {
                out.last() = t;
                lastScore = v;
            }
            continue;
        }
        out << t;
        lastScore = v;
    }
    return out;
}

bool save(const QString &file, const Profile &p)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream s(&f);
    s.setFloatingPointPrecision(QDataStream::SinglePrecision);
    s.writeRawData("BCSC", 4);
    s << kVersion << quint32(p.timeUs.size());
    for (size_t i = 0; i < p.timeUs.size(); ++i)
        s << qint64(p.timeUs[i]) << p.score[i];
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
    quint32 version = 0, n = 0;
    if (s.readRawData(magic, 4) != 4 || std::string(magic, 4) != "BCSC")
        return false;
    s >> version >> n;
    if (version != kVersion || n == 0 || n > 24u * 3600u * 240u)
        return false;
    Profile r;
    r.timeUs.resize(n);
    r.score.resize(n);
    for (quint32 i = 0; i < n; ++i) {
        qint64 t = 0;
        s >> t >> r.score[i];
        r.timeUs[i] = t;
    }
    if (s.status() != QDataStream::Ok)
        return false;
    p = std::move(r);
    return true;
}

} // namespace bc::SceneDetect
