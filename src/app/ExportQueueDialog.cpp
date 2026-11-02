#include "app/ExportQueueDialog.h"

#include "app/ExportQueue.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace bc {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;

QString stateText(const ExportQueue::Job &j)
{
    using S = ExportQueue::Job::State;
    switch (j.state) {
    case S::Waiting: return ExportQueueDialog::tr("Waiting");
    case S::Running:
        return j.total > 0 ? ExportQueueDialog::tr("Exporting: %1%").arg(j.frame * 100 / j.total)
                           : ExportQueueDialog::tr("Exporting");
    case S::Done: return ExportQueueDialog::tr("Done");
    case S::Failed: return ExportQueueDialog::tr("Failed: %1").arg(j.error);
    case S::Cancelled: return ExportQueueDialog::tr("Cancelled");
    }
    return {};
}
} // namespace

ExportQueueDialog::ExportQueueDialog(ExportQueue *queue, QWidget *parent)
    : QDialog(parent)
    , m_queue(queue)
    , m_list(new QTreeWidget(this))
    , m_summary(new QLabel(this))
    , m_start(new QPushButton(tr("Start queue"), this))
    , m_stop(new QPushButton(tr("Stop"), this))
    , m_remove(new QPushButton(tr("Remove"), this))
    , m_retry(new QPushButton(tr("Try again"), this))
    , m_clear(new QPushButton(tr("Clear finished"), this))
{
    setObjectName(QStringLiteral("exportQueueDialog"));
    setWindowTitle(tr("Export queue"));
    m_list->setObjectName(QStringLiteral("exportQueueList"));
    m_start->setObjectName(QStringLiteral("exportQueueStart"));
    m_stop->setObjectName(QStringLiteral("exportQueueStop"));
    m_remove->setObjectName(QStringLiteral("exportQueueRemove"));
    m_retry->setObjectName(QStringLiteral("exportQueueRetry"));
    m_clear->setObjectName(QStringLiteral("exportQueueClear"));
    m_list->setRootIsDecorated(false);
    m_list->setHeaderLabels({tr("Project"), tr("File"), tr("State")});
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(QStringLiteral("color:#9a9a9a;"));

    auto *buttons = new QHBoxLayout;
    for (QPushButton *b : {m_start, m_stop, m_remove, m_retry, m_clear})
        buttons->addWidget(b);
    buttons->addStretch();
    auto *close = new QPushButton(tr("Close"), this);
    buttons->addWidget(close);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_list, 1);
    lay->addWidget(m_summary);
    lay->addLayout(buttons);
    resize(720, 360);

    connect(m_start, &QPushButton::clicked, m_queue, &ExportQueue::start);
    connect(m_stop, &QPushButton::clicked, m_queue, &ExportQueue::stop);
    connect(m_remove, &QPushButton::clicked, this, [this] { m_queue->remove(currentJob()); });
    connect(m_retry, &QPushButton::clicked, this, [this] { m_queue->retry(currentJob()); });
    connect(m_clear, &QPushButton::clicked, m_queue, &ExportQueue::clearFinished);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &ExportQueueDialog::updateButtons);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *row) {
        const ExportQueue::Job *j = m_queue->job(QUuid(row->data(0, kIdRole).toString()));
        if (j && j->state == ExportQueue::Job::State::Done)
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(j->settings.path).absolutePath()));
    });
    connect(m_queue, &ExportQueue::changed, this, &ExportQueueDialog::rebuild);
    connect(m_queue, &ExportQueue::jobProgress, this, &ExportQueueDialog::updateRow);
    rebuild();
}

QUuid ExportQueueDialog::currentJob() const
{
    const QTreeWidgetItem *row = m_list->currentItem();
    return row ? QUuid(row->data(0, kIdRole).toString()) : QUuid();
}

void ExportQueueDialog::selectJob(const QUuid &id)
{
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (QUuid(m_list->topLevelItem(i)->data(0, kIdRole).toString()) == id)
            m_list->setCurrentItem(m_list->topLevelItem(i));
}

void ExportQueueDialog::rebuild()
{
    const QUuid keep = currentJob();
    m_list->clear();
    for (const ExportQueue::Job &j : m_queue->jobs()) {
        auto *row = new QTreeWidgetItem(m_list);
        row->setData(0, kIdRole, j.id.toString());
        row->setText(0, j.name);
        row->setText(1, QDir::toNativeSeparators(j.settings.path));
        row->setToolTip(1, row->text(1));
        row->setText(2, stateText(j));
        row->setToolTip(2, row->text(2));
    }
    selectJob(keep);
    if (!m_list->currentItem() && m_list->topLevelItemCount() > 0)
        m_list->setCurrentItem(m_list->topLevelItem(0));
    const int waiting = m_queue->waitingCount();
    m_summary->setText(m_queue->jobs().isEmpty()
                           ? tr("The queue is empty. In the Export window, press \"Add to queue\" to put an export here.")
                       : m_queue->isRunning() ? tr("Exporting one after another. You can go on editing; queued jobs "
                                                   "use the project as it was when they were added.")
                                              : tr("Waiting: %1. Press \"Start queue\" to export them one after another.")
                                                    .arg(waiting));
    updateButtons();
}

void ExportQueueDialog::updateRow(const QUuid &id)
{
    const ExportQueue::Job *j = m_queue->job(id);
    for (int i = 0; j && i < m_list->topLevelItemCount(); ++i)
        if (QUuid(m_list->topLevelItem(i)->data(0, kIdRole).toString()) == id)
            m_list->topLevelItem(i)->setText(2, stateText(*j));
}

void ExportQueueDialog::updateButtons()
{
    const ExportQueue::Job *j = m_queue->job(currentJob());
    using S = ExportQueue::Job::State;
    m_start->setEnabled(!m_queue->isRunning() && m_queue->waitingCount() > 0);
    m_stop->setEnabled(m_queue->isRunning());
    m_remove->setEnabled(j && j->state != S::Running);
    m_retry->setEnabled(j && (j->state == S::Failed || j->state == S::Cancelled));
    m_clear->setEnabled(std::any_of(m_queue->jobs().begin(), m_queue->jobs().end(), [](const ExportQueue::Job &x) {
        return x.state == S::Done || x.state == S::Failed || x.state == S::Cancelled;
    }));
}

} // namespace bc
