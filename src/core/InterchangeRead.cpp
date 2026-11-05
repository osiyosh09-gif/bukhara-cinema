// Reading timelines of other editors (phase 15b): Final Cut Pro 7 XML,
// OpenTimelineIO JSON and CMX 3600 EDL. The result is a flat list of clips
// in sequence frames; ProjectController turns it into a sequence.
#include "core/Interchange.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace bc {
namespace Interchange {

namespace {

using Kind = TimelineTrack::Kind;

void addLoss(ReadResult &r, ImportLoss l, int n = 1)
{
    if (n > 0)
        r.losses[l] += n;
}

// A local path from a file URL or a plain path. "file:///C:/x" and
// "file://localhost/C%3a/x" give "C:/x" on every system.
QString localPath(const QString &ref, const QString &baseDir)
{
    QString s = ref.trimmed();
    if (s.isEmpty())
        return {};
    if (s.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
        QUrl u(s);
        if (u.host().compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
            u.setHost(QString());
        s = u.toLocalFile();
        if (s.isEmpty())
            s = QUrl::fromPercentEncoding(ref.mid(ref.indexOf(QLatin1Char(':')) + 1).toUtf8());
    }
    s = QDir::fromNativeSeparators(s);
    while (s.startsWith(QLatin1String("//localhost/")))
        s = s.mid(11);
    // "/C:/x" -> "C:/x"
    if (s.size() >= 3 && s[0] == QLatin1Char('/') && s[1].isLetter() && s[2] == QLatin1Char(':'))
        s = s.mid(1);
    const bool drive = s.size() >= 2 && s[0].isLetter() && s[1] == QLatin1Char(':');
    if (!drive && QDir::isRelativePath(s) && !baseDir.isEmpty())
        s = QDir(baseDir).absoluteFilePath(s);
    return QDir::cleanPath(s);
}

// 29.97 -> 30000/1001, 25 -> 25/1, 23.976 -> 24000/1001
void rateFromDouble(double fps, int *num, int *den)
{
    if (fps <= 0) {
        *num = 0;
        *den = 1;
        return;
    }
    const long whole = std::lround(fps);
    const double ntsc = whole * 1000.0 / 1001.0;
    if (std::abs(fps - whole) < 1e-6) {
        *num = int(whole);
        *den = 1;
    } else if (std::abs(fps - ntsc) < 0.005) {
        *num = int(whole * 1000);
        *den = 1001;
    } else {
        *num = int(std::lround(fps * 1000));
        *den = 1000;
    }
}

// Sorts, removes empty clips, trims overlaps, counts tracks, moves
// everything to start at 0 or later.
void finish(ReadResult &r)
{
    QList<ImportClip> &cs = r.clips;
    cs.erase(std::remove_if(cs.begin(), cs.end(), [](const ImportClip &c) { return c.length <= 0; }), cs.end());
    qint64 first = 0;
    for (const ImportClip &c : cs)
        first = std::min(first, c.start);
    for (const ImportMarker &m : r.markers)
        first = std::min(first, m.frame);
    if (first < 0) {
        for (ImportClip &c : cs)
            c.start -= first;
        for (ImportMarker &m : r.markers)
            m.frame -= first;
    }
    std::stable_sort(cs.begin(), cs.end(), [](const ImportClip &a, const ImportClip &b) {
        if (a.kind != b.kind)
            return a.kind == Kind::Video;
        if (a.track != b.track)
            return a.track < b.track;
        return a.start < b.start;
    });
    for (int i = 1; i < cs.size(); ++i) {
        ImportClip &prev = cs[i - 1];
        const ImportClip &cur = cs[i];
        if (prev.kind == cur.kind && prev.track == cur.track && prev.end() > cur.start)
            prev.length = cur.start - prev.start;
    }
    cs.erase(std::remove_if(cs.begin(), cs.end(), [](const ImportClip &c) { return c.length <= 0; }), cs.end());
    for (ImportClip &c : cs)
        c.in = std::max<qint64>(0, c.in);
    r.videoTracks = r.audioTracks = 0;
    for (const ImportClip &c : cs) {
        if (c.kind == Kind::Video)
            r.videoTracks = std::max(r.videoTracks, c.track + 1);
        else
            r.audioTracks = std::max(r.audioTracks, c.track + 1);
    }
    std::sort(r.markers.begin(), r.markers.end(),
              [](const ImportMarker &a, const ImportMarker &b) { return a.frame < b.frame; });
    if (cs.isEmpty() && r.error == ReadResult::Error::None)
        r.error = ReadResult::Error::Empty;
}

// Renumbers the tracks of a kind so that tracks without clips disappear.
void compactTracks(ReadResult &r, Kind kind)
{
    QSet<int> used;
    for (const ImportClip &c : r.clips)
        if (c.kind == kind)
            used.insert(c.track);
    QList<int> order(used.begin(), used.end());
    std::sort(order.begin(), order.end());
    for (ImportClip &c : r.clips)
        if (c.kind == kind)
            c.track = int(order.indexOf(c.track));
}

// Transitions found on one track, attached once all its clips are known
// (positions in the file's frames).
struct Cut
{
    qint64 cut = 0;
    qint64 length = 0;
    bool black = false; // fade to / from black at a free end
};

void attachCuts(QList<ImportClip> &clips, const QList<Cut> &cuts)
{
    for (const Cut &t : cuts) {
        ImportClip *a = nullptr, *b = nullptr;
        for (ImportClip &c : clips) {
            if (c.start == t.cut)
                b = &c;
            if (c.end() == t.cut)
                a = &c;
        }
        const ClipTransition::Kind freeKind =
            t.black && b && b->kind == Kind::Video ? ClipTransition::Kind::DipBlack : ClipTransition::Kind::Dissolve;
        if (a && b) {
            b->head = {ClipTransition::Kind::Dissolve, t.length};
        } else if (b) {
            b->head = {freeKind, t.length};
        } else if (a) {
            a->tail = {t.black && a->kind == Kind::Video ? ClipTransition::Kind::DipBlack : ClipTransition::Kind::Dissolve,
                       t.length};
        }
    }
}

// Groups of linked clips -> link numbers (union-find over item keys).
struct Links
{
    QHash<QString, QString> parent;
    QString find(const QString &k)
    {
        QString p = parent.value(k, k);
        if (p == k)
            return k;
        const QString root = find(p);
        parent.insert(k, root);
        return root;
    }
    void join(const QString &a, const QString &b)
    {
        const QString ra = find(a), rb = find(b);
        if (ra != rb)
            parent.insert(ra, rb);
    }
};

// ---------------------------------------------------------------------------
// Final Cut Pro 7 XML

struct XNode
{
    QString name;
    QString text;
    QHash<QString, QString> attrs;
    std::vector<XNode> kids;

    const XNode *child(const char *n) const
    {
        for (const XNode &k : kids)
            if (k.name == QLatin1String(n))
                return &k;
        return nullptr;
    }
    QString value(const char *n) const
    {
        const XNode *c = child(n);
        return c ? c->text.trimmed() : QString();
    }
    qint64 number(const char *n, qint64 fallback) const
    {
        bool ok = false;
        const qint64 v = value(n).toLongLong(&ok);
        return ok ? v : fallback;
    }
};

void readNode(QXmlStreamReader &x, XNode &node)
{
    node.name = x.name().toString();
    for (const QXmlStreamAttribute &a : x.attributes())
        node.attrs.insert(a.name().toString(), a.value().toString());
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement()) {
            node.kids.emplace_back();
            readNode(x, node.kids.back());
        } else if (x.isCharacters()) {
            node.text += x.text();
        } else if (x.isEndElement()) {
            return;
        }
    }
}

void collect(const XNode &n, const char *name, std::vector<const XNode *> &out)
{
    if (n.name == QLatin1String(name))
        out.push_back(&n);
    for (const XNode &k : n.kids)
        collect(k, name, out);
}

// <rate><timebase>30</timebase><ntsc>TRUE</ntsc></rate> -> 30000/1001
bool xmlRate(const XNode *rate, int *num, int *den)
{
    if (!rate)
        return false;
    const qint64 tb = rate->number("timebase", 0);
    if (tb <= 0)
        return false;
    if (rate->value("ntsc").compare(QLatin1String("TRUE"), Qt::CaseInsensitive) == 0) {
        *num = int(tb * 1000);
        *den = 1001;
    } else {
        *num = int(tb);
        *den = 1;
    }
    return true;
}

bool isDissolve(const XNode *effect)
{
    if (!effect)
        return true;
    const QString s = effect->value("name") + QLatin1Char(' ') + effect->value("effectid");
    return s.contains(QLatin1String("Dissolve"), Qt::CaseInsensitive)
           || s.contains(QLatin1String("Cross Fade"), Qt::CaseInsensitive)
           || s.contains(QLatin1String("CrossFade"), Qt::CaseInsensitive)
           || s.contains(QLatin1String("Constant Power"), Qt::CaseInsensitive)
           || s.contains(QLatin1String("Constant Gain"), Qt::CaseInsensitive)
           || s.contains(QLatin1String("Exponential Fade"), Qt::CaseInsensitive)
           || s.contains(QLatin1String("KGAudioTrans"), Qt::CaseInsensitive);
}

ReadResult readXml(const QByteArray &data, const SequenceSettings &seq, const QString &baseDir)
{
    ReadResult r;
    r.format = Format::FcpXml;
    QXmlStreamReader x(data);
    XNode root;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement()) {
            readNode(x, root);
            break;
        }
    }
    if (x.hasError() || root.name != QLatin1String("xmeml")) {
        r.error = ReadResult::Error::NotRecognized;
        return r;
    }
    // The shallowest <sequence> with <media> (nested ones sit deeper, in clip items).
    const XNode *sequence = nullptr;
    std::vector<const XNode *> level{&root};
    while (!level.empty() && !sequence) {
        std::vector<const XNode *> next;
        for (const XNode *n : level) {
            if (n->name == QLatin1String("sequence") && n->child("media")) {
                sequence = n;
                break;
            }
            for (const XNode &k : n->kids)
                next.push_back(&k);
        }
        level.swap(next);
    }
    if (!sequence) {
        r.error = ReadResult::Error::NotRecognized;
        return r;
    }
    r.name = sequence->value("name");
    int num = seq.fpsNum, den = seq.fpsDen;
    if (xmlRate(sequence->child("rate"), &num, &den)) {
        r.fpsNum = num;
        r.fpsDen = den;
    }
    const double fileFps = double(num) / den;
    auto conv = [&](qint64 f, double fps) { return qint64(std::llround(double(f) / fps * seq.fps())); };

    // Files are described once and then referred to by id.
    struct FileRef { QString path, name; };
    QHash<QString, FileRef> files;
    std::vector<const XNode *> fileNodes;
    collect(*sequence, "file", fileNodes);
    for (const XNode *f : fileNodes) {
        const QString id = f->attrs.value(QStringLiteral("id"));
        if (!f->child("pathurl") && !f->child("name"))
            continue;
        FileRef ref{localPath(f->value("pathurl"), baseDir), f->value("name")};
        if (!id.isEmpty() && !files.contains(id))
            files.insert(id, ref);
    }

    // Full definitions of items that later appear as bare references.
    QHash<QString, const XNode *> defined;
    for (const char *tag : {"clipitem", "transitionitem", "generatoritem"}) {
        std::vector<const XNode *> nodes;
        collect(*sequence, tag, nodes);
        for (const XNode *n : nodes) {
            const QString id = n->attrs.value(QStringLiteral("id"));
            const QString key = n->name + QLatin1Char('#') + id;
            if (!id.isEmpty() && !n->kids.empty() && !defined.contains(key))
                defined.insert(key, n);
        }
    }

    const XNode *media = sequence->child("media");
    if (const XNode *video = media->child("video"))
        if (const XNode *fmt = video->child("format"))
            if (const XNode *sc = fmt->child("samplecharacteristics")) {
                r.width = int(sc->number("width", 0));
                r.height = int(sc->number("height", 0));
            }

    Links links;
    QHash<int, QString> keyOf;       // index in r.clips -> clip item id
    QHash<int, int> sourceTrack;     // index in r.clips -> sourcetrack/trackindex (audio channel)
    for (Kind kind : {Kind::Video, Kind::Audio}) {
        const XNode *part = media->child(kind == Kind::Video ? "video" : "audio");
        if (!part)
            continue;
        int trackNo = -1;
        for (const XNode &track : part->kids) {
            if (track.name != QLatin1String("track"))
                continue;
            ++trackNo;
            QList<ImportClip> clips;
            QList<QString> keys;
            QList<int> channels;
            QList<Cut> cuts;
            // Cut points of the transitions on this track, in item order.
            struct Trans { qint64 s, e, cut; };
            const std::vector<XNode> &items = track.kids;
            // An item equal to one written before may be only a reference:
            // <clipitem id="clipitem-1"/>. Use the full one it names.
            auto full = [&](size_t i) -> const XNode & {
                const XNode &n = items[i];
                if (!n.kids.empty())
                    return n;
                const XNode *def = defined.value(n.name + QLatin1Char('#') + n.attrs.value(QStringLiteral("id")));
                return def ? *def : n;
            };
            auto transAt = [&](size_t i, Trans *t) {
                if (i >= items.size() || items[i].name != QLatin1String("transitionitem"))
                    return false;
                const XNode &n = full(i);
                t->s = n.number("start", 0);
                t->e = n.number("end", t->s);
                const QString align = n.value("alignment");
                t->cut = align.startsWith(QLatin1String("start")) ? t->s
                         : align.startsWith(QLatin1String("end")) ? t->e
                                                                  : (t->s + t->e) / 2;
                return true;
            };
            for (size_t i = 0; i < items.size(); ++i) {
                const XNode &it = full(i);
                if (it.name == QLatin1String("transitionitem")) {
                    Trans t;
                    transAt(i, &t);
                    const XNode *effect = it.child("effect");
                    if (kind == Kind::Video && !isDissolve(effect))
                        addLoss(r, ImportLoss::TransitionKinds);
                    cuts.append({t.cut, t.e - t.s, it.value("alignment").endsWith(QLatin1String("black"))});
                    continue;
                }
                if (it.name == QLatin1String("generatoritem")) {
                    addLoss(r, ImportLoss::Generators);
                    continue;
                }
                if (it.name != QLatin1String("clipitem"))
                    continue;
                if (it.child("sequence") || it.child("multiclip")) {
                    addLoss(r, ImportLoss::Nested);
                    continue;
                }
                if (it.value("enabled").compare(QLatin1String("FALSE"), Qt::CaseInsensitive) == 0) {
                    addLoss(r, ImportLoss::DisabledClips);
                    continue;
                }
                const XNode *file = it.child("file");
                const FileRef ref = file ? files.value(file->attrs.value(QStringLiteral("id"))) : FileRef();
                FileRef use = ref;
                if (file && use.path.isEmpty() && use.name.isEmpty())
                    use = {localPath(file->value("pathurl"), baseDir), file->value("name")};
                if (use.path.isEmpty() && use.name.isEmpty()) {
                    addLoss(r, ImportLoss::NoMedia);
                    continue;
                }
                qint64 start = it.number("start", -1), end = it.number("end", -1);
                qint64 in = it.number("in", 0);
                Trans t;
                if (start < 0) { // begins in a transition: in counts from the transition start
                    if (i > 0 && transAt(i - 1, &t)) {
                        start = t.cut;
                        in += t.cut - t.s;
                    } else {
                        start = clips.isEmpty() ? 0 : clips.last().end();
                    }
                }
                if (end < 0) {
                    if (transAt(i + 1, &t))
                        end = t.cut;
                    else
                        end = start + std::max<qint64>(0, it.number("out", in) - it.number("in", 0));
                }
                int cn = num, cd = den;
                xmlRate(it.child("rate"), &cn, &cd);
                const double clipFps = double(cn) / cd;

                ImportClip c;
                c.kind = kind;
                c.track = trackNo;
                c.path = use.path;
                c.name = it.value("name").isEmpty() ? use.name : it.value("name");
                c.start = start; // converted below, after the cuts are attached
                c.length = end - start;
                c.in = conv(in, clipFps);
                bool speed = false, effects = false;
                for (const XNode &f : it.kids) {
                    if (f.name != QLatin1String("filter"))
                        continue;
                    if (f.value("enabled").compare(QLatin1String("FALSE"), Qt::CaseInsensitive) == 0)
                        continue;
                    const XNode *e = f.child("effect");
                    if (!e)
                        continue;
                    const QString id = e->value("effectid"), name = e->value("name");
                    if (id.compare(QLatin1String("audiolevels"), Qt::CaseInsensitive) == 0
                        || name.compare(QLatin1String("Audio Levels"), Qt::CaseInsensitive) == 0) {
                        for (const XNode &p : e->kids) {
                            if (p.name != QLatin1String("parameter"))
                                continue;
                            QString v = p.value("value");
                            if (v.isEmpty())
                                if (const XNode *kf = p.child("keyframe"))
                                    v = kf->value("value");
                            bool ok = false;
                            const double level = v.toDouble(&ok);
                            if (ok && level > 0)
                                c.gainDb += 20.0 * std::log10(level);
                            break;
                        }
                    } else if (id.compare(QLatin1String("timeremap"), Qt::CaseInsensitive) == 0
                               || name.contains(QLatin1String("Time Remap"), Qt::CaseInsensitive)) {
                        for (const XNode &p : e->kids)
                            if (p.name == QLatin1String("parameter")
                                && p.value("parameterid").compare(QLatin1String("speed"), Qt::CaseInsensitive) == 0
                                && std::abs(p.value("value").toDouble() - 100.0) > 1e-6)
                                speed = true;
                    } else if (id.compare(QLatin1String("audiopan"), Qt::CaseInsensitive) != 0) {
                        effects = true;
                    }
                }
                if (speed)
                    addLoss(r, ImportLoss::Speed);
                if (effects)
                    addLoss(r, ImportLoss::Effects);
                QString key = it.attrs.value(QStringLiteral("id"));
                if (key.isEmpty())
                    key = QStringLiteral("#%1").arg(r.clips.size() + clips.size());
                for (const XNode &l : it.kids)
                    if (l.name == QLatin1String("link")) {
                        const QString other = l.value("linkclipref");
                        if (!other.isEmpty())
                            links.join(key, other);
                    }
                int channel = 1;
                if (const XNode *st = it.child("sourcetrack"))
                    channel = int(st->number("trackindex", 1));
                clips.append(c);
                keys.append(key);
                channels.append(channel);
            }
            attachCuts(clips, cuts);
            for (int i = 0; i < clips.size(); ++i) {
                ImportClip c = clips[i];
                const qint64 s = conv(c.start, fileFps), e = conv(c.end(), fileFps);
                c.start = s;
                c.length = e - s;
                c.head.length = conv(c.head.length, fileFps);
                c.tail.length = conv(c.tail.length, fileFps);
                keyOf.insert(int(r.clips.size()), keys[i]);
                sourceTrack.insert(int(r.clips.size()), channels[i]);
                r.clips.append(c);
            }
        }
    }

    // A stereo file is often laid out as two mono clips (channel 1 on A1,
    // channel 2 on A2); here a clip plays the whole file, so the second copy goes.
    QList<bool> drop(r.clips.size(), false);
    for (int i = 0; i < r.clips.size(); ++i) {
        const ImportClip &a = r.clips[i];
        if (a.kind != Kind::Audio || sourceTrack.value(i) < 2)
            continue;
        for (int j = 0; j < r.clips.size(); ++j) {
            const ImportClip &b = r.clips[j];
            if (j != i && !drop[j] && b.kind == Kind::Audio && b.path == a.path && b.name == a.name
                && b.start == a.start && b.length == a.length && b.in == a.in && sourceTrack.value(j) < sourceTrack.value(i)) {
                drop[i] = true;
                break;
            }
        }
    }
    QHash<QString, int> groupNo;
    QHash<QString, int> groupSize;
    QList<ImportClip> kept;
    QList<QString> keptKeys;
    for (int i = 0; i < r.clips.size(); ++i) {
        if (drop[i])
            continue;
        kept.append(r.clips[i]);
        keptKeys.append(links.find(keyOf.value(i)));
        groupSize[keptKeys.last()] += 1;
    }
    for (int i = 0; i < kept.size(); ++i) {
        if (groupSize.value(keptKeys[i]) < 2)
            continue;
        if (!groupNo.contains(keptKeys[i]))
            groupNo.insert(keptKeys[i], int(groupNo.size()));
        kept[i].link = groupNo.value(keptKeys[i]);
    }
    r.clips = kept;
    compactTracks(r, Kind::Audio);

    for (const XNode &m : sequence->kids)
        if (m.name == QLatin1String("marker")) {
            const qint64 in = m.number("in", -1);
            if (in >= 0)
                r.markers.append({conv(in, fileFps), m.value("name")});
        }
    finish(r);
    return r;
}

// ---------------------------------------------------------------------------
// OpenTimelineIO (JSON)

QString schemaName(const QJsonObject &o)
{
    const QString s = o.value(QStringLiteral("OTIO_SCHEMA")).toString();
    return s.left(s.indexOf(QLatin1Char('.')));
}

struct Span
{
    double start = 0, duration = 0; // seconds
    bool ok = false;
};

double seconds(const QJsonValue &rt, double *rate = nullptr)
{
    const QJsonObject o = rt.toObject();
    const double r = o.value(QStringLiteral("rate")).toDouble();
    if (rate && r > 0 && *rate <= 0)
        *rate = r;
    return r > 0 ? o.value(QStringLiteral("value")).toDouble() / r : 0.0;
}

Span span(const QJsonValue &range, double *rate = nullptr)
{
    Span s;
    if (!range.isObject())
        return s;
    const QJsonObject o = range.toObject();
    s.start = seconds(o.value(QStringLiteral("start_time")), rate);
    s.duration = seconds(o.value(QStringLiteral("duration")), rate);
    s.ok = true;
    return s;
}

QJsonObject activeReference(const QJsonObject &clip)
{
    if (clip.value(QStringLiteral("media_reference")).isObject())
        return clip.value(QStringLiteral("media_reference")).toObject();
    const QJsonObject refs = clip.value(QStringLiteral("media_references")).toObject();
    QString key = clip.value(QStringLiteral("active_media_reference_key")).toString();
    if (key.isEmpty())
        key = QStringLiteral("DEFAULT_MEDIA");
    if (refs.value(key).isObject())
        return refs.value(key).toObject();
    return refs.isEmpty() ? QJsonObject() : refs.begin().value().toObject();
}

double otioDuration(const QJsonObject &o)
{
    const Span s = span(o.value(QStringLiteral("source_range")));
    if (s.ok)
        return s.duration;
    const QString sc = schemaName(o);
    if (sc == QLatin1String("Clip"))
        return span(activeReference(o).value(QStringLiteral("available_range"))).duration;
    if (sc == QLatin1String("Track") || sc == QLatin1String("Sequence") || sc == QLatin1String("Stack")) {
        double sum = 0, longest = 0;
        for (const QJsonValue &v : o.value(QStringLiteral("children")).toArray()) {
            const QJsonObject k = v.toObject();
            if (schemaName(k) == QLatin1String("Transition"))
                continue;
            const double d = otioDuration(k);
            sum += d;
            longest = std::max(longest, d);
        }
        return sc == QLatin1String("Stack") ? longest : sum;
    }
    return 0;
}

ReadResult readOtio(const QByteArray &data, const SequenceSettings &seq, const QString &baseDir)
{
    ReadResult r;
    r.format = Format::Otio;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        r.error = ReadResult::Error::NotRecognized;
        return r;
    }
    QJsonObject top = doc.object();
    if (schemaName(top) == QLatin1String("SerializableCollection")) {
        for (const QJsonValue &v : top.value(QStringLiteral("children")).toArray())
            if (schemaName(v.toObject()) == QLatin1String("Timeline")) {
                top = v.toObject();
                break;
            }
    }
    QJsonObject stack;
    if (schemaName(top) == QLatin1String("Timeline")) {
        r.name = top.value(QStringLiteral("name")).toString();
        stack = top.value(QStringLiteral("tracks")).toObject();
    } else if (schemaName(top) == QLatin1String("Stack")) {
        stack = top;
    } else if (schemaName(top) == QLatin1String("Track") || schemaName(top) == QLatin1String("Sequence")) {
        stack.insert(QStringLiteral("children"), QJsonArray{top});
    } else {
        r.error = ReadResult::Error::NotRecognized;
        return r;
    }
    double rate = 0;
    auto toSeq = [&](double sec) { return qint64(std::llround(sec * seq.fps())); };
    auto markersOf = [&](const QJsonObject &o) {
        for (const QJsonValue &v : o.value(QStringLiteral("markers")).toArray()) {
            const QJsonObject m = v.toObject();
            QJsonValue range = m.value(QStringLiteral("marked_range"));
            if (!range.isObject())
                range = m.value(QStringLiteral("range")); // Marker.1
            const Span s = span(range, &rate);
            if (s.ok)
                r.markers.append({toSeq(s.start), m.value(QStringLiteral("name")).toString()});
        }
    };
    markersOf(stack);

    int nVideo = 0, nAudio = 0;
    for (const QJsonValue &tv : stack.value(QStringLiteral("children")).toArray()) {
        const QJsonObject track = tv.toObject();
        const QString sc = schemaName(track);
        if (sc != QLatin1String("Track") && sc != QLatin1String("Sequence")) {
            addLoss(r, ImportLoss::Nested);
            continue;
        }
        const QString kindName = track.value(QStringLiteral("kind")).toString();
        Kind kind;
        int trackNo;
        if (kindName == QLatin1String("Video")) {
            kind = Kind::Video;
            trackNo = nVideo++;
        } else if (kindName == QLatin1String("Audio")) {
            kind = Kind::Audio;
            trackNo = nAudio++;
        } else {
            continue;
        }
        markersOf(track);
        double pos = 0;
        int prevClip = -1;          // index in r.clips of the clip ending at pos, or -1
        double pending = -1;        // length of a transition waiting for the next clip
        int pendingFrom = -1;
        for (const QJsonValue &cv : track.value(QStringLiteral("children")).toArray()) {
            const QJsonObject item = cv.toObject();
            const QString is = schemaName(item);
            if (is == QLatin1String("Transition")) {
                const double tin = seconds(item.value(QStringLiteral("in_offset")), &rate);
                const double tout = seconds(item.value(QStringLiteral("out_offset")), &rate);
                if (kind == Kind::Video
                    && item.value(QStringLiteral("transition_type")).toString() != QLatin1String("SMPTE_Dissolve"))
                    addLoss(r, ImportLoss::TransitionKinds);
                pending = tin + tout;
                pendingFrom = prevClip;
                continue;
            }
            const double dur = otioDuration(item);
            const bool enabled = item.value(QStringLiteral("enabled")).toBool(true);
            if (is != QLatin1String("Clip")) {
                if (is == QLatin1String("Stack") || is == QLatin1String("Track"))
                    addLoss(r, ImportLoss::Nested);
                pos += dur;
                prevClip = -1;
                continue;
            }
            if (!enabled) {
                addLoss(r, ImportLoss::DisabledClips);
                pos += dur;
                prevClip = -1;
                continue;
            }
            const QJsonObject ref = activeReference(item);
            const QString rs = schemaName(ref);
            Span src = span(item.value(QStringLiteral("source_range")), &rate);
            if (!src.ok)
                src = span(ref.value(QStringLiteral("available_range")), &rate);
            if (rs == QLatin1String("GeneratorReference")) {
                addLoss(r, ImportLoss::Generators);
                pos += dur;
                prevClip = -1;
                continue;
            }
            ImportClip c;
            c.kind = kind;
            c.track = trackNo;
            c.name = item.value(QStringLiteral("name")).toString();
            if (rs == QLatin1String("ExternalReference"))
                c.path = localPath(ref.value(QStringLiteral("target_url")).toString(), baseDir);
            if (rs == QLatin1String("ImageSequenceReference") || (c.path.isEmpty() && c.name.isEmpty())) {
                addLoss(r, ImportLoss::NoMedia);
                pos += dur;
                prevClip = -1;
                continue;
            }
            bool speed = false, effects = false;
            for (const QJsonValue &ev : item.value(QStringLiteral("effects")).toArray()) {
                const QString es = schemaName(ev.toObject());
                if (es == QLatin1String("LinearTimeWarp") || es == QLatin1String("FreezeFrame")
                    || es == QLatin1String("TimeEffect"))
                    speed = true;
                else
                    effects = true;
            }
            if (speed)
                addLoss(r, ImportLoss::Speed);
            if (effects)
                addLoss(r, ImportLoss::Effects);
            c.gainDb = item.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("bukhara_cinema"))
                           .toObject().value(QStringLiteral("gain_db")).toDouble();
            c.start = toSeq(pos);
            c.length = toSeq(pos + src.duration) - c.start;
            c.in = toSeq(src.start);
            if (pending > 0) {
                const qint64 len = toSeq(pending);
                c.head = {ClipTransition::Kind::Dissolve, len};
                pending = -1;
                (void)pendingFrom;
            }
            pos += src.duration;
            prevClip = int(r.clips.size());
            r.clips.append(c);
        }
        if (pending > 0 && prevClip >= 0)
            r.clips[prevClip].tail = {ClipTransition::Kind::Dissolve, toSeq(pending)};
    }
    rateFromDouble(rate, &r.fpsNum, &r.fpsDen);

    // OTIO has no links: picture and sound of the same placement belong together.
    QHash<QString, int> groups;
    QHash<QString, int> kinds;
    auto keyOf = [](const ImportClip &c) {
        return QStringLiteral("%1|%2|%3|%4").arg(c.path, c.name).arg(c.start).arg(c.in) + QLatin1Char('|')
               + QString::number(c.length);
    };
    for (const ImportClip &c : r.clips)
        kinds[keyOf(c)] |= c.kind == Kind::Video ? 1 : 2;
    for (ImportClip &c : r.clips) {
        const QString k = keyOf(c);
        if (kinds.value(k) != 3)
            continue;
        if (!groups.contains(k))
            groups.insert(k, int(groups.size()));
        c.link = groups.value(k);
    }
    finish(r);
    return r;
}

// ---------------------------------------------------------------------------
// CMX 3600 EDL

ReadResult readEdl(const QByteArray &data, const SequenceSettings &seq, const QString &baseDir)
{
    ReadResult r;
    r.format = Format::Edl;
    const QStringList lines = QString::fromUtf8(data).split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    const int base = timecodeBase(seq.fpsNum, seq.fpsDen);
    bool drop = false;
    static const QRegularExpression event(QStringLiteral(
        R"(^\s*(\d+)\s+(\S+)\s+(\S+)\s+(C|D|W\d+|K\s*[BO]?)\s+(?:(\d+)\s+)?)"
        R"((\d\d[:;.]\d\d[:;.]\d\d[:;.]\d\d)\s+(\d\d[:;.]\d\d[:;.]\d\d[:;.]\d\d)\s+)"
        R"((\d\d[:;.]\d\d[:;.]\d\d[:;.]\d\d)\s+(\d\d[:;.]\d\d[:;.]\d\d[:;.]\d\d)\s*$)"));

    struct Ev
    {
        QString reel, track, trans;
        qint64 transLen = 0;
        QString tc[4];
        QString name, toName, path;
        bool speed = false;
    };
    QList<Ev> evs;
    bool any = false;
    for (const QString &raw : lines) {
        const QString l = raw.trimmed();
        if (l.startsWith(QLatin1String("TITLE:"), Qt::CaseInsensitive)) {
            r.name = l.mid(6).trimmed();
            any = true;
            continue;
        }
        if (l.startsWith(QLatin1String("FCM:"), Qt::CaseInsensitive)) {
            drop = l.contains(QLatin1String("DROP FRAME"), Qt::CaseInsensitive)
                   && !l.contains(QLatin1String("NON"), Qt::CaseInsensitive);
            any = true;
            continue;
        }
        const QRegularExpressionMatch m = event.match(l);
        if (m.hasMatch()) {
            Ev e;
            e.reel = m.captured(2);
            e.track = m.captured(3).toUpper();
            e.trans = m.captured(4).toUpper().remove(QLatin1Char(' '));
            e.transLen = m.captured(5).toLongLong();
            for (int i = 0; i < 4; ++i)
                e.tc[i] = m.captured(6 + i);
            evs.append(e);
            any = true;
            continue;
        }
        if (evs.isEmpty())
            continue;
        Ev &last = evs.last();
        if (l.startsWith(QLatin1String("M2"), Qt::CaseInsensitive)) {
            last.speed = true;
        } else if (l.startsWith(QLatin1Char('*'))) {
            const QString c = l.mid(1).trimmed();
            auto after = [&](const char *key) { return c.mid(int(qstrlen(key))).trimmed(); };
            if (c.startsWith(QLatin1String("FROM CLIP NAME:"), Qt::CaseInsensitive))
                last.name = after("FROM CLIP NAME:");
            else if (c.startsWith(QLatin1String("TO CLIP NAME:"), Qt::CaseInsensitive))
                last.toName = after("TO CLIP NAME:");
            else if (c.startsWith(QLatin1String("SOURCE FILE:"), Qt::CaseInsensitive))
                last.path = after("SOURCE FILE:");
        }
    }
    if (!any) {
        r.error = ReadResult::Error::NotRecognized;
        return r;
    }

    struct Placed { qint64 sIn, sOut, rIn, rOut; };
    QList<Placed> times;
    qint64 firstRec = -1;
    for (const Ev &e : evs) {
        bool df = drop;
        for (const QString &t : e.tc)
            df |= t.contains(QLatin1Char(';'));
        Placed p{parseTimecode(e.tc[0], base, df), parseTimecode(e.tc[1], base, df), parseTimecode(e.tc[2], base, df),
                 parseTimecode(e.tc[3], base, df)};
        times.append(p);
        if (p.rOut > p.rIn && (firstRec < 0 || p.rIn < firstRec))
            firstRec = p.rIn;
    }
    // Record times often start at 01:00:00:00: the timeline starts at the
    // whole hour before the first event.
    const qint64 hour = qint64(base) * 3600;
    const qint64 offset = firstRec > 0 ? (firstRec / hour) * hour : 0;

    int link = 0;
    for (int i = 0; i < evs.size(); ++i) {
        const Ev &e = evs[i];
        const Placed &p = times[i];
        const bool black = e.reel.compare(QLatin1String("BL"), Qt::CaseInsensitive) == 0
                           || e.reel.compare(QLatin1String("BLACK"), Qt::CaseInsensitive) == 0;
        const bool dissolve = e.trans == QLatin1String("D") || e.trans.startsWith(QLatin1Char('W'));
        if (e.trans.startsWith(QLatin1Char('W')))
            addLoss(r, ImportLoss::TransitionKinds);
        if (p.rOut <= p.rIn)
            continue; // the "from" half of a dissolve, zero long
        // Which tracks: V, A, A2, AA, B (= AA/V), AA/V, A/V.
        const QString t = e.track;
        const bool video = t.startsWith(QLatin1Char('V')) || t == QLatin1String("B") || t.contains(QLatin1String("/V"));
        QList<int> audio;
        if (t == QLatin1String("B") || t.startsWith(QLatin1String("AA")))
            audio << 0;
        else if (t.startsWith(QLatin1Char('A'))) {
            bool ok = false;
            const int n = t.mid(1).section(QLatin1Char('/'), 0, 0).toInt(&ok);
            audio << (ok && n >= 1 ? n - 1 : 0);
        }
        const qint64 n = dissolve ? e.transLen : 0;
        auto previousEnding = [&](Kind kind, int track, qint64 at) -> ImportClip * {
            for (int k = int(r.clips.size()) - 1; k >= 0; --k)
                if (r.clips[k].kind == kind && r.clips[k].track == track && r.clips[k].end() == at)
                    return &r.clips[k];
            return nullptr;
        };
        if (black) {
            // Dissolve to black: the clip before fades out over n frames.
            if (n > 0) {
                for (Kind kind : {Kind::Video, Kind::Audio}) {
                    const QList<int> tracks = kind == Kind::Video ? (video ? QList<int>{0} : QList<int>()) : audio;
                    for (int tr : tracks)
                        if (ImportClip *a = previousEnding(kind, tr, p.rIn - offset)) {
                            a->length += n;
                            a->tail = {kind == Kind::Video ? ClipTransition::Kind::DipBlack : ClipTransition::Kind::Dissolve, n};
                        }
                }
            }
            continue;
        }
        QString name = !e.toName.isEmpty() && dissolve ? e.toName : e.name;
        if (name.isEmpty())
            name = e.reel;
        const QString path = e.path.isEmpty() ? QString() : localPath(e.path, baseDir);
        if (e.speed)
            addLoss(r, ImportLoss::Speed);
        const bool both = video && !audio.isEmpty();
        auto put = [&](Kind kind, int track) {
            ImportClip c;
            c.kind = kind;
            c.track = track;
            c.name = name;
            c.path = path;
            c.start = p.rIn - offset;
            c.in = p.sIn;
            c.length = p.rOut - p.rIn;
            if (both)
                c.link = link;
            if (n > 0) {
                ImportClip *a = previousEnding(kind, track, c.start);
                if (a) {
                    // Centre the dissolve on a cut: [rIn, rIn + n) mixes both.
                    const qint64 before = n / 2;
                    a->length += before;
                    c.start += before;
                    c.in += before;
                    c.length -= before;
                    c.head = {ClipTransition::Kind::Dissolve, n};
                } else {
                    c.head = {kind == Kind::Video ? ClipTransition::Kind::DipBlack : ClipTransition::Kind::Dissolve, n};
                }
            }
            r.clips.append(c);
        };
        if (video)
            put(Kind::Video, 0);
        for (int a : audio)
            put(Kind::Audio, a);
        if (both)
            ++link;
    }
    finish(r);
    return r;
}

} // namespace

qint64 parseTimecode(const QString &tc, int base, bool dropFrame)
{
    const QStringList parts = tc.trimmed().split(QRegularExpression(QStringLiteral("[:;.]")));
    if (parts.size() != 4 || base <= 0)
        return -1;
    qint64 v[4];
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        v[i] = parts[i].toLongLong(&ok);
        if (!ok)
            return -1;
    }
    qint64 frames = ((v[0] * 3600 + v[1] * 60 + v[2]) * base) + v[3];
    if (dropFrame && (base == 30 || base == 60)) {
        const qint64 dropPerMinute = base == 30 ? 2 : 4;
        const qint64 minutes = v[0] * 60 + v[1];
        frames -= dropPerMinute * (minutes - minutes / 10);
    }
    return frames;
}

bool formatFromPath(const QString &path, Format *format)
{
    const QString s = QFileInfo(path).suffix().toLower();
    if (s == QLatin1String("xml"))
        *format = Format::FcpXml;
    else if (s == QLatin1String("otio"))
        *format = Format::Otio;
    else if (s == QLatin1String("edl"))
        *format = Format::Edl;
    else
        return false;
    return true;
}

ReadResult read(const QByteArray &data, Format format, const SequenceSettings &seq, const QString &baseDir)
{
    switch (format) {
    case Format::FcpXml: return readXml(data, seq, baseDir);
    case Format::Otio: return readOtio(data, seq, baseDir);
    case Format::Edl: return readEdl(data, seq, baseDir);
    }
    return {};
}

} // namespace Interchange
} // namespace bc
