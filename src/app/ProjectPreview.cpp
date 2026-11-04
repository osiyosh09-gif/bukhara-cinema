// Render preview (phase 13, part 2). Part of ProjectController.
//
// Heavy parts of the open sequence are rendered (picture only, without
// subtitles, at the Program monitor's size) into files in the cache folder;
// the Program monitor then plays those files there. See core/RenderPreview.

#include "app/ProjectController.h"
#include "core/RenderPreview.h"
#include "engine/TimelinePlayer.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QPointer>
#include <QtConcurrent>

namespace bc {

namespace {
constexpr int kPreviewW = 960; // same as the Program monitor
constexpr int kPreviewH = 540;

QSize previewSizeFor(const SequenceSettings &seq)
{
    QSize s(std::max(2, seq.width), std::max(2, seq.height));
    if (s.width() > kPreviewW || s.height() > kPreviewH)
        s.scale(QSize(kPreviewW, kPreviewH), Qt::KeepAspectRatio);
    return QSize(std::max(2, s.width() & ~1), std::max(2, s.height() & ~1));
}
} // namespace

QList<ProjectController::PreviewPart> ProjectController::previewParts() const
{
    if (m_previewPartsRev == m_graphRev)
        return m_previewParts;
    m_previewParts.clear();
    const QList<RenderPreview::Region> regions
        = RenderPreview::regions(m_project, timeline(), previewSizeFor(m_project.sequence));
    for (const RenderPreview::Region &r : regions)
        m_previewParts << PreviewPart{r.start, r.length, r.heavy, QFile::exists(RenderPreview::fileFor(r.key))};
    m_previewPartsRev = m_graphRev;
    return m_previewParts;
}

void ProjectController::applyPreviews()
{
    if (!m_player)
        return;
    QList<TimelinePlayer::PreviewSegment> segments;
    if (m_previewsOff) {
        m_player->setPreviews(segments);
        return;
    }
    const QList<RenderPreview::Region> regions
        = RenderPreview::regions(m_project, timeline(), previewSizeFor(m_project.sequence));
    for (const RenderPreview::Region &r : regions) {
        const QString file = RenderPreview::fileFor(r.key);
        if (QFile::exists(file))
            segments << TimelinePlayer::PreviewSegment{r.start, r.length, file};
    }
    m_player->setPreviews(segments);
}

bool ProjectController::renderPreview(qint64 from, qint64 to, QString *error)
{
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        return false;
    };
    if (m_previewBusy)
        return fail(tr("A preview is being rendered already."));
    if (!canPlay())
        return fail(tr("The video engine is not available."));
    struct Job
    {
        qint64 start, length;
        QString file;
    };
    QList<Job> jobs;
    qint64 total = 0;
    const QList<RenderPreview::Region> regions
        = RenderPreview::regions(m_project, timeline(), previewSizeFor(m_project.sequence));
    for (const RenderPreview::Region &r : regions) {
        if (!r.heavy || r.end() <= from || r.start >= to)
            continue;
        const QString file = RenderPreview::fileFor(r.key);
        if (QFile::exists(file))
            continue;
        jobs << Job{r.start, r.length, file};
        total += r.length;
    }
    if (jobs.isEmpty())
        return fail(tr("Nothing to render here: these parts are rendered already or play without it."));

    // The renderer gets its own graph of the sequence as it is now (full
    // quality media, no subtitles: they are drawn over the preview anyway).
    Project view = m_project;
    view.timeline = timeline();
    view.timeline.subtitleStyle.show = false;
    auto player = std::make_shared<TimelinePlayer>();
    player->setPreviewLimit(QSize(kPreviewW, kPreviewH));
    player->setProject(view);
    const QSize size = player->previewSize();
    const SequenceSettings seq = m_project.sequence;

    m_previewBusy = true;
    m_previewCancel = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = m_previewCancel;
    QPointer<ProjectController> self(this);
    emit previewProgress(0, total);
    (void)QtConcurrent::run(&m_previewPool, [self, player, jobs, total, size, seq, cancel]() mutable {
        QString failure;
        qint64 done = 0;
        bool ok = true;
        for (const Job &job : jobs) {
            RenderPreview::Writer writer;
            if (!writer.open(job.file, size, seq.fpsNum, seq.fpsDen, &failure)) {
                ok = false;
                break;
            }
            for (qint64 f = job.start; f < job.start + job.length && ok; ++f) {
                if (cancel->load()) {
                    ok = false;
                    break;
                }
                if (!writer.write(player->renderFrame(f), &failure))
                    ok = false;
                ++done;
                if (done % 5 == 0)
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [self, done, total] {
                        if (self)
                            emit self->previewProgress(done, total);
                    }, Qt::QueuedConnection);
            }
            if (!ok) {
                writer.abort();
                break;
            }
            if (!writer.finish(&failure)) {
                ok = false;
                break;
            }
        }
        const bool cancelled = cancel->load();
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, ok, cancelled, failure, total, player = std::move(player)] {
            if (!self)
                return;
            self->m_previewBusy = false;
            self->m_previewPartsRev = ~quint64(0);
            const bool playing = self->isPlaying();
            const qint64 at = self->m_playhead;
            self->syncPlayer(); // the Program monitor picks the new files up
            if (playing)
                self->play();
            else
                self->setPlayhead(at);
            if (ok)
                emit self->previewProgress(total, total);
            emit self->previewsChanged();
            emit self->previewFinished(ok, ok || cancelled ? QString() : failure);
        }, Qt::QueuedConnection);
    });
    return true;
}

void ProjectController::cancelPreviewRender()
{
    if (m_previewCancel)
        m_previewCancel->store(true);
}

int ProjectController::deletePreviews()
{
    cancelPreviewRender();
    m_previewPool.waitForDone();
    m_previewsOff = true; // close the files first (Windows keeps open files)
    syncPlayer();
    int n = 0;
    QDirIterator it(RenderPreview::folder(), QDir::Files);
    while (it.hasNext())
        n += QFile::remove(it.next()) ? 1 : 0;
    m_previewsOff = false;
    m_previewPartsRev = ~quint64(0);
    syncPlayer();
    emit previewsChanged();
    return n;
}

} // namespace bc
