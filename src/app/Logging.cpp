#include "app/Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

namespace bc::Logging {

namespace {
QFile *g_file = nullptr;
QMutex g_mutex;
QtMessageHandler g_previous = nullptr;

void handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    {
        QMutexLocker lock(&g_mutex);
        if (g_file && g_file->isOpen()) {
            const char *level = "INFO";
            switch (type) {
            case QtDebugMsg:    level = "DEBUG"; break;
            case QtInfoMsg:     level = "INFO"; break;
            case QtWarningMsg:  level = "WARN"; break;
            case QtCriticalMsg: level = "ERROR"; break;
            case QtFatalMsg:    level = "FATAL"; break;
            }
            QTextStream out(g_file);
            out << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << ' ' << level << ' ' << msg << '\n';
            out.flush();
        }
    }
    if (g_previous)
        g_previous(type, ctx, msg);
}
} // namespace

QString logDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
}

void init()
{
    const QString dir = logDirectory();
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/bukhara.log");
    QFileInfo info(path);
    if (info.exists() && info.size() > 1024 * 1024) {
        QFile::remove(path + QStringLiteral(".1"));
        QFile::rename(path, path + QStringLiteral(".1"));
    }
    g_file = new QFile(path);
    if (!g_file->open(QIODevice::Append | QIODevice::Text)) {
        delete g_file;
        g_file = nullptr;
    }
    g_previous = qInstallMessageHandler(handler);
}

} // namespace bc::Logging
