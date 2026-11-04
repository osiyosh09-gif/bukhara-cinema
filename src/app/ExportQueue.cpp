#include "app/ExportQueue.h"

#include "app/ProjectController.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace bc {

namespace {
QString samePath(const QString &p)
{
    return QDir::cleanPath(QFileInfo(p).absoluteFilePath()).toLower(); // Windows paths ignore case
}
} // namespace

ExportQueue::ExportQueue(ProjectController *controller, QObject *parent)
    : QObject(parent)
    , m_ctl(controller)
{
    connect(m_ctl, &ProjectController::exportProgress, this, [this](qint64 frame, qint64 total, qint64 remainingMs) {
        Job *j = find(m_current);
        if (!j || j->state != Job::State::Running)
            return;
        j->frame = frame;
        j->total = total;
        j->remainingMs = remainingMs;
        emit jobProgress(j->id);
    });
    connect(m_ctl, &ProjectController::exportFinished, this, [this](bool ok, const QString &error) {
        Job *j = find(m_current);
        if (!j || j->state != Job::State::Running)
            return; // an export started from the Export dialog
        m_current = QUuid();
        if (ok) {
            j->state = Job::State::Done;
            j->frame = j->total;
            ++m_done;
        } else if (error.isEmpty()) {
            j->state = Job::State::Cancelled;
        } else {
            j->state = Job::State::Failed;
            j->error = error;
            ++m_failed;
        }
        emit changed();
        // Later, so the exporter has finished its own clean-up.
        QMetaObject::invokeMethod(this, &ExportQueue::next, Qt::QueuedConnection);
    });
}

ExportQueue::Job *ExportQueue::find(const QUuid &id)
{
    for (Job &j : m_jobs)
        if (j.id == id && !id.isNull())
            return &j;
    return nullptr;
}

const ExportQueue::Job *ExportQueue::job(const QUuid &id) const
{
    return const_cast<ExportQueue *>(this)->find(id);
}

QUuid ExportQueue::add(const Project &project, const ExportSettings &settings, const QString &name, QString *error)
{
    for (const Job &j : m_jobs)
        if ((j.state == Job::State::Waiting || j.state == Job::State::Running)
            && samePath(j.settings.path) == samePath(settings.path)) {
            if (error)
                *error = tr("Another export in the queue already writes %1.").arg(QDir::toNativeSeparators(settings.path));
            return {};
        }
    Job j;
    j.id = QUuid::createUuid();
    j.name = name;
    j.project = project;
    j.settings = settings;
    j.total = project.timeline.duration();
    m_jobs << j;
    emit changed();
    return j.id;
}

bool ExportQueue::remove(const QUuid &id)
{
    for (int i = 0; i < m_jobs.size(); ++i)
        if (m_jobs[i].id == id && m_jobs[i].state != Job::State::Running) {
            m_jobs.removeAt(i);
            emit changed();
            return true;
        }
    return false;
}

bool ExportQueue::retry(const QUuid &id)
{
    Job *j = find(id);
    if (!j || (j->state != Job::State::Failed && j->state != Job::State::Cancelled))
        return false;
    j->state = Job::State::Waiting;
    j->error.clear();
    j->frame = 0;
    j->remainingMs = -1;
    emit changed();
    return true;
}

int ExportQueue::clearFinished()
{
    const auto n = m_jobs.removeIf([](const Job &j) {
        return j.state == Job::State::Done || j.state == Job::State::Failed || j.state == Job::State::Cancelled;
    });
    if (n > 0)
        emit changed();
    return int(n);
}

int ExportQueue::waitingCount() const
{
    return int(std::count_if(m_jobs.begin(), m_jobs.end(), [](const Job &j) { return j.state == Job::State::Waiting; }));
}

void ExportQueue::start()
{
    if (m_running)
        return;
    m_running = true;
    m_stopping = false;
    m_done = m_failed = 0;
    emit changed();
    next();
}

void ExportQueue::stop()
{
    if (!m_running)
        return;
    m_stopping = true;
    if (find(m_current))
        m_ctl->cancelExport(); // its finished signal ends the queue
    else
        next();
}

void ExportQueue::next()
{
    if (m_stopping) {
        m_running = m_stopping = false;
        emit changed();
        return;
    }
    for (Job &j : m_jobs) {
        if (j.state != Job::State::Waiting)
            continue;
        QString error;
        QDir().mkpath(QFileInfo(j.settings.path).absolutePath());
        j.state = Job::State::Running;
        j.frame = 0;
        m_current = j.id;
        emit changed();
        if (m_ctl->startExport(j.project, j.settings, &error))
            return; // the finished signal goes on with the next one
        j.state = Job::State::Failed;
        j.error = error;
        m_current = QUuid();
        ++m_failed;
        emit changed();
    }
    m_running = false;
    emit changed();
    emit finishedAll(m_done, m_failed);
}

} // namespace bc
