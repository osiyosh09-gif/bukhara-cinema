#include "app/Autosave.h"

#include "app/ProjectController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace bc {

namespace {
QString &dirOverride()
{
    static QString d;
    return d;
}
const char *kMinutesKey = "autosave/minutes";
constexpr int kDefaultMinutes = 2;

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}
} // namespace

QString Autosave::directory()
{
    if (!dirOverride().isEmpty())
        return dirOverride();
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/autosave");
}

void Autosave::setDirectory(const QString &dir)
{
    dirOverride() = dir;
}

int Autosave::intervalSetting()
{
    return std::clamp(QSettings().value(QString::fromLatin1(kMinutesKey), kDefaultMinutes).toInt(), 0, 60);
}

Autosave::Autosave(ProjectController *ctl, QObject *parent)
    : QObject(parent)
    , m_ctl(ctl)
    , m_timer(new QTimer(this))
    , m_session(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    QDir().mkpath(directory());
    m_lock = std::make_unique<QLockFile>(base() + QStringLiteral(".lock"));
    m_lock->setStaleLockTime(0); // only a dead process makes a lock stale
    if (!m_lock->tryLock(0))
        qWarning("autosave: cannot create lock file in %s", qPrintable(directory()));

    m_savedSerial = m_ctl->isModified() ? 0 : m_ctl->changeSerial();
    connect(m_timer, &QTimer::timeout, this, &Autosave::saveNow);
    // After a save, a reload or a new project the copy is out of date.
    connect(m_ctl, &ProjectController::modifiedChanged, this, [this](bool modified) {
        if (!modified) {
            removeOwnCopy();
            m_savedSerial = m_ctl->changeSerial();
        }
    });
    connect(m_ctl, &ProjectController::projectReset, this, [this] {
        if (!m_ctl->isModified())
            removeOwnCopy();
        m_savedSerial = m_ctl->isModified() ? 0 : m_ctl->changeSerial();
    });
    setIntervalMinutes(intervalSetting());
}

Autosave::~Autosave()
{
    // A normal exit: nothing to recover.
    removeOwnCopy();
    if (m_lock)
        m_lock->unlock();
}

QString Autosave::base() const
{
    return directory() + QLatin1Char('/') + m_session;
}

void Autosave::setIntervalMinutes(int minutes)
{
    m_minutes = std::clamp(minutes, 0, 60);
    QSettings().setValue(QString::fromLatin1(kMinutesKey), m_minutes);
    if (m_minutes > 0)
        m_timer->start(m_minutes * 60 * 1000);
    else
        m_timer->stop();
}

bool Autosave::saveNow()
{
    if (!m_ctl->isModified() || m_ctl->changeSerial() == m_savedSerial)
        return true;
    const QString original = m_ctl->filePath();
    const QString data = base() + QStringLiteral(".bcproj");
    QString error;
    QDir().mkpath(directory());
    // Relative media paths are written as seen from the real project file.
    if (!writeFile(data, m_ctl->project().toJson(original.isEmpty() ? data : original), &error)) {
        qWarning("autosave failed: %s", qPrintable(error));
        emit failed(error);
        return false;
    }
    const QDateTime now = QDateTime::currentDateTime();
    QJsonObject meta{{QStringLiteral("original"), original},
                     {QStringLiteral("name"), m_ctl->displayName()},
                     {QStringLiteral("savedAt"), now.toString(Qt::ISODateWithMs)}};
    if (!writeFile(base() + QStringLiteral(".json"), QJsonDocument(meta).toJson(), &error)) {
        emit failed(error);
        return false;
    }
    m_savedSerial = m_ctl->changeSerial();
    m_lastSaved = now;
    qInfo("autosaved %s", qPrintable(m_ctl->displayName()));
    emit saved(now);
    return true;
}

void Autosave::removeOwnCopy()
{
    QFile::remove(base() + QStringLiteral(".bcproj"));
    QFile::remove(base() + QStringLiteral(".json"));
}

QList<Autosave::Entry> Autosave::orphaned()
{
    QList<Entry> out;
    const QDir dir(directory());
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        const QString stem = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName();
        QLockFile lock(stem + QStringLiteral(".lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            continue; // that program is still running
        lock.unlock();
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        Entry e;
        e.dataPath = stem + QStringLiteral(".bcproj");
        e.originalPath = o.value(QStringLiteral("original")).toString();
        e.name = o.value(QStringLiteral("name")).toString();
        e.savedAt = QDateTime::fromString(o.value(QStringLiteral("savedAt")).toString(), Qt::ISODateWithMs);
        if (!QFileInfo::exists(e.dataPath))
            continue;
        out.append(e);
    }
    std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.savedAt > b.savedAt; });
    return out;
}

void Autosave::discard(const Entry &e)
{
    const QFileInfo fi(e.dataPath);
    const QString stem = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName();
    QFile::remove(e.dataPath);
    QFile::remove(stem + QStringLiteral(".json"));
    QFile::remove(stem + QStringLiteral(".lock"));
}

} // namespace bc
