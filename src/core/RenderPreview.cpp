#include "core/RenderPreview.h"

#include "core/Effects.h"
#include "core/MediaCache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

#include <algorithm>

namespace bc::RenderPreview {

namespace {

constexpr int kMaxDepth = 8;

class Signer
{
public:
    explicit Signer(const Project &project)
        : m_project(project)
    {
    }

    QByteArray media(const QUuid &id)
    {
        if (auto it = m_media.constFind(id); it != m_media.cend())
            return *it;
        QByteArray sig;
        if (const MediaItem *m = m_project.item(id)) {
            const QFileInfo fi(m->path);
            sig = (fi.absoluteFilePath() + QLatin1Char('|') + QString::number(fi.size()) + QLatin1Char('|')
                   + QString::number(fi.lastModified().toMSecsSinceEpoch()))
                      .toUtf8();
        } else {
            sig = "missing";
        }
        m_media.insert(id, sig);
        return sig;
    }

    // Everything a nested sequence's picture depends on.
    QByteArray nest(const QUuid &id, int depth)
    {
        const QString cacheKey = id.toString() + QString::number(depth);
        if (auto it = m_nests.constFind(cacheKey); it != m_nests.cend())
            return *it;
        QByteArray sig;
        const NestedSequence *n = m_project.nest(id);
        if (!n || depth >= kMaxDepth) {
            sig = "none";
        } else {
            QCryptographicHash h(QCryptographicHash::Sha1);
            h.addData(n->multicam ? "mc" : "n");
            h.addData(QJsonDocument(n->timeline.toJson()).toJson(QJsonDocument::Compact));
            for (const TimelineTrack &t : n->timeline.tracks)
                for (const TimelineClip &c : t.clips)
                    h.addData(c.isNested() ? nest(c.nestId, depth + 1) : media(c.mediaId));
            sig = h.result();
        }
        m_nests.insert(cacheKey, sig);
        return sig;
    }

private:
    const Project &m_project;
    QHash<QUuid, QByteArray> m_media;
    QHash<QString, QByteArray> m_nests;
};

// The clip as JSON without what does not change its picture inside a region
// (its place on the timeline and its ids).
QByteArray clipJson(const TimelineClip &c)
{
    Timeline t;
    TimelineTrack track;
    track.kind = Timeline::Kind::Video;
    TimelineClip copy = c;
    copy.start = 0;
    copy.id = QUuid();
    copy.linkId = QUuid();
    track.clips << copy;
    t.tracks << track;
    return QJsonDocument(t.toJson()).toJson(QJsonDocument::Compact);
}

bool heavyClip(const TimelineClip &c)
{
    return c.isNested() || c.speed != 1.0 || effectsActive(c.effects) || !c.color.isNeutral()
           || c.motion.animated() || !c.motion.isIdentity() || !c.head.isNone() || !c.tail.isNone();
}

} // namespace

QList<Region> regions(const Project &project, const Timeline &tl, const QSize &previewSize)
{
    QList<int> tracks;
    for (int ti = 0; ti < tl.tracks.size(); ++ti)
        if (tl.tracks[ti].kind == Timeline::Kind::Video && !tl.tracks[ti].hidden && !tl.tracks[ti].clips.isEmpty())
            tracks << ti;
    QList<qint64> cuts;
    QSet<qint64> joined; // cuts with a transition: the frames around them belong together
    for (int ti : tracks) {
        const QList<TimelineClip> &clips = tl.tracks[ti].clips;
        for (int i = 0; i < clips.size(); ++i) {
            cuts << clips[i].start << clips[i].end();
            if (i > 0 && clips[i - 1].end() == clips[i].start && !clips[i].head.isNone())
                joined.insert(clips[i].start);
        }
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    cuts.erase(std::remove_if(cuts.begin(), cuts.end(), [&](qint64 f) { return joined.contains(f); }), cuts.end());

    const SequenceSettings &seq = project.sequence;
    const QByteArray base = "bcpv1|" + QByteArray::number(seq.width) + 'x' + QByteArray::number(seq.height) + '@'
                            + QByteArray::number(seq.fpsNum) + '/' + QByteArray::number(seq.fpsDen) + '|'
                            + QByteArray::number(previewSize.width()) + 'x' + QByteArray::number(previewSize.height());
    Signer signer(project);
    QList<Region> out;
    for (int k = 0; k + 1 < cuts.size(); ++k) {
        const qint64 a = cuts[k], b = cuts[k + 1];
        QCryptographicHash h(QCryptographicHash::Sha1);
        h.addData(base);
        h.addData("|len" + QByteArray::number(b - a));
        int count = 0;
        int layer = 0; // stacking order among the tracks with a picture here only
        bool heavy = false;
        for (int order = 0; order < tracks.size(); ++order) {
            const QList<TimelineClip> &clips = tl.tracks[tracks[order]].clips;
            const int countBefore = count;
            for (int i = 0; i < clips.size(); ++i) {
                const TimelineClip &c = clips[i];
                if (c.end() <= a || c.start >= b)
                    continue;
                ++count;
                heavy = heavy || heavyClip(c);
                const bool prevTouches = i > 0 && clips[i - 1].end() == c.start;
                const bool nextTouches = i + 1 < clips.size() && clips[i + 1].start == c.end();
                h.addData("|t" + QByteArray::number(layer) + "|o" + QByteArray::number(a - c.start)
                          + (prevTouches ? "|p" : "|-") + (nextTouches ? "|n" : "|-"));
                h.addData(clipJson(c));
                h.addData(c.isNested() ? signer.nest(c.nestId, 1) : c.isTitle() ? QByteArray("title")
                                                                                 : signer.media(c.mediaId));
            }
            if (count > countBefore)
                ++layer;
        }
        if (count == 0)
            continue;
        out << Region{a, b - a, h.result().toHex(), heavy || count > 1};
    }
    return out;
}

QString folder()
{
    return MediaCache::folder(MediaCache::Kind::Preview);
}

QString fileFor(const QByteArray &key)
{
    return folder() + QLatin1Char('/') + QString::fromLatin1(key) + QStringLiteral(".mkv");
}

// ---------------------------------------------------------------------------

namespace {
QString averr(int rc)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(rc, buf, sizeof buf);
    return QString::fromUtf8(buf);
}
} // namespace

struct Writer::Impl
{
    AVFormatContext *out = nullptr;
    AVCodecContext *ec = nullptr;
    AVStream *stream = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *pkt = nullptr;
    SwsContext *sws = nullptr;
    QSize size;
    QString path, temp;
    int64_t pts = 0;
    bool headerWritten = false;

    ~Impl() { close(); }
    void close()
    {
        sws_freeContext(sws);
        sws = nullptr;
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&ec);
        if (out) {
            if (out->pb && !(out->oformat->flags & AVFMT_NOFILE))
                avio_closep(&out->pb);
            avformat_free_context(out);
            out = nullptr;
        }
    }
    bool drain(QString *error)
    {
        int rc;
        while ((rc = avcodec_receive_packet(ec, pkt)) >= 0) {
            pkt->stream_index = stream->index;
            if (pkt->duration <= 0)
                pkt->duration = 1; // one frame: the file's length counts the last one too
            av_packet_rescale_ts(pkt, ec->time_base, stream->time_base);
            if ((rc = av_interleaved_write_frame(out, pkt)) < 0) {
                if (error)
                    *error = averr(rc);
                return false;
            }
        }
        return rc == AVERROR(EAGAIN) || rc == AVERROR_EOF;
    }
};

Writer::Writer() = default;
Writer::~Writer()
{
    abort();
}

bool Writer::open(const QString &path, const QSize &size, int fpsNum, int fpsDen, QString *error)
{
    abort();
    d = std::make_unique<Impl>();
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        abort();
        return false;
    };
    d->path = path;
    d->temp = path + QStringLiteral(".part");
    d->size = QSize(std::max(2, size.width() & ~1), std::max(2, size.height() & ~1));
    QDir().mkpath(QFileInfo(path).absolutePath());
    int rc;
    if ((rc = avformat_alloc_output_context2(&d->out, nullptr, "matroska", nullptr)) < 0)
        return fail(averr(rc));
    const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!enc)
        return fail(QStringLiteral("MPEG-4 encoder missing"));
    d->ec = avcodec_alloc_context3(enc);
    d->ec->width = d->size.width();
    d->ec->height = d->size.height();
    d->ec->pix_fmt = AV_PIX_FMT_YUV420P;
    d->ec->sample_aspect_ratio = AVRational{1, 1};
    d->ec->time_base = AVRational{std::max(1, fpsDen), std::max(1, fpsNum)};
    d->ec->framerate = AVRational{std::max(1, fpsNum), std::max(1, fpsDen)};
    d->ec->gop_size = 1; // every frame a key frame: seeking costs nothing
    d->ec->max_b_frames = 0;
    d->ec->flags |= AV_CODEC_FLAG_QSCALE;
    d->ec->global_quality = FF_QP2LAMBDA * 3;
    d->ec->thread_count = 0;
    d->ec->colorspace = AVCOL_SPC_SMPTE170M;
    d->ec->color_primaries = AVCOL_PRI_SMPTE170M;
    d->ec->color_trc = AVCOL_TRC_SMPTE170M;
    d->ec->color_range = AVCOL_RANGE_MPEG;
    if (d->out->oformat->flags & AVFMT_GLOBALHEADER)
        d->ec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if ((rc = avcodec_open2(d->ec, enc, nullptr)) < 0)
        return fail(averr(rc));
    d->stream = avformat_new_stream(d->out, nullptr);
    avcodec_parameters_from_context(d->stream->codecpar, d->ec);
    d->stream->time_base = d->ec->time_base;

    const QByteArray tempPath = QFileInfo(d->temp).absoluteFilePath().toUtf8();
    if ((rc = avio_open(&d->out->pb, tempPath.constData(), AVIO_FLAG_WRITE)) < 0)
        return fail(QStringLiteral("cannot write %1: %2").arg(QDir::toNativeSeparators(d->temp), averr(rc)));
    if ((rc = avformat_write_header(d->out, nullptr)) < 0)
        return fail(averr(rc));
    d->headerWritten = true;

    d->frame = av_frame_alloc();
    d->frame->format = AV_PIX_FMT_YUV420P;
    d->frame->width = d->size.width();
    d->frame->height = d->size.height();
    if (av_frame_get_buffer(d->frame, 0) < 0)
        return fail(QStringLiteral("out of memory"));
    d->pkt = av_packet_alloc();
    d->sws = sws_getContext(d->size.width(), d->size.height(), AV_PIX_FMT_RGBA, d->size.width(), d->size.height(),
                            AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!d->sws)
        return fail(QStringLiteral("cannot convert the picture"));
    // Full-range RGB in, BT.601 video-range YUV out; Reader does the inverse.
    const int *coef = sws_getCoefficients(SWS_CS_ITU601);
    sws_setColorspaceDetails(d->sws, coef, 1, coef, 0, 0, 1 << 16, 1 << 16);
    return true;
}

bool Writer::write(const QImage &image, QString *error)
{
    if (!d || !d->frame)
        return false;
    QImage img = image.convertToFormat(QImage::Format_RGBA8888);
    if (img.size() != d->size)
        img = img.scaled(d->size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (img.isNull())
        img = QImage(d->size, QImage::Format_RGBA8888);
    if (av_frame_make_writable(d->frame) < 0)
        return false;
    const uint8_t *src[1] = {img.constBits()};
    const int stride[1] = {int(img.bytesPerLine())};
    sws_scale(d->sws, src, stride, 0, d->size.height(), d->frame->data, d->frame->linesize);
    d->frame->pts = d->pts++;
    int rc;
    if ((rc = avcodec_send_frame(d->ec, d->frame)) < 0) {
        if (error)
            *error = averr(rc);
        return false;
    }
    return d->drain(error);
}

bool Writer::finish(QString *error)
{
    if (!d || !d->headerWritten)
        return false;
    bool ok = avcodec_send_frame(d->ec, nullptr) >= 0 && d->drain(error);
    ok = av_write_trailer(d->out) >= 0 && ok;
    const QString temp = d->temp, path = d->path;
    d->close();
    d.reset();
    if (ok) {
        QFile::remove(path);
        ok = QFile::rename(temp, path);
        if (!ok && error)
            *error = QStringLiteral("cannot rename %1").arg(QDir::toNativeSeparators(temp));
    }
    if (!ok)
        QFile::remove(temp);
    return ok;
}

void Writer::abort()
{
    if (!d)
        return;
    const QString temp = d->temp;
    d->close();
    d.reset();
    if (!temp.isEmpty())
        QFile::remove(temp);
}

// ---------------------------------------------------------------------------

struct Reader::Impl
{
    QMutex mutex;
    AVFormatContext *in = nullptr;
    AVCodecContext *dc = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *pkt = nullptr;
    SwsContext *sws = nullptr;
    QSize swsSize;
    int stream = -1;
    AVRational tb{1, 25};     // stream time base
    AVRational rate{25, 1};   // frames per second
    qint64 count = 0;
    qint64 next = -1;         // index of the frame the next decode gives
    QSize size;

    ~Impl()
    {
        sws_freeContext(sws);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&dc);
        avformat_close_input(&in);
    }
    qint64 indexOf(int64_t ts) const
    {
        return ts == AV_NOPTS_VALUE ? -1 : av_rescale_q_rnd(ts, tb, av_inv_q(rate), AV_ROUND_NEAR_INF);
    }
    // Decodes until frame `index` is in `frame`.
    bool decodeTo(qint64 index)
    {
        if (next < 0 || index < next || index > next + 25) {
            const int64_t ts = av_rescale_q(index, av_inv_q(rate), tb);
            if (av_seek_frame(in, stream, ts, AVSEEK_FLAG_BACKWARD) < 0)
                return false;
            avcodec_flush_buffers(dc);
            next = -1;
        }
        while (true) {
            int rc = avcodec_receive_frame(dc, frame);
            if (rc >= 0) {
                const qint64 got = indexOf(frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp
                                                                                         : frame->pts);
                next = got + 1;
                if (got >= index)
                    return true;
                continue;
            }
            if (rc != AVERROR(EAGAIN))
                return false;
            rc = av_read_frame(in, pkt);
            if (rc < 0) {
                avcodec_send_packet(dc, nullptr); // drain the last frames
                if (avcodec_receive_frame(dc, frame) >= 0) {
                    next = -1;
                    return true;
                }
                return false;
            }
            if (pkt->stream_index == stream)
                avcodec_send_packet(dc, pkt);
            av_packet_unref(pkt);
        }
    }
};

Reader::Reader() = default;
Reader::~Reader() = default;

bool Reader::open(const QString &path, QString *error)
{
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        d.reset();
        return false;
    };
    d = std::make_unique<Impl>();
    int rc;
    if ((rc = avformat_open_input(&d->in, QFile::encodeName(path).constData(), nullptr, nullptr)) < 0)
        return fail(averr(rc));
    if ((rc = avformat_find_stream_info(d->in, nullptr)) < 0)
        return fail(averr(rc));
    const AVCodec *dec = nullptr;
    d->stream = av_find_best_stream(d->in, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (d->stream < 0 || !dec)
        return fail(QStringLiteral("no picture"));
    AVStream *st = d->in->streams[d->stream];
    d->tb = st->time_base;
    d->rate = st->avg_frame_rate.num > 0 ? st->avg_frame_rate : st->r_frame_rate;
    if (d->rate.num <= 0 || d->rate.den <= 0)
        d->rate = AVRational{25, 1};
    d->dc = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(d->dc, st->codecpar);
    d->dc->thread_count = 1; // frames are asked for one by one
    if ((rc = avcodec_open2(d->dc, dec, nullptr)) < 0)
        return fail(averr(rc));
    d->size = QSize(d->dc->width, d->dc->height);
    const int64_t dur = st->duration != AV_NOPTS_VALUE ? av_rescale_q(st->duration, st->time_base, AVRational{1, AV_TIME_BASE})
                                                       : d->in->duration;
    d->count = dur > 0 ? av_rescale_q_rnd(dur, AVRational{1, AV_TIME_BASE}, av_inv_q(d->rate), AV_ROUND_NEAR_INF) : 0;
    d->frame = av_frame_alloc();
    d->pkt = av_packet_alloc();
    return true;
}

qint64 Reader::frameCount() const
{
    return d ? d->count : 0;
}

QSize Reader::size() const
{
    return d ? d->size : QSize();
}

QImage Reader::frame(qint64 index, const QSize &wanted)
{
    if (!d)
        return {};
    QMutexLocker lock(&d->mutex);
    if (!d->decodeTo(std::clamp<qint64>(index, 0, std::max<qint64>(0, d->count - 1))))
        return {};
    const QSize out = wanted.isEmpty() ? QSize(d->frame->width, d->frame->height) : wanted;
    if (!d->sws || d->swsSize != out) {
        sws_freeContext(d->sws);
        d->sws = sws_getContext(d->frame->width, d->frame->height, AVPixelFormat(d->frame->format), out.width(),
                                out.height(), AV_PIX_FMT_RGBA, SWS_BICUBIC, nullptr, nullptr, nullptr);
        if (!d->sws)
            return {};
        // The inverse of Writer: BT.601 video range in, full-range RGB out.
        const int *coef = sws_getCoefficients(SWS_CS_ITU601);
        sws_setColorspaceDetails(d->sws, coef, 0, coef, 1, 0, 1 << 16, 1 << 16);
        d->swsSize = out;
    }
    QImage img(out, QImage::Format_RGBA8888);
    uint8_t *dst[1] = {img.bits()};
    const int stride[1] = {int(img.bytesPerLine())};
    sws_scale(d->sws, d->frame->data, d->frame->linesize, 0, d->frame->height, dst, stride);
    return img;
}

} // namespace bc::RenderPreview
