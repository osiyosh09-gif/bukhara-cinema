#include "app/LoudnessDialog.h"

#include "app/ProjectController.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTreeWidget>
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

LoudnessDialog::LoudnessDialog(ProjectController *controller, const QList<QUuid> &selection, QWidget *parent)
    : QDialog(parent)
    , m_ctl(controller)
    , m_clips(controller->loudnessClips(selection))
    , m_preset(new QComboBox(this))
    , m_target(new QDoubleSpinBox(this))
    , m_ceiling(new QDoubleSpinBox(this))
    , m_together(new QCheckBox(this))
    , m_list(new QTreeWidget(this))
    , m_status(new QLabel(this))
{
    setObjectName(QStringLiteral("loudnessDialog"));
    setWindowTitle(tr("Normalize loudness"));
    m_preset->setObjectName(QStringLiteral("loudnessPreset"));
    m_target->setObjectName(QStringLiteral("loudnessTarget"));
    m_ceiling->setObjectName(QStringLiteral("loudnessCeiling"));
    m_together->setObjectName(QStringLiteral("loudnessTogether"));
    m_list->setObjectName(QStringLiteral("loudnessList"));
    m_status->setObjectName(QStringLiteral("loudnessStatus"));

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
    m_together->setText(tr("One volume change for all clips (keeps their balance)"));
    m_together->setToolTip(tr("For one recording cut into several clips: they are measured together"));

    QSettings s;
    const double target = s.value(QStringLiteral("loudness/target"), -14.0).toDouble();
    m_target->setValue(target);
    m_ceiling->setValue(s.value(QStringLiteral("loudness/ceiling"), -1.0).toDouble());
    m_together->setChecked(s.value(QStringLiteral("loudness/together"), false).toBool());
    int preset = 3;
    for (int i = 0; i < 3; ++i)
        if (std::abs(kPresets[i] - target) < 0.05)
            preset = i;
    m_preset->setCurrentIndex(preset);
    m_target->setEnabled(preset == 3);

    m_list->setColumnCount(4);
    m_list->setHeaderLabels({tr("Clip"), tr("Loudness"), tr("Peak"), tr("New volume")});
    m_list->setRootIsDecorated(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->setMinimumWidth(460);
    m_status->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Target:"), m_preset);
    form->addRow(QString(), m_target);
    form->addRow(tr("Peak ceiling:"), m_ceiling);
    form->addRow(QString(), m_together);

    auto *note = new QLabel(tr("The sound of the clip file is measured, before its volume and sound effects. "
                               "Silence and very quiet parts are not counted."), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#8a8a8a;"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_apply = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
    m_apply->setObjectName(QStringLiteral("loudnessApply"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(m_list, 1);
    lay->addWidget(m_status);
    lay->addWidget(note);
    lay->addWidget(buttons);

    connect(m_preset, &QComboBox::currentIndexChanged, this, [this](int i) {
        const double v = m_preset->itemData(i).toDouble();
        m_target->setEnabled(v == 0.0);
        if (v != 0.0) {
            const QSignalBlocker block(m_target);
            m_target->setValue(v);
        }
        refresh();
    });
    connect(m_target, &QDoubleSpinBox::valueChanged, this, &LoudnessDialog::refresh);
    connect(m_ceiling, &QDoubleSpinBox::valueChanged, this, &LoudnessDialog::refresh);
    connect(m_together, &QCheckBox::toggled, this, &LoudnessDialog::refresh);
    connect(m_ctl, &ProjectController::loudnessAnalyzed, this, &LoudnessDialog::refresh);

    m_ctl->analyzeLoudness(m_clips);
    refresh();
}

double LoudnessDialog::target() const { return m_target->value(); }

bool LoudnessDialog::measured() const
{
    for (const QUuid &id : m_clips)
        if (!m_ctl->loudnessKnown(id))
            return false;
    return true;
}

void LoudnessDialog::refresh()
{
    const QHash<QUuid, double> gains = m_ctl->loudnessGains(m_clips, target(), m_ceiling->value(),
                                                            m_together->isChecked());
    m_list->clear();
    int waiting = 0;
    for (const QUuid &id : m_clips) {
        const TimelineClip *c = m_ctl->timeline().clip(id);
        const MediaItem *m = c ? m_ctl->project().item(c->mediaId) : nullptr;
        auto *row = new QTreeWidgetItem(m_list);
        row->setText(0, m ? m->name : QString());
        if (!m_ctl->loudnessKnown(id)) {
            ++waiting;
            row->setText(1, tr("measuring..."));
            continue;
        }
        const Loudness::Result r = m_ctl->clipLoudness(id);
        row->setText(1, r.valid ? db(r.lufs, QStringLiteral("LUFS")) : tr("silent"));
        row->setText(2, r.peakDb > -120 ? db(r.peakDb, QStringLiteral("dBFS")) : QStringLiteral("-"));
        if (gains.contains(id)) {
            row->setText(3, db(gains.value(id), QStringLiteral("dB")));
            if (c && std::abs(gains.value(id) - c->gainDb) > 0.05)
                row->setToolTip(3, tr("Now %1").arg(db(c->gainDb, QStringLiteral("dB"))));
        }
    }
    if (m_clips.isEmpty())
        m_status->setText(tr("Select audio clips (or video clips with linked sound) on the timeline first."));
    else if (waiting > 0)
        m_status->setText(tr("Measuring... clips left: %1").arg(waiting));
    else if (gains.isEmpty())
        m_status->setText(tr("Nothing to change: the clips are silent."));
    else {
        bool limited = false;
        for (auto it = gains.constBegin(); it != gains.constEnd(); ++it) {
            const Loudness::Result r = m_together->isChecked() ? m_ctl->clipsLoudness(m_clips) : m_ctl->clipLoudness(it.key());
            limited |= it.value() < std::min(12.0, target() - r.lufs) - 0.05;
        }
        m_status->setText(limited ? tr("Some clips stay quieter than the target so that their peaks stay below the "
                                       "ceiling. A compressor (Effects tab) can help them.")
                                  : tr("Ready: Apply sets the clip volumes (one undo step)."));
    }
    m_apply->setEnabled(!m_clips.isEmpty() && waiting == 0 && !gains.isEmpty());
}

int LoudnessDialog::apply()
{
    QSettings s;
    s.setValue(QStringLiteral("loudness/target"), target());
    s.setValue(QStringLiteral("loudness/ceiling"), m_ceiling->value());
    s.setValue(QStringLiteral("loudness/together"), m_together->isChecked());
    return m_ctl->normalizeLoudness(m_clips, target(), m_ceiling->value(), m_together->isChecked());
}

} // namespace bc
