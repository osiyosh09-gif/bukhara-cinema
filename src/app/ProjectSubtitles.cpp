// Subtitles of the open sequence and .srt files (phase 11, part 1).
// Part of ProjectController.

#include "app/ProjectController.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace bc {

namespace {

double seqFps(const Project &p) { return p.sequence.fps() > 0 ? p.sequence.fps() : 25.0; }

Subtitle *findSubtitle(Timeline &t, const QUuid &id)
{
    for (auto &s : t.subtitles)
        if (s.id == id)
            return &s;
    return nullptr;
}

} // namespace

QUuid ProjectController::addSubtitle(qint64 frame, const QString &text)
{
    frame = std::max<qint64>(0, frame);
    const qint64 wanted = qint64(std::llround(seqFps(m_project) * 3));
    QUuid id;
    editTimeline(tr("Add subtitle"), [&](Timeline &t) {
        qint64 length = wanted;
        for (const auto &s : t.subtitles)
            if (s.start > frame) {
                length = std::min(length, s.start - frame);
                break;
            }
        Subtitle s;
        s.id = QUuid::createUuid();
        s.start = frame;
        s.length = std::max<qint64>(1, length);
        s.text = text;
        t.subtitles << s;
        t.sortSubtitles();
        id = s.id;
        return true;
    });
    return id;
}

bool ProjectController::setSubtitleText(const QUuid &id, const QString &text)
{
    return editTimeline(tr("Subtitle text"), [&](Timeline &t) {
        Subtitle *s = findSubtitle(t, id);
        if (!s || s->text == text)
            return false;
        s->text = text;
        return true;
    }, QStringLiteral("subtitle-text:") + id.toString());
}

bool ProjectController::setSubtitleTiming(const QUuid &id, qint64 start, qint64 length)
{
    return editTimeline(tr("Subtitle timing"), [&](Timeline &t) {
        Subtitle *s = findSubtitle(t, id);
        start = std::max<qint64>(0, start);
        length = std::max<qint64>(1, length);
        if (!s || (s->start == start && s->length == length))
            return false;
        s->start = start;
        s->length = length;
        t.sortSubtitles();
        return true;
    }, QStringLiteral("subtitle-time:") + id.toString());
}

int ProjectController::removeSubtitles(const QList<QUuid> &ids)
{
    int removed = 0;
    editTimeline(ids.size() == 1 ? tr("Delete subtitle") : tr("Delete subtitles"), [&](Timeline &t) {
        const auto before = t.subtitles.size();
        t.subtitles.erase(std::remove_if(t.subtitles.begin(), t.subtitles.end(),
                                         [&](const Subtitle &s) { return ids.contains(s.id); }),
                          t.subtitles.end());
        removed = int(before - t.subtitles.size());
        return removed > 0;
    });
    return removed;
}

bool ProjectController::setSubtitleStyle(const SubtitleStyle &style)
{
    SubtitleStyle s = style;
    s.size = std::clamp(s.size, SubtitleStyle::kMinSize, SubtitleStyle::kMaxSize);
    s.margin = std::clamp(s.margin, 0, SubtitleStyle::kMaxMargin);
    return editTimeline(tr("Subtitle style"), [&](Timeline &t) {
        if (t.subtitleStyle == s)
            return false;
        t.subtitleStyle = s;
        return true;
    }, QStringLiteral("subtitle-style"));
}

int ProjectController::importSrt(const QString &path, qint64 offset, bool replace, QString *error, int *skipped)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Cannot open %1: %2").arg(QDir::toNativeSeparators(path), f.errorString());
        return 0;
    }
    if (f.size() > 20 * 1024 * 1024) {
        if (error)
            *error = tr("The file is too large for a subtitle file.");
        return 0;
    }
    int bad = 0;
    QList<Subtitle> subs = Srt::toSubtitles(Srt::parse(Srt::decode(f.readAll()), &bad), seqFps(m_project));
    if (skipped)
        *skipped = bad;
    if (subs.isEmpty()) {
        if (error)
            *error = tr("No subtitles were found in %1.").arg(QDir::toNativeSeparators(path));
        return 0;
    }
    for (Subtitle &s : subs)
        s.start = std::max<qint64>(0, s.start + offset);
    editTimeline(tr("Import subtitles"), [&](Timeline &t) {
        if (replace)
            t.subtitles.clear();
        t.subtitles += subs;
        t.sortSubtitles();
        return true;
    });
    return int(subs.size());
}

bool ProjectController::exportSrt(const QString &path, QString *error) const
{
    const QList<Srt::Cue> cues = Srt::fromSubtitles(timeline().subtitles, seqFps(m_project));
    if (cues.isEmpty()) {
        if (error)
            *error = tr("The sequence has no subtitles with text.");
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(Srt::write(cues)) < 0 || !f.commit()) {
        if (error)
            *error = tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), f.errorString());
        return false;
    }
    return true;
}

} // namespace bc
