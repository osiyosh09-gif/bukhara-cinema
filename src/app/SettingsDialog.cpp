#include "app/SettingsDialog.h"

#include "app/Autosave.h"
#include "app/Language.h"
#include "core/HwAccel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace bc {

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_langLabel(new QLabel(this))
    , m_langCombo(new QComboBox(this))
    , m_autosaveLabel(new QLabel(this))
    , m_autosaveCombo(new QComboBox(this))
    , m_backupLabel(new QLabel(this))
    , m_backupSpin(new QSpinBox(this))
    , m_autoProxy(new QCheckBox(this))
    , m_gpuLabel(new QLabel(this))
    , m_gpuCombo(new QComboBox(this))
    , m_gpuInfo(new QLabel(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setObjectName(QStringLiteral("settingsDialog"));
    for (const QString &code : Language::availableCodes())
        m_langCombo->addItem(Language::nativeName(code), code);
    m_langCombo->setObjectName(QStringLiteral("languageCombo"));
    setSelectedLanguage(Language::instance().current());

    m_autosaveCombo->setObjectName(QStringLiteral("autosaveCombo"));
    for (int minutes : {1, 2, 5, 10, 0})
        m_autosaveCombo->addItem(QString(), minutes);
    const int cur = m_autosaveCombo->findData(Autosave::intervalSetting());
    m_autosaveCombo->setCurrentIndex(cur >= 0 ? cur : 1);
    m_backupSpin->setObjectName(QStringLiteral("backupSpin"));
    m_backupSpin->setRange(0, 100);
    m_backupSpin->setValue(QSettings().value(QStringLiteral("backup/keep"), 20).toInt());

    m_autoProxy->setObjectName(QStringLiteral("autoProxyCheck"));
    m_autoProxy->setChecked(QSettings().value(QStringLiteral("proxy/auto"), true).toBool());

    // Video card: "auto" and "off"; a forced device type (set by hand in the
    // settings file) stays selectable so OK does not lose it.
    m_gpuCombo->setObjectName(QStringLiteral("gpuDecodeCombo"));
    m_gpuCombo->addItem(QString(), QStringLiteral("auto"));
    m_gpuCombo->addItem(QString(), QStringLiteral("off"));
    const QString gpu = HwAccel::setting();
    if (m_gpuCombo->findData(gpu) < 0)
        m_gpuCombo->addItem(gpu, gpu);
    m_gpuCombo->setCurrentIndex(m_gpuCombo->findData(gpu));
    m_gpuInfo->setObjectName(QStringLiteral("gpuInfo"));
    m_gpuInfo->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(m_langLabel, m_langCombo);
    form->addRow(m_autosaveLabel, m_autosaveCombo);
    form->addRow(m_backupLabel, m_backupSpin);
    form->addRow(QString(), m_autoProxy);
    form->addRow(m_gpuLabel, m_gpuCombo);
    form->addRow(QString(), m_gpuInfo);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addStretch();
    lay->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(560, 300);
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
    QSettings s;
    s.setValue(QStringLiteral("autosave/minutes"), m_autosaveCombo->currentData().toInt());
    s.setValue(QStringLiteral("backup/keep"), m_backupSpin->value());
    s.setValue(QStringLiteral("proxy/auto"), m_autoProxy->isChecked());
    HwAccel::setSetting(m_gpuCombo->currentData().toString());
    HwAccel::applyToEngine();
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
    m_autosaveLabel->setText(tr("Autosave:"));
    for (int i = 0; i < m_autosaveCombo->count(); ++i) {
        const int m = m_autosaveCombo->itemData(i).toInt();
        m_autosaveCombo->setItemText(i, m == 0 ? tr("Off") : tr("Every %1 min").arg(m));
    }
    m_backupLabel->setText(tr("Previous versions to keep:"));
    m_backupSpin->setSpecialValueText(tr("None"));
    m_autoProxy->setText(tr("Make proxies for videos larger than Full HD automatically"));
    m_autoProxy->setToolTip(tr("4K and other big videos then play smoothly; export still uses the originals."));
    m_backupSpin->setToolTip(tr("Each save keeps the old project file in the \"Backups\" folder next to it."));
    m_gpuLabel->setText(tr("Video card decoding:"));
    m_gpuCombo->setItemText(0, tr("Automatic"));
    m_gpuCombo->setItemText(1, tr("Off (processor only)"));
    m_gpuCombo->setToolTip(tr("The video card decodes H.264, HEVC and other videos for playback, export and proxies. "
                              "If it cannot decode a file, the processor does it."));
    const QStringList devices = HwAccel::available();
    const QStringList encoders = HwAccel::encoders();
    QString info = devices.isEmpty()
        ? tr("No video card decoder was found on this computer: the processor decodes.")
        : tr("Found: %1.").arg(devices.join(QStringLiteral(", ")).toUpper());
    info += QLatin1Char(' ')
        + (encoders.isEmpty() ? tr("Export: no hardware H.264 encoder, MPEG-4 is used.")
                              : tr("Export can use: %1.").arg(encoders.join(QStringLiteral(", "))));
    info += QLatin1Char(' ') + tr("Effects and colour are computed on the processor. "
                                  "A change applies to videos opened after it; restart to apply it everywhere.");
    m_gpuInfo->setText(info);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));
    m_buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
}

} // namespace bc
