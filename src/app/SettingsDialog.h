#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QDialogButtonBox;
class QSpinBox;
class QCheckBox;

namespace bc {

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    QString selectedLanguage() const;
    void setSelectedLanguage(const QString &code);

    // Applies the selected language and stores the autosave, backup, proxy
    // and video card settings (called on OK).
    void apply();

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslate();

    QLabel *m_langLabel;
    QComboBox *m_langCombo;
    QLabel *m_autosaveLabel;
    QComboBox *m_autosaveCombo;
    QLabel *m_backupLabel;
    QSpinBox *m_backupSpin;
    QCheckBox *m_autoProxy;
    QLabel *m_gpuLabel;
    QComboBox *m_gpuCombo;
    QLabel *m_gpuInfo;
    QDialogButtonBox *m_buttons;
};

} // namespace bc
