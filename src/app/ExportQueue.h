#pragma once

#include "core/Project.h"
#include "engine/Exporter.h"

#include <QList>
#include <QObject>
#include <QUuid>

namespace bc {

class ProjectController;

// File > Export queue (phase 13a): exports waiting in line, done one after
// another. Each job keeps a copy of the project from when it was added, so
// editing goes on while the queue works and does not change queued jobs.
class ExportQueue : public QObject
{
    Q_OBJECT
public:
    struct Job
    {
        enum class State { Waiting, Running, Done, Failed, Cancelled };
        QUuid id;
        QString name;          // shown in the list (project name)
        Project project;       // the copy that is exported (its main sequence)
        ExportSettings settings;
        State state = State::Waiting;
        qint64 frame = 0;
        qint64 total = 0;
        qint64 remainingMs = -1;
        QString error;
    };

    explicit ExportQueue(ProjectController *controller, QObject *parent = nullptr);

    // Adds a job; null id (and *error) when the file is already the target
    // of a job that has not finished.
    QUuid add(const Project &project, const ExportSettings &settings, const QString &name, QString *error = nullptr);
    bool remove(const QUuid &id);      // not while it runs
    bool retry(const QUuid &id);       // a failed or cancelled job waits again
    int clearFinished();               // removes done, failed and cancelled jobs
    void start();                      // works through the waiting jobs
    void stop();                       // cancels the running job and stops the queue
    bool isRunning() const { return m_running; }
    int waitingCount() const;
    const QList<Job> &jobs() const { return m_jobs; }
    const Job *job(const QUuid &id) const;

signals:
    void changed();                          // jobs added, removed or changed state
    void jobProgress(const QUuid &id);       // the running job moved on
    void finishedAll(int done, int failed);  // the queue ran out of waiting jobs

private:
    void next();
    Job *find(const QUuid &id);

    ProjectController *m_ctl;
    QList<Job> m_jobs;
    QUuid m_current;
    bool m_running = false;
    bool m_stopping = false;
    int m_done = 0;
    int m_failed = 0;
};

} // namespace bc
