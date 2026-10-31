#include "app/SettingsDialog.h"

#include "app/Language.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace bc {

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_langLabel(new QLabel(this))
    , m_langCombo(new QComboBox(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setObjectName(QStringLiteral("settingsDialog"));
    for (const QString &code : Language::availableCodes())
        m_langCombo->addItem(Language::nativeName(code), code);
    m_langCombo->setObjectName(QStringLiteral("languageCombo"));
    setSelectedLanguage(Language::instance().current());

    auto *form = new QFormLayout;
    form->addRow(m_langLabel, m_langCombo);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addStretch();
    lay->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(420, 140);
    retranslate();
}

QString SettingsDialog::selectedLanguage() const
{
    return m_langCombo->currentData().toString();
}

void SettingsDialog::setSelectedLanguage(const QString &code)
{
    const int idx = m_langCombo->findData(code);
    if (idx >= 0)
        m_langCombo->setCurrentIndex(idx);
}

void SettingsDialog::apply()
{
    Language::instance().setLanguage(selectedLanguage());
}

void SettingsDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QDialog::changeEvent(event);
}

void SettingsDialog::retranslate()
{
    setWindowTitle(tr("Settings"));
    m_langLabel->setText(tr("Interface language:"));
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));
    m_buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
}

} // namespace bc
