#include "app/RecoveryDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace bc {

namespace {
QString when(const QDateTime &t)
{
    return QLocale().toString(t, QStringLiteral("dd.MM.yyyy HH:mm:ss"));
}
} // namespace

RecoveryDialog::RecoveryDialog(const QList<Autosave::Entry> &entries, QWidget *parent)
    : QDialog(parent)
    , m_entries(entries)
    , m_text(new QLabel(this))
    , m_list(new QListWidget(this))
    , m_recover(new QPushButton(tr("Recover"), this))
    , m_delete(new QPushButton(tr("Delete"), this))
    , m_later(new QPushButton(tr("Later"), this))
{
    setObjectName(QStringLiteral("recoveryDialog"));
    setWindowTitle(tr("Recover unsaved work"));
    m_text->setWordWrap(true);
    m_text->setText(tr("BUKHARA CINEMA was not closed normally last time. These autosaved copies contain "
                       "changes that were not saved. Recover one to continue working; then save it."));
    m_list->setObjectName(QStringLiteral("recoveryList"));
    m_recover->setObjectName(QStringLiteral("recoverButton"));
    m_delete->setObjectName(QStringLiteral("recoveryDeleteButton"));
    m_later->setObjectName(QStringLiteral("recoveryLaterButton"));
    m_recover->setDefault(true);

    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(m_recover, QDialogButtonBox::AcceptRole);
    buttons->addButton(m_delete, QDialogButtonBox::DestructiveRole);
    buttons->addButton(m_later, QDialogButtonBox::RejectRole);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_text);
    lay->addWidget(m_list, 1);
    lay->addWidget(buttons);

    connect(m_recover, &QPushButton::clicked, this, [this] {
        m_chosen = m_list->currentRow();
        if (m_chosen >= 0)
            accept();
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0)
            return;
        const auto answer = QMessageBox::question(this, windowTitle(),
                                                  tr("Delete this copy? The unsaved changes in it will be lost."));
        if (answer != QMessageBox::Yes)
            return;
        Autosave::discard(m_entries[row]);
        m_entries.removeAt(row);
        refill();
        if (m_entries.isEmpty())
            reject();
    });
    connect(m_later, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, m_recover, &QPushButton::click);
    refill();
    resize(560, 320);
}

void RecoveryDialog::refill()
{
    m_list->clear();
    for (const Autosave::Entry &e : m_entries) {
        const QString where = e.originalPath.isEmpty() ? tr("never saved")
                                                       : QDir::toNativeSeparators(e.originalPath);
        m_list->addItem(QStringLiteral("%1 — %2\n%3").arg(e.name, when(e.savedAt), where));
    }
    m_list->setCurrentRow(0);
    m_recover->setEnabled(!m_entries.isEmpty());
    m_delete->setEnabled(!m_entries.isEmpty());
}

VersionsDialog::VersionsDialog(const QString &projectPath, QWidget *parent)
    : QDialog(parent)
    , m_versions(Backups::list(projectPath))
    , m_list(new QListWidget(this))
    , m_open(new QPushButton(tr("Open this version"), this))
{
    setObjectName(QStringLiteral("versionsDialog"));
    setWindowTitle(tr("Previous versions"));
    auto *text = new QLabel(this);
    text->setWordWrap(true);
    text->setText(m_versions.isEmpty()
                      ? tr("No previous versions yet. Each time you save, the old file is kept in the "
                           "\"Backups\" folder next to the project.")
                      : tr("Each save keeps the old file in the \"Backups\" folder next to the project. An opened "
                           "version replaces the project only when you save it."));
    m_list->setObjectName(QStringLiteral("versionsList"));
    m_open->setObjectName(QStringLiteral("openVersionButton"));
    for (const Backups::Version &v : m_versions)
        m_list->addItem(when(v.time));
    m_list->setCurrentRow(0);
    m_open->setEnabled(!m_versions.isEmpty());

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    buttons->addButton(m_open, QDialogButtonBox::AcceptRole);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(text);
    lay->addWidget(m_list, 1);
    lay->addWidget(buttons);

    connect(m_open, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0)
            return;
        m_chosen = m_versions[row].path;
        accept();
    });
    connect(m_list, &QListWidget::itemDoubleClicked, m_open, &QPushButton::click);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(420, 360);
}

} // namespace bc
