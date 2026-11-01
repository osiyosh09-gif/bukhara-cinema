#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

class QLockFile;
class QTimer;

namespace bc {

class ProjectController;

// Writes a copy of the open project to a private folder every few minutes
// while it has unsaved changes. The real project file is never touched.
// Each running program holds a lock file; copies whose program is no longer
// running (it crashed or the computer lost power) can be recovered.
class Autosave : public QObject
{
    Q_OBJECT
public:
    struct Entry
    {
        QString dataPath;     // the saved copy (.bcproj)
        QString originalPath; // project file it belongs to; empty = never saved
        QString name;
        QDateTime savedAt;
    };

    explicit Autosave(ProjectController *ctl, QObject *parent = nullptr);
    ~Autosave() override;

    // Folder of the copies. Tests may point it elsewhere before creating
    // an Autosave.
    static QString directory();
    static void setDirectory(const QString &dir);

    static int intervalSetting();          // minutes, 0 = off (QSettings "autosave/minutes")
    void setIntervalMinutes(int minutes);  // also stores the setting
    int intervalMinutes() const { return m_minutes; }

    // Writes now if something changed since the last copy. Returns false on
    // a write error.
    bool saveNow();
    QDateTime lastSaved() const { return m_lastSaved; }

    // Copies left by programs that are no longer running, newest first.
    static QList<Entry> orphaned();
    static void discard(const Entry &e);

signals:
    void saved(const QDateTime &when);
    void failed(const QString &error);

private:
    void removeOwnCopy();
    QString base() const;

    ProjectController *m_ctl;
    QTimer *m_timer;
    std::unique_ptr<QLockFile> m_lock;
    QString m_session;
    int m_minutes = 2;
    quint64 m_savedSerial = 0;
    QDateTime m_lastSaved;
};

} // namespace bc
