// Proxy files: light copies of big videos used for playback (see core/Proxy).
#include "app/ProjectController.h"

#include "core/MediaCache.h"
#include "core/Proxy.h"
#include "engine/TimelinePlayer.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QtConcurrent>

namespace bc {

bool ProjectController::proxiesEnabledSetting()
{
    return QSettings().value(QStringLiteral("proxy/use"), true).toBool();
}

bool ProjectController::autoProxySetting()
{
    return QSettings().value(QStringLiteral("proxy/auto"), true).toBool();
}

void ProjectController::setUseProxies(bool on)
{
    QSettings().setValue(QStringLiteral("proxy/use"), on);
    if (m_useProxies == on)
        return;
    m_useProxies = on;
    syncPlayer();
    m_lastFrameKey.clear();
    requestProgramFrame();
}

void ProjectController::checkProxy(const MediaItem &item)
{
    if (item.info.kind != MediaInfo::Kind::Video)
        return;
    const QString file = MediaCache::pathFor(MediaCache::Kind::Proxy, item.path);
    if (QFileInfo::exists(file)) {
        if (m_proxies.value(item.id) != file) {
            m_proxies.insert(item.id, file);
            syncPlayer();
            m_lastFrameKey.clear();
            requestProgramFrame();
        }
        return;
    }
    m_proxies.remove(item.id);
    if (autoProxySetting() && Proxy::recommended(item.info))
        createProxies({item.id});
}

int ProjectController::createProxies(const QList<QUuid> &ids)
{
    int queued = 0;
    for (const QUuid &id : ids) {
        const MediaItem *m = m_project.item(id);
        if (!m || m->info.kind != MediaInfo::Kind::Video || !isOnline(id) || hasProxy(id) || isMakingProxy(id))
            continue;
        m_proxyQueue.append(id);
        ++queued;
        emit itemUpdated(id);
    }
    if (queued)
        startNextProxy();
    return queued;
}

void ProjectController::startNextProxy()
{
    if (!m_proxyRunning.isNull() || m_proxyQueue.isEmpty())
        return;
    const QUuid id = m_proxyQueue.takeFirst();
    const MediaItem *m = m_project.item(id);
    if (!m) {
        startNextProxy();
        return;
    }
    m_proxyRunning = id;
    m_proxyCancel = std::make_shared<std::atomic_bool>(false);
    const QString source = m->path;
    const QString target = MediaCache::pathFor(MediaCache::Kind::Proxy, source);
    const quint64 gen = m_generation;
    const auto cancel = m_proxyCancel;
    QPointer<ProjectController> self(this);
    emit proxyProgress(id, 0, proxyJobsLeft());
    (void)QtConcurrent::run(&m_proxyPool, [self, id, source, target, gen, cancel] {
        int lastPercent = -1;
        QString err;
        const bool ok = Proxy::make(source, target, &err, [&](double f) {
            const int percent = int(f * 100);
            if (percent == lastPercent || !self)
                return;
            lastPercent = percent;
            QMetaObject::invokeMethod(self.data(), [self, id, f] {
                if (self && self->m_proxyRunning == id)
                    emit self->proxyProgress(id, f, self->proxyJobsLeft());
            }, Qt::QueuedConnection);
        }, cancel.get());
        if (!ok)
            qWarning("proxy of %s: %s", qPrintable(source), qPrintable(err));
        else
            qInfo("proxy ready: %s", qPrintable(target));
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, id, source, target, gen, ok, err, cancel] {
            if (!self)
                return;
            if (self->m_proxyRunning == id)
                self->m_proxyRunning = QUuid();
            const MediaItem *m = self->m_project.item(id);
            if (ok && gen == self->m_generation && m && m->path == source && !cancel->load()) {
                self->m_proxies.insert(id, target);
                self->syncPlayer();
                self->m_lastFrameKey.clear();
                self->requestProgramFrame();
            }
            emit self->proxyFinished(id, ok, err);
            emit self->itemUpdated(id);
            self->startNextProxy();
        }, Qt::QueuedConnection);
    });
}

void ProjectController::cancelProxies()
{
    const QList<QUuid> queued = m_proxyQueue;
    m_proxyQueue.clear();
    if (m_proxyCancel)
        m_proxyCancel->store(true);
    for (const QUuid &id : queued)
        emit itemUpdated(id);
}

void ProjectController::deleteProxies(const QList<QUuid> &ids)
{
    QStringList files;
    for (const QUuid &id : ids) {
        m_proxyQueue.removeAll(id);
        if (m_proxyRunning == id && m_proxyCancel)
            m_proxyCancel->store(true);
        if (m_proxies.contains(id))
            files << m_proxies.take(id);
    }
    if (files.isEmpty())
        return;
    syncPlayer(); // closes the proxy files before they are deleted
    for (const QString &f : files)
        QFile::remove(f);
    m_lastFrameKey.clear();
    requestProgramFrame();
    for (const QUuid &id : ids)
        emit itemUpdated(id);
}

void ProjectController::refreshProxies()
{
    bool changed = false;
    for (auto it = m_proxies.begin(); it != m_proxies.end();) {
        if (!QFileInfo::exists(it.value())) {
            const QUuid id = it.key();
            it = m_proxies.erase(it);
            changed = true;
            emit itemUpdated(id);
        } else {
            ++it;
        }
    }
    if (changed) {
        syncPlayer();
        m_lastFrameKey.clear();
        requestProgramFrame();
    }
}

void ProjectController::waitForProxies()
{
    for (int i = 0; i < 6000 && proxyJobsLeft() > 0; ++i) {
        m_proxyPool.waitForDone(50);
        QCoreApplication::processEvents();
    }
}

} // namespace bc
