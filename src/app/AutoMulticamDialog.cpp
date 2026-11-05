#include "app/AutoMulticamDialog.h"

#include "app/ProjectController.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <cmath>

namespace bc {

AutoMulticamDialog::AutoMulticamDialog(ProjectController *controller, const QList<QUuid> &selection, QWidget *parent)
    : QDialog(parent)
    , m_ctl(controller)
    , m_clips(controller->autoMulticamClips(selection))
    , m_mode(new QComboBox(this))
    , m_wide(new QComboBox(this))
    , m_minShot(new QDoubleSpinBox(this))
    , m_maxShot(new QDoubleSpinBox(this))
    , m_margin(new QDoubleSpinBox(this))
    , m_progress(new QProgressBar(this))
    , m_result(new QLabel(this))
    , m_warning(new QLabel(this))
{
    setObjectName(QStringLiteral("autoMulticamDialog"));
    setWindowTitle(tr("Automatic multicamera edit"));
    m_mode->setObjectName(QStringLiteral("autoMode"));
    m_wide->setObjectName(QStringLiteral("autoWide"));
    m_minShot->setObjectName(QStringLiteral("autoMinShot"));
    m_maxShot->setObjectName(QStringLiteral("autoMaxShot"));
    m_margin->setObjectName(QStringLiteral("autoMargin"));
    m_progress->setObjectName(QStringLiteral("autoProgress"));
    m_result->setObjectName(QStringLiteral("autoResult"));
    m_warning->setObjectName(QStringLiteral("autoWarning"));

    QSettings s;
    m_mode->addItem(tr("Whoever is speaking (each camera's own sound)"), int(AutoMulticam::Mode::Speaker));
    m_mode->addItem(tr("In a rhythm (cameras in turn)"), int(AutoMulticam::Mode::Rhythm));
    m_mode->setCurrentIndex(s.value(QStringLiteral("autoMulticam/mode"), 0).toInt() == 1 ? 1 : 0);
    m_wide->addItem(tr("None"), -1);
    const QStringList angles = m_clips.isEmpty() ? QStringList() : m_ctl->multicamClipAngles(m_clips.first());
    for (int i = 0; i < angles.size(); ++i)
        m_wide->addItem(tr("Camera %1: %2").arg(i + 1).arg(angles[i]), i);
    const int wide = s.value(QStringLiteral("autoMulticam/wide"), -1).toInt();
    m_wide->setCurrentIndex(std::max(0, m_wide->findData(wide)));
    m_wide->setToolTip(tr("Shown when nobody speaks or several people speak at once, and between long shots"));
    for (QDoubleSpinBox *box : {m_minShot, m_maxShot}) {
        box->setDecimals(1);
        box->setSingleStep(0.5);
        box->setSuffix(tr(" s"));
        box->setKeyboardTracking(false);
    }
    m_minShot->setRange(0.5, 30);
    m_minShot->setValue(s.value(QStringLiteral("autoMulticam/minShot"), 2.0).toDouble());
    m_maxShot->setRange(0, 120);
    m_maxShot->setSpecialValueText(tr("No limit"));
    m_maxShot->setValue(s.value(QStringLiteral("autoMulticam/maxShot"), 8.0).toDouble());
    m_margin->setRange(1, 20);
    m_margin->setDecimals(0);
    m_margin->setSuffix(tr(" dB"));
    m_margin->setKeyboardTracking(false);
    m_margin->setValue(s.value(QStringLiteral("autoMulticam/margin"), 6.0).toDouble());
    m_margin->setToolTip(tr("How much louder the speaker's camera must hear than the others. "
                            "Lower switches more readily."));
    m_progress->setRange(0, 1);
    m_result->setWordWrap(true);
    QFont big = m_result->font();
    big.setBold(true);
    m_result->setFont(big);
    m_warning->setWordWrap(true);
    m_warning->setStyleSheet(QStringLiteral("color:#d1a54a;"));

    auto *form = new QFormLayout;
    form->addRow(tr("Switch by:"), m_mode);
    form->addRow(tr("Wide shot:"), m_wide);
    form->addRow(tr("Shortest shot:"), m_minShot);
    form->addRow(tr("Longest shot:"), m_maxShot);
    form->addRow(tr("Speaker louder by:"), m_margin);

    auto *note = new QLabel(tr("Works best when each camera (or each person's microphone) records its own sound: "
                               "the camera that hears a person loudest shows that person. It works on this "
                               "computer, without the internet; the sound of each recording is read once and "
                               "remembered. The result is one undo step, and every cut can be changed afterwards "
                               "with the camera keys 1-9 or in the Clip panel."),
                            this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#8a8a8a;"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    m_apply = buttons->addButton(tr("Edit"), QDialogButtonBox::AcceptRole);
    m_apply->setObjectName(QStringLiteral("autoApply"));
    connect(m_apply, &QPushButton::clicked, this, [this] {
        if (apply() > 0)
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(m_progress);
    lay->addWidget(m_result);
    lay->addWidget(m_warning);
    lay->addWidget(note);
    lay->addStretch();
    lay->addWidget(buttons);
    setMinimumWidth(500);

    connect(m_mode, &QComboBox::currentIndexChanged, this, &AutoMulticamDialog::refresh);
    connect(m_wide, &QComboBox::currentIndexChanged, this, &AutoMulticamDialog::refresh);
    for (QDoubleSpinBox *box : {m_minShot, m_maxShot, m_margin})
        connect(box, &QDoubleSpinBox::valueChanged, this, &AutoMulticamDialog::refresh);
    connect(m_ctl, &ProjectController::multicamSoundProgress, this, [this](int done, int total) {
        m_progress->setRange(0, std::max(1, total));
        m_progress->setValue(done);
    });
    connect(m_ctl, &ProjectController::multicamSoundReady, this, &AutoMulticamDialog::refresh);

    if (!m_clips.isEmpty() && !m_ctl->multicamSoundKnown(m_clips))
        m_ctl->analyzeMulticamSound(m_clips);
    refresh();
}

AutoMulticamDialog::~AutoMulticamDialog()
{
    QSettings s;
    s.setValue(QStringLiteral("autoMulticam/mode"), m_mode->currentIndex());
    s.setValue(QStringLiteral("autoMulticam/wide"), m_wide->currentData().toInt());
    s.setValue(QStringLiteral("autoMulticam/minShot"), m_minShot->value());
    s.setValue(QStringLiteral("autoMulticam/maxShot"), m_maxShot->value());
    s.setValue(QStringLiteral("autoMulticam/margin"), m_margin->value());
}

AutoMulticam::Settings AutoMulticamDialog::settings() const
{
    AutoMulticam::Settings s;
    s.mode = AutoMulticam::Mode(m_mode->currentData().toInt());
    s.wideAngle = m_wide->currentData().toInt();
    s.minShot = m_minShot->value();
    s.maxShot = m_maxShot->value();
    s.margin = m_margin->value();
    return s;
}

int AutoMulticamDialog::apply()
{
    if (m_clips.isEmpty() || !m_ctl->multicamSoundKnown(m_clips))
        return 0;
    return m_ctl->applyAutoMulticam(m_clips, settings());
}

void AutoMulticamDialog::refresh()
{
    const bool speaker = m_mode->currentData().toInt() == int(AutoMulticam::Mode::Speaker);
    m_margin->setEnabled(speaker);
    m_warning->clear();
    if (m_clips.isEmpty()) {
        m_progress->hide();
        m_result->setText(tr("Put the playhead on a multicamera clip, or select multicamera clips on the "
                             "timeline first (Sequence > Create multicamera sequence makes one)."));
        m_apply->setEnabled(false);
        return;
    }
    const bool known = m_ctl->multicamSoundKnown(m_clips);
    m_progress->setVisible(!known);
    if (!known) {
        m_result->setText(m_ctl->isAnalyzingMulticamSound() ? tr("Listening to the cameras... you can keep working.")
                                                            : tr("The sound of the cameras could not be read."));
        m_apply->setEnabled(false);
        return;
    }
    const AutoMulticam::Settings set = settings();
    const QStringList angles = m_ctl->multicamClipAngles(m_clips.first());
    int cuts = 0;
    QList<double> share;
    double separation = 0, weight = 0;
    for (const QUuid &id : m_clips) {
        const AutoMulticam::Plan p = m_ctl->autoMulticamPlan(id, set);
        cuts += std::max(0, int(p.cuts.size()) - 1);
        if (share.size() < p.share.size())
            share.resize(p.share.size(), 0.0);
        for (int i = 0; i < p.share.size(); ++i)
            share[i] += p.share[i] / double(m_clips.size());
        separation += p.separation;
        weight += 1;
    }
    QStringList parts;
    for (int i = 0; i < share.size(); ++i)
        if (share[i] > 0.005)
            parts << tr("%1 %2%").arg(i < angles.size() ? angles[i] : tr("Camera %1").arg(i + 1))
                         .arg(qRound(share[i] * 100));
    m_result->setText(cuts > 0 ? tr("Cuts: %1. On screen: %2.").arg(cuts).arg(parts.join(QStringLiteral(", ")))
                               : tr("No cuts with these settings."));
    if (speaker && weight > 0 && separation / weight < 0.3)
        m_warning->setText(tr("The cameras hear almost the same sound, so who is speaking is hard to tell. "
                              "Try \"In a rhythm\", a lower \"Speaker louder by\", or choose a wide shot."));
    m_apply->setEnabled(cuts > 0);
}

} // namespace bc
