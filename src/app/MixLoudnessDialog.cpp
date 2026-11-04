#include "app/MixLoudnessDialog.h"

#include "app/ProjectController.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

namespace bc {

namespace {
constexpr double kPresets[] = {-14.0, -16.0, -23.0};

QString db(double v, const QString &unit)
{
    return QStringLiteral("%1%2 %3").arg(v > 0 ? QStringLiteral("+") : QString()).arg(v, 0, 'f', 1).arg(unit);
}
} // namespace

MixLoudnessDialog::MixLoudnessDialog(ProjectController *controller, QWidget *parent)
    : QDialog(parent)
    , m_ctl(controller)
    , m_preset(new QComboBox(this))
    , m_target(new QDoubleSpinBox(this))
    , m_ceiling(new QDoubleSpinBox(this))
    , m_progress(new QProgressBar(this))
    , m_result(new QLabel(this))
    , m_status(new QLabel(this))
{
    setObjectName(QStringLiteral("mixLoudnessDialog"));
    setWindowTitle(tr("Mix loudness"));
    m_preset->setObjectName(QStringLiteral("mixLoudnessPreset"));
    m_target->setObjectName(QStringLiteral("mixLoudnessTarget"));
    m_ceiling->setObjectName(QStringLiteral("mixLoudnessCeiling"));
    m_progress->setObjectName(QStringLiteral("mixLoudnessProgress"));
    m_result->setObjectName(QStringLiteral("mixLoudnessResult"));
    m_status->setObjectName(QStringLiteral("mixLoudnessStatus"));

    m_preset->addItem(tr("-14 LUFS: YouTube, Instagram, Telegram"), kPresets[0]);
    m_preset->addItem(tr("-16 LUFS: podcasts, phones"), kPresets[1]);
    m_preset->addItem(tr("-23 LUFS: television (EBU R128)"), kPresets[2]);
    m_preset->addItem(tr("Custom"), 0.0);
    m_target->setRange(-40, -5);
    m_target->setDecimals(1);
    m_target->setSuffix(QStringLiteral(" LUFS"));
    m_ceiling->setRange(-12, 0);
    m_ceiling->setDecimals(1);
    m_ceiling->setSingleStep(0.5);
    m_ceiling->setSuffix(QStringLiteral(" dBFS"));
    m_ceiling->setToolTip(tr("No sample may be louder than this after the change (sample peak)"));
    QSettings s;
    const double target = s.value(QStringLiteral("loudness/target"), -14.0).toDouble();
    m_target->setValue(target);
    m_ceiling->setValue(s.value(QStringLiteral("loudness/ceiling"), -1.0).toDouble());
    int preset = 3;
    for (int i = 0; i < 3; ++i)
        if (std::abs(kPresets[i] - target) < 0.05)
            preset = i;
    m_preset->setCurrentIndex(preset);
    m_target->setEnabled(preset == 3);

    m_progress->setRange(0, 100);
    m_progress->setTextVisible(true);
    m_result->setWordWrap(true);
    m_status->setWordWrap(true);
    QFont big = m_result->font();
    big.setPointSizeF(big.pointSizeF() + 1);
    big.setBold(true);
    m_result->setFont(big);

    auto *form = new QFormLayout;
    form->addRow(tr("Target:"), m_preset);
    form->addRow(QString(), m_target);
    form->addRow(tr("Peak ceiling:"), m_ceiling);

    auto *note = new QLabel(tr("The whole sequence is measured as it will be exported: every track with its "
                               "volume, sound effects and fader, and the master. Apply changes only the master "
                               "volume (Audio tab), so the balance between the tracks stays."),
                            this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#8a8a8a;"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    m_measure = buttons->addButton(tr("Measure again"), QDialogButtonBox::ActionRole);
    m_measure->setObjectName(QStringLiteral("mixLoudnessMeasure"));
    m_apply = buttons->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
    m_apply->setObjectName(QStringLiteral("mixLoudnessApply"));
    connect(m_measure, &QPushButton::clicked, this, &MixLoudnessDialog::measure);
    connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(m_progress);
    lay->addWidget(m_result);
    lay->addWidget(m_status);
    lay->addWidget(note);
    lay->addStretch();
    lay->addWidget(buttons);
    setMinimumWidth(460);

    connect(m_preset, &QComboBox::currentIndexChanged, this, [this](int i) {
        const double v = m_preset->itemData(i).toDouble();
        m_target->setEnabled(v == 0.0);
        if (v != 0.0) {
            const QSignalBlocker block(m_target);
            m_target->setValue(v);
        }
        refresh();
    });
    connect(m_target, &QDoubleSpinBox::valueChanged, this, &MixLoudnessDialog::refresh);
    connect(m_ceiling, &QDoubleSpinBox::valueChanged, this, &MixLoudnessDialog::refresh);
    connect(m_ctl, &ProjectController::mixLoudnessProgress, this, [this](qint64 done, qint64 total) {
        m_progress->setValue(total > 0 ? int(done * 100 / total) : 0);
    });
    connect(m_ctl, &ProjectController::mixLoudnessMeasured, this, [this](bool, const QString &err) {
        refresh();
        if (!err.isEmpty())
            m_status->setText(tr("Could not measure: %1").arg(err));
    });
    connect(m_ctl, &ProjectController::timelineChanged, this, &MixLoudnessDialog::refresh);

    bool current = false;
    m_ctl->mixLoudness(&current);
    if (!current)
        measure();
    refresh();
}

MixLoudnessDialog::~MixLoudnessDialog()
{
    m_ctl->cancelMixLoudness(); // nobody waits for it any more
}

double MixLoudnessDialog::target() const { return m_target->value(); }

void MixLoudnessDialog::measure()
{
    QString err;
    if (!m_ctl->isMeasuringMix() && !m_ctl->measureMixLoudness(&err))
        m_status->setText(err);
    refresh();
}

void MixLoudnessDialog::refresh()
{
    bool current = false;
    const Loudness::Result r = m_ctl->mixLoudness(&current);
    const bool busy = m_ctl->isMeasuringMix();
    m_progress->setVisible(busy);
    m_measure->setEnabled(!busy);
    const double master = m_ctl->timeline().masterGainDb;
    if (busy) {
        m_result->setText(tr("Measuring the mix..."));
        m_status->setText(tr("The sound of the whole sequence is rendered; you can keep working."));
        m_apply->setEnabled(false);
        return;
    }
    if (!current) {
        m_result->setText(r.valid || r.peakDb > -120 ? tr("The sequence has changed since the last measurement.")
                                                     : QString());
        m_status->setText(tr("Press Measure again."));
        m_apply->setEnabled(false);
        return;
    }
    if (!r.valid) {
        m_result->setText(tr("The mix is silent."));
        m_status->clear();
        m_apply->setEnabled(false);
        return;
    }
    m_result->setText(tr("Loudness: %1   Peak: %2")
                          .arg(db(r.lufs, QStringLiteral("LUFS")), db(r.peakDb, QStringLiteral("dBFS"))));
    const double gain = m_ctl->mixGain(target(), m_ceiling->value());
    const double wanted = target() - r.lufs;
    if (std::abs(gain) < 0.05) {
        m_status->setText(std::abs(wanted) < 0.5
                              ? tr("The mix is at the target already (master volume %1).")
                                    .arg(db(master, QStringLiteral("dB")))
                              : tr("The peaks are at the ceiling already: the mix cannot get louder without a "
                                   "compressor or limiter (Effects tab)."));
        m_apply->setEnabled(false);
        return;
    }
    QString text = tr("Apply changes the master volume from %1 to %2.")
                       .arg(db(master, QStringLiteral("dB")), db(master + gain, QStringLiteral("dB")));
    if (gain < wanted - 0.05)
        text += QLatin1Char(' ')
                + tr("The mix stays at %1 so that its peaks stay below the ceiling; a compressor (Effects tab) can help.")
                      .arg(db(r.lufs + gain, QStringLiteral("LUFS")));
    m_status->setText(text);
    m_apply->setEnabled(true);
}

bool MixLoudnessDialog::apply()
{
    QSettings s;
    s.setValue(QStringLiteral("loudness/target"), target());
    s.setValue(QStringLiteral("loudness/ceiling"), m_ceiling->value());
    const bool ok = m_ctl->normalizeMix(target(), m_ceiling->value());
    refresh();
    return ok;
}

} // namespace bc
