#include "core/Interchange.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QSet>
#include <QUrl>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bc {
namespace Interchange {

namespace {

using Kind = TimelineTrack::Kind;

// ---------------------------------------------------------------------------
// Flattening: the main sequence as plain clips of media files on tracks.
// Nested and multicamera clips are replaced by the clips inside them (only
// the chosen angle of a multicamera clip). Tracks keep their stacking order:
// everything coming from main track n lies above everything from track n-1.

struct FlatClip
{
    const MediaItem *media = nullptr;
    qint64 start = 0;
    qint64 in = 0;      // first source frame (sequence frames)
    qint64 length = 0;
    double speed = 1.0;
    double gainDb = 0;
    QUuid linkKey;      // clips sharing a key are linked (video + its sound)
    // Transition at the cut with the clip before (same rules as the engine).
    ClipTransition head;
    bool wantsCut = false;  // head was a cut transition in the original sequence
    qint64 before = 0;      // frames of the transition before the cut (resolved later)
    qint64 after = 0;       // frames after the cut

    qint64 end() const { return start + length; }
    bool hasTransition() const { return before + after > 0; }
};

struct FlatTrack
{
    Kind kind = Kind::Video;
    bool enabled = true;
    QString name;
    QList<FlatClip> clips; // sorted by start, never overlapping
};

struct Flat
{
    const Project *project = nullptr;
    QList<FlatTrack> video, audio;
    QMap<Loss, int> losses;
    QSet<QUuid> flattened; // nested placements: a linked picture + sound count once
};

void addLoss(QMap<Loss, int> &losses, Loss l, int n = 1)
{
    if (n > 0)
        losses[l] += n;
}

bool isStill(const MediaItem *m) { return m && m->info.kind == MediaInfo::Kind::Image; }

// Puts the clip on the first track at or above `minTrack` where it fits.
void place(QList<FlatTrack> &tracks, Kind kind, int minTrack, bool enabled, const FlatClip &c)
{
    for (int k = std::max(0, minTrack);; ++k) {
        while (tracks.size() <= k) {
            FlatTrack t;
            t.kind = kind;
            t.enabled = enabled;
            tracks.append(t);
        }
        QList<FlatClip> &clips = tracks[k].clips;
        auto it = std::lower_bound(clips.begin(), clips.end(), c.start,
                                   [](const FlatClip &x, qint64 s) { return x.start < s; });
        const bool freeAfter = it == clips.end() || it->start >= c.end();
        const bool freeBefore = it == clips.begin() || std::prev(it)->end() <= c.start;
        if (freeAfter && freeBefore) {
            clips.insert(it, c);
            return;
        }
    }
}

struct Window
{
    qint64 from = std::numeric_limits<qint64>::min();
    qint64 to = std::numeric_limits<qint64>::max();
};

void emitClip(Flat &f, const Timeline &tl, const TimelineClip &c, Kind kind, qint64 offset, const Window &w,
              int minTrack, bool enabled, double extraGain, const QUuid &outerLink, int depth)
{
    const qint64 s0 = std::max(c.start, w.from), e0 = std::min(c.end(), w.to);
    if (e0 <= s0)
        return;
    const qint64 headCut = s0 - c.start, tailCut = c.end() - e0;
    const bool video = kind == Kind::Video;

    if (video) {
        if (c.isTitle()) {
            addLoss(f.losses, Loss::Titles);
            return;
        }
        if (effectsActive(c.effects))
            addLoss(f.losses, Loss::PictureEffects);
        if (!c.color.isNeutral())
            addLoss(f.losses, Loss::Color);
        if (c.motion.animated() || !c.motion.isIdentity())
            addLoss(f.losses, Loss::Motion);
    } else {
        if (effectsActive(c.effects))
            addLoss(f.losses, Loss::SoundEffects);
        if (c.fadeIn > 0 || c.fadeOut > 0)
            addLoss(f.losses, Loss::SoundFades);
    }

    const bool touchedBefore = tl.touchingBefore(c.id) != nullptr;
    const bool touchedAfter = tl.touchingAfter(c.id) != nullptr;
    if (!c.head.isNone() && headCut == 0 && !touchedBefore)
        addLoss(f.losses, Loss::EndFades);
    if (!c.tail.isNone() && tailCut == 0 && !touchedAfter)
        addLoss(f.losses, Loss::EndFades);

    if (c.isNested()) {
        // The clips inside, cut to the part this clip shows.
        if (!c.head.isNone() && touchedBefore && headCut == 0)
            addLoss(f.losses, Loss::Transitions);
        const NestedSequence *nest = f.project->nest(c.nestId);
        if (!nest || depth >= 8)
            return;
        if (depth == 0)
            f.flattened.insert(c.linkId.isNull() ? c.id : c.linkId);
        const Timeline &inner = nest->timeline;
        const Window iw{c.in + headCut, c.in + headCut + (e0 - s0)};
        const qint64 innerOffset = offset + c.start - c.in;
        QList<int> tracks;
        if (nest->multicam) {
            const int t = inner.indexOf(kind, c.angle);
            if (t >= 0)
                tracks.append(t);
        } else {
            for (int t = 0; t < inner.tracks.size(); ++t)
                if (inner.tracks[t].kind == kind)
                    tracks.append(t);
        }
        for (int j = 0; j < tracks.size(); ++j) {
            const TimelineTrack &tr = inner.tracks[tracks[j]];
            const bool heard = video ? !tr.hidden : inner.isAudible(tracks[j]);
            if (!heard && !nest->multicam)
                continue;
            for (const TimelineClip &ic : tr.clips)
                emitClip(f, inner, ic, kind, innerOffset, iw, minTrack + j, enabled, extraGain + c.gainDb,
                         c.linkId.isNull() ? QUuid() : c.linkId, depth + 1);
        }
        return;
    }

    const MediaItem *media = f.project->item(c.mediaId);
    if (!media)
        return;
    if (c.speed != 1.0)
        addLoss(f.losses, Loss::Speed);

    FlatClip fc;
    fc.media = media;
    fc.start = s0 + offset;
    fc.in = isStill(media) ? 0 : c.in + c.src(headCut);
    fc.length = e0 - s0;
    fc.speed = c.speed;
    fc.gainDb = video ? 0 : c.gainDb + extraGain;
    if (!c.linkId.isNull())
        fc.linkKey = outerLink.isNull() ? c.linkId : QUuid::createUuidV5(outerLink, c.linkId.toString());
    if (!c.head.isNone() && touchedBefore) {
        if (headCut == 0) {
            fc.head = c.head;
            fc.wantsCut = true;
        } else {
            addLoss(f.losses, Loss::Transitions); // the transition lies outside the shown part
        }
    }
    QList<FlatTrack> &out = video ? f.video : f.audio;
    place(out, kind, minTrack, enabled, fc);
}

// Resolves cut transitions like the engine does (TimelinePlayer::buildTractor):
// centred on the cut when both sources have the frames.
void resolveTransitions(Flat &f, QList<FlatTrack> &tracks, const SequenceSettings &seq)
{
    for (FlatTrack &t : tracks) {
        for (int i = 0; i < t.clips.size(); ++i) {
            FlatClip &B = t.clips[i];
            if (!B.wantsCut)
                continue;
            if (i == 0 || t.clips[i - 1].end() != B.start) {
                addLoss(f.losses, Loss::Transitions);
                continue;
            }
            const FlatClip &A = t.clips[i - 1];
            if (A.speed != 1.0 || B.speed != 1.0) {
                addLoss(f.losses, Loss::Transitions);
                continue;
            }
            const qint64 L = std::min({B.head.length, (A.length - 1) / 2, (B.length - 1) / 2});
            const qint64 hB = isStill(B.media) ? L : B.in;
            const qint64 hA = isStill(A.media) ? L : mediaLengthFrames(*A.media, seq) - (A.in + A.length);
            qint64 a = L / 2, b = L - a;
            if (a > hB) {
                a = std::max<qint64>(0, hB);
                b = std::min(L - a, hA);
            }
            if (b > hA) {
                b = std::max<qint64>(0, hA);
                a = std::min(L - b, std::max<qint64>(0, hB));
            }
            if (a + b < 2)
                continue; // the engine plays a plain cut too
            if (t.kind == Kind::Video && B.head.kind != ClipTransition::Kind::Dissolve)
                addLoss(f.losses, Loss::TransitionKinds);
            B.before = a;
            B.after = b;
        }
    }
}

Flat flatten(const Project &p)
{
    Flat f;
    f.project = &p;
    const Timeline &tl = p.timeline;
    for (Kind kind : {Kind::Video, Kind::Audio}) {
        QList<FlatTrack> &out = kind == Kind::Video ? f.video : f.audio;
        for (int t = 0; t < tl.tracks.size(); ++t) {
            const TimelineTrack &tr = tl.tracks[t];
            if (tr.kind != kind)
                continue;
            const int base = out.size();
            const bool enabled = kind == Kind::Video ? !tr.hidden : tl.isAudible(t);
            // Every main track gets at least one output track, even when empty,
            // so V2 stays V2 in the other editor when V1 holds no nests.
            FlatTrack empty;
            empty.kind = kind;
            empty.enabled = enabled;
            out.append(empty);
            for (const TimelineClip &c : tr.clips)
                emitClip(f, tl, c, kind, 0, Window(), base, enabled, 0, QUuid(), 0);
        }
        // Drop empty tracks at the top; keep the ones in between (stacking).
        while (!out.isEmpty() && out.last().clips.isEmpty() && out.size() > 1)
            out.removeLast();
        int n = 0;
        for (FlatTrack &t : out)
            t.name = (kind == Kind::Video ? QStringLiteral("V") : QStringLiteral("A")) + QString::number(++n);
        resolveTransitions(f, out, p.sequence);
    }
    bool mixer = tl.masterGainDb != 0;
    for (const TimelineTrack &t : tl.tracks)
        if (t.kind == Kind::Audio && (t.gainDb != 0 || t.pan != 0))
            mixer = true;
    if (mixer)
        addLoss(f.losses, Loss::Mixer);
    addLoss(f.losses, Loss::Subtitles, int(tl.subtitles.size()));
    return f;
}

qint64 sequenceDuration(const Flat &f)
{
    qint64 d = 0;
    for (const QList<FlatTrack> *tracks : {&f.video, &f.audio})
        for (const FlatTrack &t : *tracks)
            if (!t.clips.isEmpty())
                d = std::max(d, t.clips.last().end());
    return d;
}

QString fileUrl(const QString &path) { return QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded()); }

QString fileName(const MediaItem &m)
{
    const QString n = QFileInfo(m.path).fileName();
    return n.isEmpty() ? m.name : n;
}

// Frames of the media at the sequence rate; stills: as long as they are used.
qint64 mediaFrames(const MediaItem &m, const SequenceSettings &seq, qint64 usedEnd)
{
    const qint64 n = mediaLengthFrames(m, seq);
    return n < 0 ? std::max<qint64>(1, usedEnd) : std::max(n, usedEnd);
}

int countClips(const QList<FlatTrack> &tracks)
{
    int n = 0;
    for (const FlatTrack &t : tracks)
        n += int(t.clips.size());
    return n;
}

void countMissing(Result &r, const Flat &f, bool checkFiles, bool videoOnlyV1 = false)
{
    if (!checkFiles)
        return;
    auto scan = [&](const FlatTrack &t) {
        for (const FlatClip &c : t.clips)
            if (!QFileInfo::exists(c.media->path))
                ++r.missingMedia;
    };
    if (videoOnlyV1) {
        if (!f.video.isEmpty())
            scan(f.video.first());
        return;
    }
    for (const FlatTrack &t : f.video)
        scan(t);
    for (const FlatTrack &t : f.audio)
        scan(t);
}

// ---------------------------------------------------------------------------
// Final Cut Pro 7 XML (xmeml version 5)

void writeRate(QXmlStreamWriter &x, int fpsNum, int fpsDen)
{
    const bool ntsc = fpsDen == 1001 || (fpsDen > 0 && fpsNum % fpsDen != 0);
    x.writeStartElement(QStringLiteral("rate"));
    x.writeTextElement(QStringLiteral("timebase"), QString::number(timecodeBase(fpsNum, fpsDen)));
    x.writeTextElement(QStringLiteral("ntsc"), ntsc ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
    x.writeEndElement();
}

void writeTimecode(QXmlStreamWriter &x, int fpsNum, int fpsDen)
{
    x.writeStartElement(QStringLiteral("timecode"));
    writeRate(x, fpsNum, fpsDen);
    x.writeTextElement(QStringLiteral("string"), QStringLiteral("00:00:00:00"));
    x.writeTextElement(QStringLiteral("frame"), QStringLiteral("0"));
    x.writeTextElement(QStringLiteral("displayformat"), QStringLiteral("NDF"));
    x.writeEndElement();
}

QByteArray writeXml(const Project &p, Flat &f, Result &r)
{
    const SequenceSettings &seq = p.sequence;
    QByteArray out;
    QXmlStreamWriter x(&out);
    x.setAutoFormatting(true);
    x.setAutoFormattingIndent(2);
    x.writeStartDocument();
    x.writeDTD(QStringLiteral("<!DOCTYPE xmeml>"));
    x.writeStartElement(QStringLiteral("xmeml"));
    x.writeAttribute(QStringLiteral("version"), QStringLiteral("5"));
    x.writeStartElement(QStringLiteral("sequence"));
    x.writeAttribute(QStringLiteral("id"), QStringLiteral("sequence-1"));
    x.writeTextElement(QStringLiteral("name"), p.name);
    x.writeTextElement(QStringLiteral("duration"), QString::number(sequenceDuration(f)));
    writeRate(x, seq.fpsNum, seq.fpsDen);
    writeTimecode(x, seq.fpsNum, seq.fpsDen);
    x.writeTextElement(QStringLiteral("in"), QStringLiteral("-1"));
    x.writeTextElement(QStringLiteral("out"), QStringLiteral("-1"));

    // Ids: one file / master clip per media item, one id per clip item.
    QHash<QUuid, int> fileIds;
    struct Ref { QString id; bool video; int track; int index; };
    QHash<QUuid, QList<Ref>> links;
    QHash<const FlatClip *, QString> clipIds;
    int nextClip = 0;
    for (const QList<FlatTrack> *tracks : {&f.video, &f.audio}) {
        for (int t = 0; t < tracks->size(); ++t) {
            const FlatTrack &tr = tracks->at(t);
            for (int i = 0; i < tr.clips.size(); ++i) {
                const FlatClip &c = tr.clips[i];
                const QString id = QStringLiteral("clipitem-%1").arg(++nextClip);
                clipIds.insert(&c, id);
                if (!fileIds.contains(c.media->id))
                    fileIds.insert(c.media->id, int(fileIds.size()) + 1);
                if (!c.linkKey.isNull())
                    links[c.linkKey].append(Ref{id, tracks == &f.video, t + 1, i + 1});
            }
        }
    }
    QHash<QUuid, qint64> usedEnd; // stills: longest use
    for (const QList<FlatTrack> *tracks : {&f.video, &f.audio})
        for (const FlatTrack &tr : *tracks)
            for (const FlatClip &c : tr.clips)
                usedEnd[c.media->id] = std::max(usedEnd.value(c.media->id), c.in + c.length + c.after);
    QSet<QUuid> fileWritten;

    auto writeFile = [&](const MediaItem &m) {
        const QString id = QStringLiteral("file-%1").arg(fileIds.value(m.id));
        x.writeStartElement(QStringLiteral("file"));
        x.writeAttribute(QStringLiteral("id"), id);
        if (fileWritten.contains(m.id)) {
            x.writeEndElement();
            return;
        }
        fileWritten.insert(m.id);
        // The file's own rate when it has one; time values below are in it.
        const bool ownRate = m.info.fpsNum > 0 && m.info.fpsDen > 0 && m.info.kind == MediaInfo::Kind::Video;
        const int rn = ownRate ? m.info.fpsNum : seq.fpsNum, rd = ownRate ? m.info.fpsDen : seq.fpsDen;
        qint64 dur;
        if (isStill(&m))
            dur = std::max<qint64>(1, usedEnd.value(m.id));
        else
            dur = std::max<qint64>(1, qint64(std::llround(double(m.info.durationUs) / 1e6 * rn / rd)));
        x.writeTextElement(QStringLiteral("name"), fileName(m));
        x.writeTextElement(QStringLiteral("pathurl"), fileUrl(m.path));
        writeRate(x, rn, rd);
        x.writeTextElement(QStringLiteral("duration"), QString::number(dur));
        writeTimecode(x, rn, rd);
        x.writeStartElement(QStringLiteral("media"));
        if (m.info.hasVideo() || isStill(&m)) {
            x.writeStartElement(QStringLiteral("video"));
            x.writeStartElement(QStringLiteral("samplecharacteristics"));
            writeRate(x, rn, rd);
            x.writeTextElement(QStringLiteral("width"), QString::number(m.info.width));
            x.writeTextElement(QStringLiteral("height"), QString::number(m.info.height));
            x.writeEndElement();
            x.writeEndElement();
        }
        if (m.info.hasAudio()) {
            x.writeStartElement(QStringLiteral("audio"));
            x.writeStartElement(QStringLiteral("samplecharacteristics"));
            x.writeTextElement(QStringLiteral("depth"), QStringLiteral("16"));
            x.writeTextElement(QStringLiteral("samplerate"), QString::number(m.info.sampleRate > 0 ? m.info.sampleRate : seq.sampleRate));
            x.writeEndElement();
            x.writeTextElement(QStringLiteral("channelcount"), QString::number(m.info.channels));
            x.writeEndElement();
        }
        x.writeEndElement(); // media
        x.writeEndElement(); // file
    };

    auto writeTransition = [&](const FlatClip &B, bool video) {
        x.writeStartElement(QStringLiteral("transitionitem"));
        x.writeTextElement(QStringLiteral("start"), QString::number(B.start - B.before));
        x.writeTextElement(QStringLiteral("end"), QString::number(B.start + B.after));
        x.writeTextElement(QStringLiteral("alignment"), B.before == 0   ? QStringLiteral("start")
                                                        : B.after == 0 ? QStringLiteral("end")
                                                                       : QStringLiteral("center"));
        writeRate(x, seq.fpsNum, seq.fpsDen);
        x.writeStartElement(QStringLiteral("effect"));
        if (video) {
            x.writeTextElement(QStringLiteral("name"), QStringLiteral("Cross Dissolve"));
            x.writeTextElement(QStringLiteral("effectid"), QStringLiteral("Cross Dissolve"));
            x.writeTextElement(QStringLiteral("effectcategory"), QStringLiteral("Dissolve"));
            x.writeTextElement(QStringLiteral("effecttype"), QStringLiteral("transition"));
            x.writeTextElement(QStringLiteral("mediatype"), QStringLiteral("video"));
        } else {
            x.writeTextElement(QStringLiteral("name"), QStringLiteral("Cross Fade (+3dB)"));
            x.writeTextElement(QStringLiteral("effectid"), QStringLiteral("KGAudioTransCrossFade3dB"));
            x.writeTextElement(QStringLiteral("effecttype"), QStringLiteral("transition"));
            x.writeTextElement(QStringLiteral("mediatype"), QStringLiteral("audio"));
        }
        x.writeEndElement(); // effect
        x.writeEndElement(); // transitionitem
    };

    auto writeClip = [&](const FlatTrack &tr, int i, bool video) {
        const FlatClip &c = tr.clips[i];
        const bool nextHasTransition = i + 1 < tr.clips.size() && tr.clips[i + 1].hasTransition();
        const MediaItem &m = *c.media;
        x.writeStartElement(QStringLiteral("clipitem"));
        x.writeAttribute(QStringLiteral("id"), clipIds.value(&c));
        x.writeTextElement(QStringLiteral("masterclipid"), QStringLiteral("masterclip-%1").arg(fileIds.value(m.id)));
        x.writeTextElement(QStringLiteral("name"), m.name.isEmpty() ? fileName(m) : m.name);
        x.writeTextElement(QStringLiteral("enabled"), QStringLiteral("TRUE"));
        x.writeTextElement(QStringLiteral("duration"), QString::number(mediaFrames(m, seq, usedEnd.value(m.id))));
        writeRate(x, seq.fpsNum, seq.fpsDen);
        // -1: the clip begins (ends) in a transition; in/out then include the
        // frames played inside it.
        x.writeTextElement(QStringLiteral("start"), c.hasTransition() ? QStringLiteral("-1") : QString::number(c.start));
        x.writeTextElement(QStringLiteral("end"), nextHasTransition ? QStringLiteral("-1") : QString::number(c.end()));
        const qint64 in = c.in - c.before;
        const qint64 outFrame = c.in + c.length + (nextHasTransition ? tr.clips[i + 1].after : 0);
        x.writeTextElement(QStringLiteral("in"), QString::number(in));
        x.writeTextElement(QStringLiteral("out"), QString::number(outFrame));
        writeFile(m);
        x.writeStartElement(QStringLiteral("sourcetrack"));
        x.writeTextElement(QStringLiteral("mediatype"), video ? QStringLiteral("video") : QStringLiteral("audio"));
        x.writeTextElement(QStringLiteral("trackindex"), QStringLiteral("1"));
        x.writeEndElement();
        if (!video && c.gainDb != 0) {
            const double level = std::min(3.98109, std::pow(10.0, c.gainDb / 20.0));
            x.writeStartElement(QStringLiteral("filter"));
            x.writeStartElement(QStringLiteral("effect"));
            x.writeTextElement(QStringLiteral("name"), QStringLiteral("Audio Levels"));
            x.writeTextElement(QStringLiteral("effectid"), QStringLiteral("audiolevels"));
            x.writeTextElement(QStringLiteral("effectcategory"), QStringLiteral("audiolevels"));
            x.writeTextElement(QStringLiteral("effecttype"), QStringLiteral("audiolevels"));
            x.writeTextElement(QStringLiteral("mediatype"), QStringLiteral("audio"));
            x.writeStartElement(QStringLiteral("parameter"));
            x.writeTextElement(QStringLiteral("parameterid"), QStringLiteral("level"));
            x.writeTextElement(QStringLiteral("name"), QStringLiteral("Level"));
            x.writeTextElement(QStringLiteral("valuemin"), QStringLiteral("0"));
            x.writeTextElement(QStringLiteral("valuemax"), QStringLiteral("3.98109"));
            x.writeTextElement(QStringLiteral("value"), QString::number(level, 'f', 6));
            x.writeEndElement(); // parameter
            x.writeEndElement(); // effect
            x.writeEndElement(); // filter
        }
        const QList<Ref> group = c.linkKey.isNull() ? QList<Ref>() : links.value(c.linkKey);
        if (group.size() > 1) {
            for (const Ref &ref : group) {
                x.writeStartElement(QStringLiteral("link"));
                x.writeTextElement(QStringLiteral("linkclipref"), ref.id);
                x.writeTextElement(QStringLiteral("mediatype"), ref.video ? QStringLiteral("video") : QStringLiteral("audio"));
                x.writeTextElement(QStringLiteral("trackindex"), QString::number(ref.track));
                x.writeTextElement(QStringLiteral("clipindex"), QString::number(ref.index));
                if (!ref.video)
                    x.writeTextElement(QStringLiteral("groupindex"), QStringLiteral("1"));
                x.writeEndElement();
            }
        }
        x.writeEndElement(); // clipitem
    };

    auto writeTracks = [&](const QList<FlatTrack> &tracks, bool video) {
        for (const FlatTrack &tr : tracks) {
            x.writeStartElement(QStringLiteral("track"));
            for (int i = 0; i < tr.clips.size(); ++i) {
                if (tr.clips[i].hasTransition())
                    writeTransition(tr.clips[i], video);
                writeClip(tr, i, video);
            }
            x.writeTextElement(QStringLiteral("enabled"), tr.enabled ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
            x.writeTextElement(QStringLiteral("locked"), QStringLiteral("FALSE"));
            x.writeEndElement();
        }
    };

    x.writeStartElement(QStringLiteral("media"));
    x.writeStartElement(QStringLiteral("video"));
    x.writeStartElement(QStringLiteral("format"));
    x.writeStartElement(QStringLiteral("samplecharacteristics"));
    writeRate(x, seq.fpsNum, seq.fpsDen);
    x.writeTextElement(QStringLiteral("width"), QString::number(seq.width));
    x.writeTextElement(QStringLiteral("height"), QString::number(seq.height));
    x.writeTextElement(QStringLiteral("pixelaspectratio"), QStringLiteral("square"));
    x.writeTextElement(QStringLiteral("fielddominance"), QStringLiteral("none"));
    x.writeEndElement();
    x.writeEndElement(); // format
    writeTracks(f.video, true);
    x.writeEndElement(); // video
    x.writeStartElement(QStringLiteral("audio"));
    x.writeTextElement(QStringLiteral("numOutputChannels"), QString::number(seq.channels));
    x.writeStartElement(QStringLiteral("format"));
    x.writeStartElement(QStringLiteral("samplecharacteristics"));
    x.writeTextElement(QStringLiteral("depth"), QStringLiteral("16"));
    x.writeTextElement(QStringLiteral("samplerate"), QString::number(seq.sampleRate));
    x.writeEndElement();
    x.writeEndElement(); // format
    writeTracks(f.audio, false);
    x.writeEndElement(); // audio
    x.writeEndElement(); // media

    for (const TimelineMarker &mk : p.timeline.markers) {
        x.writeStartElement(QStringLiteral("marker"));
        x.writeTextElement(QStringLiteral("comment"), QString());
        x.writeTextElement(QStringLiteral("name"), mk.name);
        x.writeTextElement(QStringLiteral("in"), QString::number(mk.frame));
        x.writeTextElement(QStringLiteral("out"), QStringLiteral("-1"));
        x.writeEndElement();
    }
    x.writeEndElement(); // sequence
    x.writeEndElement(); // xmeml
    x.writeEndDocument();

    r.clips = countClips(f.video) + countClips(f.audio);
    return out;
}

// ---------------------------------------------------------------------------
// OpenTimelineIO (JSON). Written by hand so every number is a JSON float,
// as the reference implementation writes them.

class Json
{
public:
    QByteArray out;

    // An object as a value (after key(), or as a list element).
    void begin() { value(); out += '{'; m_comma = false; }
    void end() { out += '}'; m_comma = true; }
    void beginList(const char *k) { key(k); value(); out += '['; m_comma = false; }
    void endList() { out += ']'; m_comma = true; }
    void key(const char *k)
    {
        if (m_comma)
            out += ',';
        quote(QString::fromLatin1(k));
        out += ':';
        m_afterKey = true;
        m_comma = false;
    }
    void str(const char *k, const QString &v) { key(k); value(); quote(v); m_comma = true; }
    void num(const char *k, double v) { key(k); value(); number(v); m_comma = true; }
    void boolean(const char *k, bool v) { key(k); value(); out += v ? "true" : "false"; m_comma = true; }
    void null(const char *k) { key(k); value(); out += "null"; m_comma = true; }
    void emptyObject(const char *k) { key(k); value(); out += "{}"; m_comma = true; }
    void emptyList(const char *k) { key(k); value(); out += "[]"; m_comma = true; }

private:
    bool m_comma = false;    // a value was written at this level: the next one needs a comma
    bool m_afterKey = false; // the next value belongs to the key just written

    void value()
    {
        if (m_afterKey) {
            m_afterKey = false;
            return;
        }
        if (m_comma)
            out += ',';
    }
    void quote(const QString &s)
    {
        // Escape first, encode once: characters outside the BMP are two QChars.
        QString e;
        e.reserve(s.size() + 2);
        for (const QChar ch : s) {
            const ushort u = ch.unicode();
            if (u == '"')
                e += QLatin1String("\\\"");
            else if (u == '\\')
                e += QLatin1String("\\\\");
            else if (u == '\n')
                e += QLatin1String("\\n");
            else if (u == '\r')
                e += QLatin1String("\\r");
            else if (u == '\t')
                e += QLatin1String("\\t");
            else if (u < 0x20)
                e += QStringLiteral("\\u%1").arg(u, 4, 16, QLatin1Char('0'));
            else
                e += ch;
        }
        out += '"';
        out += e.toUtf8();
        out += '"';
    }
    void number(double v)
    {
        // Shortest text that reads back as the same double (29.97002997002997).
        QByteArray s = QString::number(v, 'g', QLocale::FloatingPointShortest).toLatin1();
        if (!s.contains('.') && !s.contains('e') && !s.contains("inf") && !s.contains("nan"))
            s += ".0";
        out += s;
    }
};

void rationalTime(Json &j, const char *key, double value, double rate)
{
    j.key(key);
    j.begin();
    j.str("OTIO_SCHEMA", QStringLiteral("RationalTime.1"));
    j.num("rate", rate);
    j.num("value", value);
    j.end();
}

void timeRange(Json &j, const char *key, double start, double duration, double rate)
{
    j.key(key);
    j.begin();
    j.str("OTIO_SCHEMA", QStringLiteral("TimeRange.1"));
    rationalTime(j, "duration", duration, rate);
    rationalTime(j, "start_time", start, rate);
    j.end();
}

void itemCommon(Json &j, const QString &schema, const QString &name)
{
    j.str("OTIO_SCHEMA", schema);
    j.emptyObject("metadata");
    j.str("name", name);
}

QByteArray writeOtio(const Project &p, Flat &f, Result &r)
{
    const SequenceSettings &seq = p.sequence;
    const double rate = seq.fps();
    QHash<QUuid, qint64> usedEnd;
    for (const QList<FlatTrack> *tracks : {&f.video, &f.audio})
        for (const FlatTrack &tr : *tracks)
            for (const FlatClip &c : tr.clips) {
                usedEnd[c.media->id] = std::max(usedEnd.value(c.media->id), c.in + c.length + c.after);
                if (c.gainDb != 0)
                    addLoss(f.losses, Loss::ClipVolume);
            }

    Json j;
    j.begin();
    itemCommon(j, QStringLiteral("Timeline.1"), p.name);
    j.null("global_start_time");
    j.key("tracks");
    j.begin();
    itemCommon(j, QStringLiteral("Stack.1"), QStringLiteral("tracks"));
    j.null("source_range");
    j.emptyList("effects");
    j.beginList("markers");
    for (const TimelineMarker &mk : p.timeline.markers) {
        j.begin();
        itemCommon(j, QStringLiteral("Marker.2"), mk.name);
        j.str("color", QStringLiteral("GREEN"));
        j.str("comment", QString());
        timeRange(j, "marked_range", double(mk.frame), 0, rate);
        j.end();
    }
    j.endList();
    j.boolean("enabled", true);
    j.beginList("children");
    for (const QList<FlatTrack> *tracks : {&f.video, &f.audio}) {
        for (const FlatTrack &tr : *tracks) {
            j.begin();
            itemCommon(j, QStringLiteral("Track.1"), tr.name);
            j.null("source_range");
            j.emptyList("effects");
            j.emptyList("markers");
            j.boolean("enabled", tr.enabled);
            j.str("kind", tr.kind == Kind::Video ? QStringLiteral("Video") : QStringLiteral("Audio"));
            j.beginList("children");
            qint64 pos = 0;
            for (const FlatClip &c : tr.clips) {
                if (c.start > pos) {
                    j.begin();
                    itemCommon(j, QStringLiteral("Gap.1"), QString());
                    timeRange(j, "source_range", 0, double(c.start - pos), rate);
                    j.emptyList("effects");
                    j.emptyList("markers");
                    j.boolean("enabled", true);
                    j.end();
                }
                if (c.hasTransition()) {
                    j.begin();
                    itemCommon(j, QStringLiteral("Transition.1"), QString());
                    j.str("transition_type", QStringLiteral("SMPTE_Dissolve"));
                    rationalTime(j, "in_offset", double(c.before), rate);
                    rationalTime(j, "out_offset", double(c.after), rate);
                    j.end();
                }
                const MediaItem &m = *c.media;
                j.begin();
                j.str("OTIO_SCHEMA", QStringLiteral("Clip.1"));
                if (c.gainDb != 0) {
                    j.key("metadata");
                    j.begin();
                    j.key("bukhara_cinema");
                    j.begin();
                    j.num("gain_db", c.gainDb);
                    j.end();
                    j.end();
                } else {
                    j.emptyObject("metadata");
                }
                j.str("name", m.name.isEmpty() ? fileName(m) : m.name);
                timeRange(j, "source_range", double(c.in), double(c.length), rate);
                j.emptyList("effects");
                j.emptyList("markers");
                j.boolean("enabled", true);
                j.key("media_reference");
                j.begin();
                itemCommon(j, QStringLiteral("ExternalReference.1"), fileName(m));
                timeRange(j, "available_range", 0, double(mediaFrames(m, seq, usedEnd.value(m.id))), rate);
                j.null("available_image_bounds");
                j.str("target_url", fileUrl(m.path));
                j.end();
                j.end();
                pos = c.end();
            }
            j.endList();
            j.end();
        }
    }
    j.endList();
    j.end(); // stack
    j.end(); // timeline
    j.out += '\n';
    r.clips = countClips(f.video) + countClips(f.audio);
    return j.out;
}

// ---------------------------------------------------------------------------
// CMX 3600 EDL: video track V1 only, cuts only.

QByteArray writeEdl(const Project &p, Flat &f, Result &r)
{
    const SequenceSettings &seq = p.sequence;
    const int base = timecodeBase(seq.fpsNum, seq.fpsDen);
    // Sound is not in this EDL at all, so its details are not listed one by one.
    for (Loss l : {Loss::SoundFades, Loss::ClipVolume, Loss::SoundEffects, Loss::Mixer})
        f.losses.remove(l);
    int other = countClips(f.audio);
    for (int t = 1; t < f.video.size(); ++t)
        other += int(f.video[t].clips.size());
    addLoss(f.losses, Loss::OtherTracks, other);
    addLoss(f.losses, Loss::Markers, int(p.timeline.markers.size()));

    QByteArray out;
    auto line = [&](const QString &s) { out += s.toUtf8() + "\r\n"; };
    line(QStringLiteral("TITLE: ") + p.name);
    line(QStringLiteral("FCM: NON-DROP FRAME"));
    line(QString());
    if (!f.video.isEmpty()) {
        const FlatTrack &v1 = f.video.first();
        const int width = std::max(3, int(QString::number(v1.clips.size()).size()));
        int n = 0;
        for (const FlatClip &c : v1.clips) {
            if (c.hasTransition())
                addLoss(f.losses, Loss::Transitions);
            line(QStringLiteral("%1  %2 %3 %4 %5 %6 %7 %8")
                     .arg(++n, width, 10, QLatin1Char('0'))
                     .arg(QStringLiteral("AX"), -8)
                     .arg(QStringLiteral("V"), -5)
                     .arg(QStringLiteral("C"), -8)
                     .arg(timecode(c.in, base), timecode(c.in + c.length, base), timecode(c.start, base),
                          timecode(c.end(), base)));
            line(QStringLiteral("* FROM CLIP NAME: ") + fileName(*c.media));
            line(QStringLiteral("* SOURCE FILE: ") + QDir::toNativeSeparators(c.media->path));
            line(QString());
        }
        r.clips = n;
    }
    return out;
}

} // namespace

QString fileSuffix(Format f)
{
    switch (f) {
    case Format::FcpXml: return QStringLiteral("xml");
    case Format::Otio: return QStringLiteral("otio");
    case Format::Edl: return QStringLiteral("edl");
    }
    return {};
}

int timecodeBase(int fpsNum, int fpsDen)
{
    if (fpsDen <= 0)
        return 25;
    return std::max(1, int(std::lround(double(fpsNum) / fpsDen)));
}

QString timecode(qint64 frame, int base)
{
    if (frame < 0)
        frame = 0;
    const qint64 ff = frame % base;
    const qint64 totalSec = frame / base;
    const qint64 ss = totalSec % 60, mm = (totalSec / 60) % 60, hh = totalSec / 3600;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(hh, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}

Result write(const Project &project, Format format, bool checkFiles)
{
    Result r;
    Flat f = flatten(project);
    r.flattened = int(f.flattened.size());
    switch (format) {
    case Format::FcpXml:
        r.data = writeXml(project, f, r);
        countMissing(r, f, checkFiles);
        break;
    case Format::Otio:
        r.data = writeOtio(project, f, r);
        countMissing(r, f, checkFiles);
        break;
    case Format::Edl:
        r.data = writeEdl(project, f, r);
        countMissing(r, f, checkFiles, true);
        break;
    }
    for (auto it = f.losses.cbegin(); it != f.losses.cend(); ++it)
        if (it.value() > 0)
            r.losses.insert(it.key(), it.value());
    return r;
}

} // namespace Interchange
} // namespace bc
