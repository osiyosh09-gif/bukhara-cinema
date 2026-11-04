#include "app/CacheDialog.h"

#include "app/Formatting.h"
#include "app/ProjectController.h"
#include "core/MediaCache.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace bc {

namespace {
constexpr MediaCache::Kind kKinds[3] = {MediaCache::Kind::Thumbnail, MediaCache::Kind::Waveform,
                                        MediaCache::Kind::Proxy};
}

CacheDialog::CacheDialog(ProjectController *ctl, QWidget *parent)
    : QDialog(parent)
    , m_ctl(ctl)
    , m_total(new QLabel(this))
{
    setObjectName(QStringLiteral("cacheDialog"));
    setWindowTitle(tr("Media cache"));
    auto *intro = new QLabel(tr("The program keeps thumbnails, audio waveforms and proxy videos made from your media "
                                "in a cache folder. Deleting them is safe: they are made again when needed. Your "
                                "media files and projects are not touched."),
                             this);
    intro->setWordWrap(true);

    const QString names[3] = {tr("Thumbnails"), tr("Audio waveforms"), tr("Proxy videos")};
    const char *objectNames[3] = {"clearThumbs", "clearWaves", "clearProxies"};
    auto *grid = new QGridLayout;
    for (int i = 0; i < 3; ++i) {
        m_sizes[i] = new QLabel(this);
        m_clear[i] = new QPushButton(tr("Delete"), this);
        m_clear[i]->setObjectName(QString::fromLatin1(objectNames[i]));
        grid->addWidget(new QLabel(names[i], this), i, 0);
        grid->addWidget(m_sizes[i], i, 1, Qt::AlignRight);
        grid->addWidget(m_clear[i], i, 2);
    }
    grid->addWidget(m_total, 3, 0, 1, 2);
    connect(m_clear[0], &QPushButton::clicked, this, &CacheDialog::clearThumbnails);
    connect(m_clear[1], &QPushButton::clicked, this, &CacheDialog::clearWaveforms);
    connect(m_clear[2], &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, windowTitle(),
                                  tr("Delete all proxy videos? Big videos will play less smoothly until new proxies "
                                     "are made."))
            == QMessageBox::Yes)
            clearProxies();
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    QPushButton *open = buttons->addButton(tr("Open folder"), QDialogButtonBox::ActionRole);
    connect(open, &QPushButton::clicked, this, [] {
        QDir().mkpath(MediaCache::root());
        QDesktopServices::openUrl(QUrl::fromLocalFile(MediaCache::root()));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(intro);
    lay->addLayout(grid);
    lay->addStretch();
    lay->addWidget(buttons);
    resize(480, 260);
    refresh();
}

void CacheDialog::refresh()
{
    qint64 total = 0;
    for (int i = 0; i < 3; ++i) {
        const qint64 n = MediaCache::size(kKinds[i]);
        total += n;
        m_sizes[i]->setText(Fmt::fileSize(n));
        m_clear[i]->setEnabled(n > 0);
    }
    m_total->setText(tr("Total: %1 in %2").arg(Fmt::fileSize(total), QDir::toNativeSeparators(MediaCache::root())));
}

void CacheDialog::clearThumbnails()
{
    MediaCache::clear(MediaCache::Kind::Thumbnail);
    refresh();
}

void CacheDialog::clearWaveforms()
{
    MediaCache::clear(MediaCache::Kind::Waveform);
    refresh();
}

void CacheDialog::clearProxies()
{
    // Stop making new ones and close the open ones before deleting.
    m_ctl->cancelProxies();
    QList<QUuid> ids;
    for (const MediaItem &m : m_ctl->project().items)
        if (m_ctl->hasProxy(m.id))
            ids << m.id;
    m_ctl->deleteProxies(ids);
    MediaCache::clear(MediaCache::Kind::Proxy);
    m_ctl->refreshProxies();
    refresh();
}

} // namespace bc
