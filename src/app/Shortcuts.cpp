#include "app/Shortcuts.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace bc {

namespace {
const char *kDefaultProp = "bc_defaultKeys";

QString settingsKey(const QAction *a)
{
    return QStringLiteral("shortcuts/") + a->objectName();
}

QString toText(const QList<QKeySequence> &keys)
{
    QStringList parts;
    for (const QKeySequence &k : keys)
        parts << k.toString(QKeySequence::PortableText);
    return parts.join(QStringLiteral("; "));
}

QList<QKeySequence> fromText(const QString &text)
{
    QList<QKeySequence> keys;
    for (const QString &part : text.split(QStringLiteral("; "), Qt::SkipEmptyParts)) {
        const QKeySequence k(part, QKeySequence::PortableText);
        if (!k.isEmpty())
            keys << k;
    }
    return keys;
}
} // namespace

QList<QAction *> Shortcuts::actions(QWidget *root)
{
    QList<QAction *> out;
    QSet<QString> seen;
    for (QAction *a : root->findChildren<QAction *>()) {
        const QString name = a->objectName();
        if (!name.startsWith(QLatin1String("act")) || a->isSeparator() || a->menu()
            || a->property("bc_planned").toBool() || seen.contains(name))
            continue;
        seen.insert(name);
        out << a;
    }
    std::sort(out.begin(), out.end(), [](QAction *x, QAction *y) {
        return displayName(x).localeAwareCompare(displayName(y)) < 0;
    });
    return out;
}

void Shortcuts::init(QWidget *root)
{
    QSettings s;
    for (QAction *a : actions(root)) {
        if (!a->property(kDefaultProp).isValid())
            a->setProperty(kDefaultProp, QVariant::fromValue(toText(a->shortcuts())));
        if (s.contains(settingsKey(a)))
            a->setShortcuts(fromText(s.value(settingsKey(a)).toString()));
    }
}

QList<QKeySequence> Shortcuts::defaults(const QAction *action)
{
    const QVariant v = action->property(kDefaultProp);
    return v.isValid() ? fromText(v.toString()) : action->shortcuts();
}

void Shortcuts::setKeys(QAction *action, const QList<QKeySequence> &keys)
{
    action->setShortcuts(keys);
    QSettings s;
    if (toText(keys) == toText(defaults(action)))
        s.remove(settingsKey(action));
    else
        s.setValue(settingsKey(action), toText(keys));
}

QString Shortcuts::displayName(const QAction *action)
{
    QString t = action->text().section(QLatin1Char('\t'), 0, 0);
    t.remove(QLatin1Char('&'));
    return t.trimmed();
}

// ---------------------------------------------------------------------------

ShortcutsDialog::ShortcutsDialog(QWidget *root, QWidget *parent)
    : QDialog(parent)
    , m_actions(Shortcuts::actions(root))
    , m_filter(new QLineEdit(this))
    , m_table(new QTableWidget(this))
    , m_edit(new QKeySequenceEdit(this))
    , m_note(new QLabel(this))
    , m_clear(new QPushButton(tr("No key"), this))
    , m_default(new QPushButton(tr("Default"), this))
{
    setObjectName(QStringLiteral("shortcutsDialog"));
    setWindowTitle(tr("Keyboard shortcuts"));
    for (QAction *a : m_actions)
        m_keys.insert(a, a->shortcuts());

    m_filter->setObjectName(QStringLiteral("shortcutFilter"));
    m_filter->setPlaceholderText(tr("Search commands"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, &ShortcutsDialog::filter);

    m_table->setObjectName(QStringLiteral("shortcutTable"));
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("Command"), tr("Keys")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ShortcutsDialog::updateEditor);

    m_edit->setObjectName(QStringLiteral("shortcutEdit"));
    connect(m_edit, &QKeySequenceEdit::editingFinished, this, [this] {
        const QKeySequence k = m_edit->keySequence();
        if (!k.isEmpty())
            assignToSelected(QKeySequence(k[0])); // one key combination
    });
    m_clear->setObjectName(QStringLiteral("shortcutClear"));
    m_default->setObjectName(QStringLiteral("shortcutDefault"));
    connect(m_clear, &QPushButton::clicked, this, &ShortcutsDialog::clearSelected);
    connect(m_default, &QPushButton::clicked, this, &ShortcutsDialog::resetSelected);
    auto *resetAllBtn = new QPushButton(tr("Reset all"), this);
    resetAllBtn->setObjectName(QStringLiteral("shortcutResetAll"));
    connect(resetAllBtn, &QPushButton::clicked, this, &ShortcutsDialog::resetAll);

    m_note->setObjectName(QStringLiteral("shortcutNote"));
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("color:#d1a54a;"));

    auto *editRow = new QHBoxLayout;
    editRow->addWidget(new QLabel(tr("Press the new keys:"), this));
    editRow->addWidget(m_edit, 1);
    editRow->addWidget(m_clear);
    editRow->addWidget(m_default);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->addButton(resetAllBtn, QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_filter);
    lay->addWidget(m_table, 1);
    lay->addLayout(editRow);
    lay->addWidget(m_note);
    lay->addWidget(buttons);
    resize(560, 560);
    fill();
    updateEditor();
}

QString ShortcutsDialog::keysText(const QList<QKeySequence> &keys) const
{
    QStringList parts;
    for (const QKeySequence &k : keys)
        parts << k.toString(QKeySequence::NativeText);
    return parts.join(QStringLiteral(", "));
}

void ShortcutsDialog::fill()
{
    const int keep = selectedRow();
    m_table->setRowCount(int(m_actions.size()));
    for (int i = 0; i < m_actions.size(); ++i) {
        QAction *a = m_actions[i];
        auto *name = new QTableWidgetItem(Shortcuts::displayName(a));
        name->setData(Qt::UserRole, a->objectName());
        m_table->setItem(i, 0, name);
        auto *keys = new QTableWidgetItem(keysText(m_keys.value(a)));
        if (m_keys.value(a) != Shortcuts::defaults(a)) {
            QFont f = keys->font();
            f.setBold(true); // changed by the user
            keys->setFont(f);
        }
        m_table->setItem(i, 1, keys);
    }
    if (keep >= 0)
        m_table->selectRow(keep);
    filter(m_filter->text());
}

void ShortcutsDialog::filter(const QString &text)
{
    for (int i = 0; i < m_table->rowCount(); ++i) {
        const bool match = text.isEmpty() || m_table->item(i, 0)->text().contains(text, Qt::CaseInsensitive)
                           || m_table->item(i, 1)->text().contains(text, Qt::CaseInsensitive);
        m_table->setRowHidden(i, !match);
    }
}

int ShortcutsDialog::selectedRow() const
{
    const auto rows = m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void ShortcutsDialog::updateEditor()
{
    const int row = selectedRow();
    const bool on = row >= 0;
    m_edit->setEnabled(on);
    m_clear->setEnabled(on);
    m_default->setEnabled(on);
    m_edit->setKeySequence(on && !m_keys.value(m_actions[row]).isEmpty() ? m_keys.value(m_actions[row]).first()
                                                                          : QKeySequence());
}

void ShortcutsDialog::assignToSelected(const QKeySequence &key)
{
    const int row = selectedRow();
    if (row < 0 || key.isEmpty())
        return;
    QAction *target = m_actions[row];
    // One key does one thing: take it away from any other command.
    QStringList taken;
    for (QAction *a : m_actions) {
        if (a == target)
            continue;
        QList<QKeySequence> keys = m_keys.value(a);
        if (keys.removeAll(key) > 0) {
            m_keys.insert(a, keys);
            taken << Shortcuts::displayName(a);
        }
    }
    m_keys.insert(target, {key});
    m_note->setText(taken.isEmpty() ? QString()
                                    : tr("%1 was used by: %2. It was removed there.")
                                          .arg(key.toString(QKeySequence::NativeText), taken.join(QStringLiteral(", "))));
    fill();
}

void ShortcutsDialog::clearSelected()
{
    const int row = selectedRow();
    if (row < 0)
        return;
    m_keys.insert(m_actions[row], {});
    m_note->clear();
    fill();
    updateEditor();
}

void ShortcutsDialog::resetSelected()
{
    const int row = selectedRow();
    if (row < 0)
        return;
    m_keys.insert(m_actions[row], Shortcuts::defaults(m_actions[row]));
    m_note->clear();
    fill();
    updateEditor();
}

void ShortcutsDialog::resetAll()
{
    for (QAction *a : m_actions)
        m_keys.insert(a, Shortcuts::defaults(a));
    m_note->clear();
    fill();
    updateEditor();
}

void ShortcutsDialog::apply()
{
    for (QAction *a : m_actions)
        if (m_keys.value(a) != a->shortcuts())
            Shortcuts::setKeys(a, m_keys.value(a));
}

} // namespace bc
