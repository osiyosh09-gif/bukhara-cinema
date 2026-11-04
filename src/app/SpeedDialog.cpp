#include "app/SpeedDialog.h"

#include "app/ProjectController.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>

namespace bc {

SpeedDialog::SpeedDialog(ProjectController *controller, const QUuid &clipId, QWidget *parent)
    : QDialog(parent)
    , m_ctl(controller)
    , m_clip(clipId)
    , m_speed(new QDoubleSpinBox(this))
    , m_duration(new QDoubleSpinBox(this))
    , m_ripple(new QCheckBox(this))
    , m_note(new QLabel(this))
{
    setObjectName(QStringLiteral("speedDialog"));
    setWindowTitle(tr("Clip speed / duration"));
    m_speed->setObjectName(QStringLiteral("speedPercent"));
    m_duration->setObjectName(QStringLiteral("speedDuration"));
    m_ripple->setObjectName(QStringLiteral("speedRipple"));

    if (m_ctl->project().sequence.fps() > 0)
        m_fps = m_ctl->project().sequence.fps();
    double speed = 1.0;
    qint64 length = 1;
    if (const TimelineClip *c = m_ctl->timeline().clip(clipId)) {
        speed = c->speed;
        length = c->length;
        m_sourceFrames = std::max<qint64>(1, c->src(c->length));
    }

    m_speed->setRange(10, 1000);
    m_speed->setDecimals(1);
    m_speed->setSuffix(QStringLiteral(" %"));
    m_speed->setValue(speed * 100);
    m_duration->setRange(1.0 / m_fps, 24 * 3600);
    m_duration->setDecimals(2);
    m_duration->setSuffix(tr(" s"));
    m_duration->setValue(length / m_fps);
    m_ripple->setText(tr("Shift the clips after it"));
    m_ripple->setToolTip(tr("When the clip gets longer or shorter, later clips on its tracks move with its end"));
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    m_note->setText(tr("The sound plays higher or lower with the speed. Without shifting, a longer clip stops at the "
                       "next clip. Playing backwards comes later."));

    auto *form = new QFormLayout;
    form->addRow(tr("Speed:"), m_speed);
    form->addRow(tr("Duration:"), m_duration);
    form->addRow(QString(), m_ripple);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(m_note);
    lay->addWidget(buttons);

    connect(m_speed, &QDoubleSpinBox::valueChanged, this, &SpeedDialog::speedEdited);
    connect(m_duration, &QDoubleSpinBox::valueChanged, this, &SpeedDialog::durationEdited);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SpeedDialog::speedEdited()
{
    m_lengthTouched = false;
    const double speed = m_speed->value() / 100.0;
    const QSignalBlocker b(m_duration);
    m_duration->setValue(std::max<qint64>(1, qint64(std::llround(m_sourceFrames / speed))) / m_fps);
}

void SpeedDialog::durationEdited()
{
    m_lengthTouched = true;
    const double frames = std::max(1.0, std::round(m_duration->value() * m_fps));
    const QSignalBlocker b(m_speed);
    m_speed->setValue(std::clamp(m_sourceFrames / frames * 100.0, 10.0, 1000.0));
}

bool SpeedDialog::apply()
{
    const qint64 length = m_lengthTouched ? qint64(std::llround(m_duration->value() * m_fps)) : 0;
    return m_ctl->setClipSpeed(m_clip, m_speed->value() / 100.0, length, m_ripple->isChecked());
}

} // namespace bc
