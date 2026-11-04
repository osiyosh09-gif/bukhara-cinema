#include "app/CollectDialog.h"

#include "app/Formatting.h"
#include "app/ProjectController.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace bc {

CollectDialog::CollectDialog(ProjectController *ctl, QWidget *parent)
    : QDialog(parent)
    , m_ctl(ctl)
    , m_dest(new QLineEdit(this))
    , m_onlyUsed(new QCheckBox(tr("Only media used in the sequence"), this))
    , m_info(new QLabel(this))
    , m_progress(new QProgressBar(this))
    , m_start(new QPushButton(tr("Collect"), this))
    , m_close(new QPushButton(tr("Close"), this))
    , m_openResult(new QPushButton(tr("Open collected project"), this))
{
    setObjectName(QStringLiteral("collectDialog"));
    setWindowTitle(tr("Collect project"));
    m_dest->setObjectName(QStringLiteral("collectDest"));
    m_onlyUsed->setObjectName(QStringLiteral("collectOnlyUsed"));
    m_onlyUsed->setChecked(true);
    m_info->setObjectName(QStringLiteral("collectInfo"));
    m_info->setWordWrap(true);
    m_progress->setObjectName(QStringLiteral("collectProgress"));
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_start->setObjectName(QStringLiteral("collectStart"));
    m_close->setObjectName(QStringLiteral("collectClose"));
    m_openResult->setObjectName(QStringLiteral("collectOpen"));
    m_openResult->hide();

    auto *intro = new QLabel(tr("Copies the project file and its media into one new folder, for example to move "
                                "it to another computer or to archive it. The open project does not change."),
                             this);
    intro->setWordWrap(true);
    auto *browse = new QPushButton(tr("Browse..."), this);
    auto *destRow = new QHBoxLayout;
    destRow->addWidget(m_dest, 1);
    destRow->addWidget(browse);
    auto *form = new QFormLayout;
    form->addRow(tr("Destination folder:"), destRow);
    form->addRow(QString(), m_onlyUsed);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_openResult);
    buttons->addStretch();
    buttons->addWidget(m_start);
    buttons->addWidget(m_close);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(intro);
    lay->addLayout(form);
    lay->addWidget(m_info);
    lay->addWidget(m_progress);
    lay->addStretch();
    lay->addLayout(buttons);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Destination folder"), m_dest->text());
        if (!d.isEmpty())
            setDestination(d);
    });
    connect(m_dest, &QLineEdit::textChanged, this, &CollectDialog::updatePlan);
    connect(m_onlyUsed, &QCheckBox::toggled, this, &CollectDialog::updatePlan);
    connect(m_start, &QPushButton::clicked, this, &CollectDialog::start);
    connect(m_close, &QPushButton::clicked, this, &CollectDialog::reject);
    connect(m_openResult, &QPushButton::clicked, this, &QDialog::accept);

    QString dest = QSettings().value(QStringLiteral("collect/lastDir")).toString();
    if (dest.isEmpty())
        dest = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    setDestination(dest);
    resize(560, 280);
}

void CollectDialog::setDestination(const QString &folder)
{
    m_dest->setText(QDir::toNativeSeparators(folder));
    updatePlan();
}

void CollectDialog::updatePlan()
{
    if (m_running)
        return;
    const QString dest = QDir::fromNativeSeparators(m_dest->text().trimmed());
    m_plan = Collector::plan(m_ctl->project(), m_ctl->displayName(), dest, m_onlyUsed->isChecked());
    QString text = tr("Files to copy: %1 (%2).").arg(m_plan.copies.size()).arg(Fmt::fileSize(m_plan.totalBytes));
    text += QLatin1Char(' ') + tr("New project: %1").arg(QDir::toNativeSeparators(m_plan.projectFile));
    if (m_plan.unusedRemoved > 0)
        text += QStringLiteral("\n") + tr("Media not used in the sequence left out: %1.").arg(m_plan.unusedRemoved);
    if (!m_plan.missing.isEmpty())
        text += QStringLiteral("\n") + tr("Missing files that cannot be copied: %1.").arg(m_plan.missing.size());
    const bool exists = QFileInfo::exists(m_plan.folder);
    if (exists)
        text += QStringLiteral("\n") + tr("This folder already exists: choose another destination.");
    m_info->setText(text);
    m_start->setEnabled(!dest.isEmpty() && !exists && QFileInfo(dest).isDir());
}

void CollectDialog::start()
{
    if (m_running || !m_start->isEnabled())
        return;
    QSettings().setValue(QStringLiteral("collect/lastDir"), QDir::fromNativeSeparators(m_dest->text().trimmed()));
    m_running = true;
    m_start->setEnabled(false);
    m_dest->setEnabled(false);
    m_onlyUsed->setEnabled(false);
    m_close->setText(tr("Cancel"));
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const Collector::Plan plan = m_plan;
    const auto cancel = m_cancel;
    QPointer<CollectDialog> self(this);
    (void)QtConcurrent::run([self, plan, cancel] {
        QString err;
        int last = -1;
        const bool ok = Collector::run(plan, &err, [&](qint64 bytes) {
            const int v = plan.totalBytes > 0 ? int(bytes * 1000 / plan.totalBytes) : 1000;
            if (v != last && self) {
                last = v;
                QMetaObject::invokeMethod(self.data(), [self, v] {
                    if (self)
                        self->m_progress->setValue(v);
                }, Qt::QueuedConnection);
            }
        }, cancel.get());
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, ok, err, plan, cancel] {
            if (!self)
                return;
            self->m_running = false;
            self->m_close->setText(tr("Close"));
            if (ok) {
                self->m_progress->setValue(1000);
                self->m_result = plan.projectFile;
                self->m_info->setText(tr("Done: %1").arg(QDir::toNativeSeparators(plan.projectFile)));
                self->m_openResult->show();
            } else {
                self->m_progress->setValue(0);
                self->m_dest->setEnabled(true);
                self->m_onlyUsed->setEnabled(true);
                self->updatePlan();
                if (!cancel->load())
                    self->m_info->setText(tr("Could not collect the project: %1").arg(err));
            }
            emit self->finished(ok);
        }, Qt::QueuedConnection);
    });
}

void CollectDialog::reject()
{
    if (m_running) {
        m_cancel->store(true); // the copy stops and removes what it wrote
        return;
    }
    QDialog::reject();
}

} // namespace bc
