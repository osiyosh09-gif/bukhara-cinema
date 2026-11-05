// Subtitles from speech (phase 14b). Part of ProjectController.
//
// The sound of the chosen clips is heard with Vosk (core/Speech) on this
// computer; the words are cached per media file and model; the lines made of
// them go to the open sequence's subtitles in one undo step.

#include "app/ProjectController.h"
#include "core/MediaCache.h"

#include <QFileInfo>
#include <QPointer>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace bc {

namespace {

constexpr double kPad = 0.5; // seconds heard before and after a clip, so cut words are whole

// A part of a media file a clip plays, and where it lands: sequence frame = a + b * file seconds.
struct Segment
{
    QUuid media;
    QString path;
    double from = 0, to = 0; // file seconds
    double a = 0, b = 1;
};

QList<Segment> segmentsOf(const Project &p, const TimelineClip &c, double fps)
{
    QList<Segment> out;
    const double speed = c.speed > 0 ? c.speed : 1.0;
    const double clipFrom = double(c.in), clipTo = double(c.in + c.src(c.length));
    if (const MediaItem *m = p.item(c.mediaId)) {
        if (!m->info.hasAudio())
            return out;
        out << Segment{m->id, m->path, clipFrom / fps, clipTo / fps, double(c.start) - double(c.in) / speed, fps / speed};
        return out;
    }
    const NestedSequence *nest = c.isNested() ? p.nest(c.nestId) : nullptr;
    if (!nest)
        return out;
    // The sound the clip plays: its camera's track, or the first audio track with clips.
    int track = -1;
    if (nest->multicam) {
        track = nest->timeline.indexOf(Timeline::Kind::Audio, c.angle);
    } else {
        for (int i = 0; i < nest->timeline.tracks.size() && track < 0; ++i)
            if (nest->timeline.tracks[i].kind == Timeline::Kind::Audio && !nest->timeline.tracks[i].clips.isEmpty())
                track = i;
    }
    if (track < 0)
        return out;
    for (const TimelineClip &ic : nest->timeline.tracks[track].clips) {
        const MediaItem *m = p.item(ic.mediaId);
        if (!m || !m->info.hasAudio())
            continue;
        const double s0 = std::max(double(ic.start), clipFrom), s1 = std::min(double(ic.end()), clipTo);
        if (s1 <= s0)
            continue;
        const double is = ic.speed > 0 ? ic.speed : 1.0;
        Segment seg;
        seg.media = m->id;
        seg.path = m->path;
        seg.from = (double(ic.in) + (s0 - double(ic.start)) * is) / fps;
        seg.to = (double(ic.in) + (s1 - double(ic.start)) * is) / fps;
        // nest frame = ic.start + (t * fps - ic.in) / is; sequence = c.start + (nest - c.in) / speed
        seg.a = double(c.start) + (double(ic.start) - double(ic.in) / is - double(c.in)) / speed;
        seg.b = fps / (is * speed);
        out << seg;
    }
    return out;
}

QString keyOf(const QUuid &media, const QString &modelDir)
{
    return media.toString() + QLatin1Char('|') + QFileInfo(modelDir).absoluteFilePath();
}

QString cacheFile(const QString &mediaPath, const QString &modelDir)
{
    QString file = MediaCache::pathFor(MediaCache::Kind::Transcript, mediaPath);
    file.insert(file.size() - 5, QLatin1Char('-') + QFileInfo(modelDir).fileName()); // before ".bctr"
    return file;
}

double fpsOf(const Project &p)
{
    return p.sequence.fps() > 0 ? p.sequence.fps() : 25.0;
}

} // namespace

QList<QUuid> ProjectController::speechClips(const QList<QUuid> &selection) const
{
    const Timeline &tl = timeline();
    QList<QUuid> out;
    QSet<QUuid> links;
    auto usable = [&](const TimelineClip &c) { return !c.mediaId.isNull() || c.isNested(); };
    for (const QUuid &id : selection) {
        const TimelineClip *c = tl.clip(id);
        if (!c)
            continue;
        if (tl.kindOf(id) == Timeline::Kind::Audio) {
            if (usable(*c) && !out.contains(id))
                out << id;
        } else if (!c->linkId.isNull()) {
            links.insert(c->linkId);
        }
    }
    for (const TimelineTrack &t : tl.tracks)
        if (t.kind == Timeline::Kind::Audio)
            for (const TimelineClip &c : t.clips)
                if (!c.linkId.isNull() && links.contains(c.linkId) && usable(c) && !out.contains(c.id))
                    out << c.id;
    if (out.isEmpty() && selection.isEmpty()) {
        for (const TimelineTrack &t : tl.tracks) {
            if (t.kind != Timeline::Kind::Audio || t.clips.isEmpty())
                continue;
            for (const TimelineClip &c : t.clips)
                if (usable(c))
                    out << c.id;
            if (!out.isEmpty())
                break;
        }
    }
    return out;
}

bool ProjectController::speechKnown(const QList<QUuid> &clipIds, const QString &modelDir) const
{
    const double fps = fpsOf(m_project);
    for (const QUuid &id : clipIds) {
        const TimelineClip *c = timeline().clip(id);
        if (!c)
            continue;
        for (const Segment &s : segmentsOf(m_project, *c, fps)) {
            const auto it = m_transcripts.constFind(keyOf(s.media, modelDir));
            if (it == m_transcripts.constEnd() || !it->covers(std::max(0.0, s.from - kPad), s.to + kPad))
                return false;
        }
    }
    return true;
}

void ProjectController::transcribeClips(const QList<QUuid> &clipIds, const QString &modelDir)
{
    if (m_speechRunning)
        return;
    struct Job
    {
        QString key, path, cache;
        double from, to;
        Speech::Transcript known;
    };
    QList<Job> jobs;
    const double fps = fpsOf(m_project);
    for (const QUuid &id : clipIds) {
        const TimelineClip *c = timeline().clip(id);
        if (!c)
            continue;
        for (const Segment &s : segmentsOf(m_project, *c, fps)) {
            const QString key = keyOf(s.media, modelDir);
            const double from = std::max(0.0, s.from - kPad), to = s.to + kPad;
            if (m_transcripts.value(key).covers(from, to))
                continue;
            jobs << Job{key, s.path, cacheFile(s.path, modelDir), from, to, m_transcripts.value(key)};
        }
    }
    if (jobs.isEmpty()) {
        emit speechReady(true, {});
        return;
    }
    m_speechRunning = true;
    m_speechCancel = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = m_speechCancel;
    const quint64 gen = m_generation;
    QPointer<ProjectController> self(this);
    emit speechProgress(0);
    (void)QtConcurrent::run(&m_speechPool, [self, jobs, modelDir, gen, cancel] {
        QHash<QString, Speech::Transcript> results;
        QString error;
        bool ok = true;
        for (int i = 0; i < jobs.size() && ok; ++i) {
            const Job &job = jobs[i];
            Speech::Transcript t = results.contains(job.key) ? results.value(job.key) : job.known;
            Speech::Transcript cached;
            if (t.covered.isEmpty() && Speech::loadTranscript(job.cache, &cached))
                t = cached;
            if (!t.covers(job.from, job.to)) {
                QList<Speech::Word> heard;
                const int n = int(jobs.size());
                ok = Speech::transcribe(job.path, modelDir, job.from, job.to, &heard, &error, [&](double f) {
                    const double total = (double(i) + f) / n;
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [self, gen, total] {
                        if (self && gen == self->m_generation)
                            emit self->speechProgress(total);
                    }, Qt::QueuedConnection);
                }, cancel.get());
                if (ok) {
                    t.add(job.from, job.to, heard);
                    Speech::saveTranscript(job.cache, t);
                }
            }
            if (ok)
                results.insert(job.key, t);
        }
        const bool cancelled = cancel->load();
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, gen, results, ok, error, cancelled] {
            if (!self)
                return;
            self->m_speechRunning = false;
            if (gen != self->m_generation)
                return;
            for (auto it = results.cbegin(); it != results.cend(); ++it)
                self->m_transcripts.insert(it.key(), it.value());
            emit self->speechReady(ok && !cancelled, cancelled ? QString() : error);
        }, Qt::QueuedConnection);
    });
}

void ProjectController::cancelSpeech()
{
    if (m_speechCancel)
        m_speechCancel->store(true);
}

QList<Subtitle> ProjectController::speechSubtitles(const QList<QUuid> &clipIds, const QString &modelDir,
                                                   const Speech::CueSettings &settings, bool cyrillic) const
{
    const double fps = fpsOf(m_project);
    const bool toCyrillic = cyrillic && Speech::languageOf(modelDir) == QLatin1String("uz");
    QList<Subtitle> out;
    for (const QUuid &id : clipIds) {
        const TimelineClip *c = timeline().clip(id);
        if (!c)
            continue;
        // The clip's words, in sequence seconds.
        QList<Speech::Word> words;
        for (const Segment &s : segmentsOf(m_project, *c, fps)) {
            const auto it = m_transcripts.constFind(keyOf(s.media, modelDir));
            if (it == m_transcripts.constEnd())
                continue;
            for (const Speech::Word &w : it->words) {
                const double mid = (w.start + w.end) / 2;
                if (mid < s.from || mid >= s.to)
                    continue;
                Speech::Word moved = w;
                moved.start = std::max(double(c->start), s.a + s.b * w.start) / fps;
                moved.end = std::min(double(c->end()), s.a + s.b * w.end) / fps;
                if (moved.end > moved.start)
                    words << moved;
            }
        }
        std::sort(words.begin(), words.end(), [](const Speech::Word &a, const Speech::Word &b) { return a.start < b.start; });
        for (const Speech::Cue &cue : Speech::cues(words, settings)) {
            Subtitle sub;
            sub.id = QUuid::createUuid();
            sub.start = std::max(c->start, qint64(std::llround(cue.start * fps)));
            const qint64 end = std::min(c->end(), qint64(std::llround(cue.end * fps)));
            sub.length = std::max<qint64>(1, end - sub.start);
            sub.text = toCyrillic ? Speech::uzLatinToCyrillic(cue.text) : cue.text;
            out << sub;
        }
    }
    std::sort(out.begin(), out.end(), [](const Subtitle &a, const Subtitle &b) { return a.start < b.start; });
    // Two clips heard at once (two tracks): a line ends where the next begins.
    for (int i = 0; i + 1 < out.size(); ++i)
        if (out[i].end() > out[i + 1].start)
            out[i].length = std::max<qint64>(1, out[i + 1].start - out[i].start);
    return out;
}

int ProjectController::addSpeechSubtitles(const QList<QUuid> &clipIds, const QString &modelDir,
                                          const Speech::CueSettings &settings, bool cyrillic, bool replace)
{
    const QList<Subtitle> lines = speechSubtitles(clipIds, modelDir, settings, cyrillic);
    QList<QPair<qint64, qint64>> ranges;
    for (const QUuid &id : clipIds)
        if (const TimelineClip *c = timeline().clip(id))
            ranges << qMakePair(c->start, c->end());
    int added = 0;
    editTimeline(tr("Subtitles from speech"), [&](Timeline &t) {
        added = 0;
        bool changed = false;
        if (replace) {
            const int before = int(t.subtitles.size());
            t.subtitles.erase(std::remove_if(t.subtitles.begin(), t.subtitles.end(), [&](const Subtitle &s) {
                for (const auto &r : ranges)
                    if (s.start < r.second && s.end() > r.first)
                        return true;
                return false;
            }), t.subtitles.end());
            changed = int(t.subtitles.size()) != before;
        }
        for (const Subtitle &line : lines) {
            bool overlaps = false;
            for (const Subtitle &s : t.subtitles)
                overlaps |= s.start < line.end() && s.end() > line.start;
            if (overlaps)
                continue; // keeps the subtitles that were written by hand
            t.subtitles << line;
            ++added;
        }
        t.sortSubtitles();
        return changed || added > 0;
    });
    return added;
}

} // namespace bc
