#pragma once

#include <QDialog>
#include <QHash>
#include <QKeySequence>
#include <QList>

class QAction;
class QLabel;
class QLineEdit;
class QKeySequenceEdit;
class QPushButton;
class QTableWidget;

namespace bc {

// User keyboard shortcuts (Edit > Keyboard shortcuts). Each named, working
// action keeps its built-in keys; changes are stored in QSettings
// ("shortcuts/<action name>", empty = no key) and applied at start-up.
namespace Shortcuts {
// The actions under `root` whose keys can be changed (not planned ones).
QList<QAction *> actions(QWidget *root);
// Remembers the built-in keys and applies the saved ones. Call once after
// the window has created its actions.
void init(QWidget *root);
QList<QKeySequence> defaults(const QAction *action);
// Sets the keys of an action and stores them (removes the stored value when
// they are the built-in ones).
void setKeys(QAction *action, const QList<QKeySequence> &keys);
// Menu text without "&" and without the phase tag.
QString displayName(const QAction *action);
} // namespace Shortcuts

class ShortcutsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShortcutsDialog(QWidget *root, QWidget *parent = nullptr);

    // Gives `key` to the selected command (used by the key field; tests).
    void assignToSelected(const QKeySequence &key);
    void clearSelected();
    void resetSelected();
    void resetAll();
    // Applies and stores the changes (also done by OK).
    void apply();

private:
    void fill();
    void filter(const QString &text);
    void updateEditor();
    int selectedRow() const;
    QString keysText(const QList<QKeySequence> &keys) const;

    QList<QAction *> m_actions;
    QHash<QAction *, QList<QKeySequence>> m_keys; // pending
    QLineEdit *m_filter;
    QTableWidget *m_table;
    QKeySequenceEdit *m_edit;
    QLabel *m_note;
    QPushButton *m_clear;
    QPushButton *m_default;
};

} // namespace bc
