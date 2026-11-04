#include "app/SequenceSettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace bc {

QList<SequenceSettingsDialog::Preset> SequenceSettingsDialog::presets()
{
    return {
        {QT_TR_NOOP("Full HD 1080p, 25 fps"), 1920, 1080, 25, 1},
        {QT_TR_NOOP("Full HD 1080p, 30 fps"), 1920, 1080, 30, 1},
        {QT_TR_NOOP("Full HD 1080p, 50 fps"), 1920, 1080, 50, 1},
        {QT_TR_NOOP("HD 720p, 25 fps"), 1280, 720, 25, 1},
        {QT_TR_NOOP("4K UHD, 25 fps"), 3840, 2160, 25, 1},
        {QT_TR_NOOP("Vertical 1080x1920, 30 fps (phone)"), 1080, 1920, 30, 1},
        {QT_TR_NOOP("Square 1080x1080, 30 fps"), 1080, 1080, 30, 1},
    };
}

namespace {
struct Fps { const char *label; int num, den; };
const Fps kFps[] = {{"23.976", 24000, 1001}, {"24", 24, 1},   {"25", 25, 1},         {"29.97", 30000, 1001},
                    {"30", 30, 1},           {"50", 50, 1},   {"59.94", 60000, 1001}, {"60", 60, 1}};
}

SequenceSettingsDialog::SequenceSettingsDialog(const SequenceSettings &cur, QWidget *parent)
    : QDialog(parent)
    , m_preset(new QComboBox(this))
    , m_width(new QSpinBox(this))
    , m_height(new QSpinBox(this))
    , m_fps(new QComboBox(this))
    , m_rate(new QComboBox(this))
    , m_channels(new QComboBox(this))
{
    setObjectName(QStringLiteral("sequenceSettingsDialog"));
    setWindowTitle(tr("Sequence settings"));

    for (const Preset &p : presets())
        m_preset->addItem(tr(p.name));
    m_preset->addItem(tr("Custom"));

    for (QSpinBox *sb : {m_width, m_height}) {
        sb->setRange(16, 8192);
        sb->setSingleStep(2);
        sb->setSuffix(tr(" px"));
    }
    for (const Fps &f : kFps)
        m_fps->addItem(QString::fromLatin1(f.label), QPoint(f.num, f.den));
    m_rate->addItem(tr("44.1 kHz"), 44100);
    m_rate->addItem(tr("48 kHz"), 48000);
    m_channels->addItem(tr("Mono"), 1);
    m_channels->addItem(tr("Stereo"), 2);

    auto *form = new QFormLayout;
    form->addRow(tr("Preset:"), m_preset);
    form->addRow(tr("Width:"), m_width);
    form->addRow(tr("Height:"), m_height);
    form->addRow(tr("Frame rate (fps):"), m_fps);
    form->addRow(tr("Audio sample rate:"), m_rate);
    form->addRow(tr("Audio channels:"), m_channels);

    auto *note = new QLabel(tr("Clips with a different size or frame rate are fitted to these settings in preview "
                               "and export (from phase 2)."),
                            this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#8a8a8a;"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(note);
    lay->addStretch();
    lay->addWidget(buttons);

    // Fill current values
    m_syncing = true;
    m_width->setValue(cur.width);
    m_height->setValue(cur.height);
    int fpsIdx = 2;
    for (int i = 0; i < m_fps->count(); ++i) {
        const QPoint f = m_fps->itemData(i).toPoint();
        if (qint64(f.x()) * cur.fpsDen == qint64(cur.fpsNum) * f.y())
            fpsIdx = i;
    }
    m_fps->setCurrentIndex(fpsIdx);
    m_rate->setCurrentIndex(cur.sampleRate == 44100 ? 0 : 1);
    m_channels->setCurrentIndex(cur.channels == 1 ? 0 : 1);
    m_syncing = false;
    syncPresetFromFields();

    connect(m_preset, &QComboBox::currentIndexChanged, this, &SequenceSettingsDialog::applyPreset);
    connect(m_width, &QSpinBox::valueChanged, this, &SequenceSettingsDialog::syncPresetFromFields);
    connect(m_height, &QSpinBox::valueChanged, this, &SequenceSettingsDialog::syncPresetFromFields);
    connect(m_fps, &QComboBox::currentIndexChanged, this, &SequenceSettingsDialog::syncPresetFromFields);
    resize(440, 320);
}

void SequenceSettingsDialog::applyPreset(int index)
{
    if (m_syncing)
        return;
    const auto list = presets();
    if (index < 0 || index >= list.size())
        return; // "Custom"
    m_syncing = true;
    const Preset &p = list[index];
    m_width->setValue(p.width);
    m_height->setValue(p.height);
    for (int i = 0; i < m_fps->count(); ++i)
        if (m_fps->itemData(i).toPoint() == QPoint(p.fpsNum, p.fpsDen))
            m_fps->setCurrentIndex(i);
    m_syncing = false;
}

void SequenceSettingsDialog::syncPresetFromFields()
{
    if (m_syncing)
        return;
    m_syncing = true;
    const auto list = presets();
    const QPoint f = m_fps->currentData().toPoint();
    int match = int(list.size()); // Custom
    for (int i = 0; i < list.size(); ++i)
        if (list[i].width == m_width->value() && list[i].height == m_height->value()
            && list[i].fpsNum == f.x() && list[i].fpsDen == f.y())
            match = i;
    m_preset->setCurrentIndex(match);
    m_syncing = false;
}

SequenceSettings SequenceSettingsDialog::settings() const
{
    SequenceSettings s;
    s.width = m_width->value();
    s.height = m_height->value();
    const QPoint f = m_fps->currentData().toPoint();
    s.fpsNum = f.x();
    s.fpsDen = f.y();
    s.sampleRate = m_rate->currentData().toInt();
    s.channels = m_channels->currentData().toInt();
    return s;
}

} // namespace bc
