#pragma once

#include "app/Autosave.h"
#include "core/Backups.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;

namespace bc {

// Shown at start-up when the program was not closed normally last time:
// lists the autosaved copies and lets the user recover or delete them.
class RecoveryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RecoveryDialog(const QList<Autosave::Entry> &entries, QWidget *parent = nullptr);

    // The entry chosen with "Recover", or null when none.
    const Autosave::Entry *chosen() const { return m_chosen >= 0 ? &m_entries[m_chosen] : nullptr; }

private:
    void refill();

    QList<Autosave::Entry> m_entries;
    int m_chosen = -1;
    QLabel *m_text;
    QListWidget *m_list;
    QPushButton *m_recover;
    QPushButton *m_delete;
    QPushButton *m_later;
};

// File > Previous versions: earlier saved states of the open project.
class VersionsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VersionsDialog(const QString &projectPath, QWidget *parent = nullptr);

    QString chosenPath() const { return m_chosen; }

private:
    QList<Backups::Version> m_versions;
    QString m_chosen;
    QListWidget *m_list;
    QPushButton *m_open;
};

} // namespace bc
