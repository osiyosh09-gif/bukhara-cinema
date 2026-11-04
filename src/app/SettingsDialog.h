#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QDialogButtonBox;

namespace bc {

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    QString selectedLanguage() const;
    void setSelectedLanguage(const QString &code);

    // Applies the selected language (called on OK).
    void apply();

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslate();

    QLabel *m_langLabel;
    QComboBox *m_langCombo;
    QDialogButtonBox *m_buttons;
};

} // namespace bc
