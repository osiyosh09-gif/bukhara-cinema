#include "app/SceneDialog.h"

#include "app/ProjectController.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

namespace bc {

SceneDialog::SceneDialog(ProjectController *controller, const QList<QUuid> &clipIds, QWidget *parent)
    : QDialog(parent)
    , m_ctl(controller)
    , m_clips(controller->sceneClips(clipIds))
    , m_sensitivity(new QSlider(Qt::Horizontal, this))
    , m_mode(new QComboBox(this))
    , m_progress(new QProgressBar(this))
    , m_result(new QLabel(this))
{
    setObjectName(QStringLiteral("sceneDialog"));
    setWindowTitle(tr("Detect scene changes"));
    m_sensitivity->setObjectName(QStringLiteral("sceneSensitivity"));
    m_mode->setObjectName(QStringLiteral("sceneMode"));
    m_progress->setObjectName(QStringLiteral("sceneProgress"));
    m_result->setObjectName(QStringLiteral("sceneResult"));

    QSettings s;
    m_sensitivity->setRange(0, 100);
    m_sensitivity->setValue(s.value(QStringLiteral("scenes/sensitivity"), 50).toInt());
    m_sensitivity->setToolTip(tr("Higher finds softer cuts too; lower finds only clear cuts"));
    m_mode->addItem(tr("Split the clips at each new shot"), 0);
    m_mode->addItem(tr("Put a marker at each new shot"), 1);
    m_mode->setCurrentIndex(s.value(QStringLiteral("scenes/mode"), 0).toInt() == 1 ? 1 : 0);
    m_progress->setRange(0, 100);
    m_result->setWordWrap(true);
    QFont big = m_result->font();
    big.setBold(true);
    m_result->setFont(big);

    auto *low = new QLabel(tr("Low"), this);
    auto *high = new QLabel(tr("High"), this);
    auto *row = new QHBoxLayout;
    row->addWidget(low);
    row->addWidget(m_sensitivity, 1);
    row->addWidget(high);
    auto *form = new QFormLayout;
    form->addRow(tr("Sensitivity:"), row);
    form->addRow(tr("Result:"), m_mode);

    auto *note = new QLabel(tr("Finds hard cuts between shots in the selected video clips. It works on this "
                               "computer, without the internet; each video is read once and remembered. "
                               "Slow fades between shots are not found."),
                            this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#8a8a8a;"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    m_apply = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    m_apply->setObjectName(QStringLiteral("sceneApply"));
    connect(m_apply, &QPushButton::clicked, this, [this] {
        if (apply() > 0)
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(m_progress);
    lay->addWidget(m_result);
    lay->addWidget(note);
    lay->addStretch();
    lay->addWidget(buttons);
    setMinimumWidth(460);

    connect(m_sensitivity, &QSlider::valueChanged, this, &SceneDialog::refresh);
    connect(m_mode, &QComboBox::currentIndexChanged, this, &SceneDialog::refresh);
    connect(m_ctl, &ProjectController::sceneProgress, this, [this](const QUuid &, double f) {
        m_progress->setValue(int(f * 100));
    });
    connect(m_ctl, &ProjectController::scenesAnalyzed, this, &SceneDialog::refresh);

    m_ctl->analyzeScenes(m_clips);
    refresh();
}

SceneDialog::~SceneDialog()
{
    QSettings s;
    s.setValue(QStringLiteral("scenes/sensitivity"), m_sensitivity->value());
    s.setValue(QStringLiteral("scenes/mode"), m_mode->currentIndex());
    if (!m_ctl->scenesKnown(m_clips))
        m_ctl->cancelScenes(); // nobody waits for it any more
}

double SceneDialog::sensitivity() const
{
    return m_sensitivity->value() / 100.0;
}

int SceneDialog::apply()
{
    if (!m_ctl->scenesKnown(m_clips))
        return 0;
    return m_mode->currentIndex() == 1 ? m_ctl->markScenes(m_clips, sensitivity())
                                       : m_ctl->splitAtScenes(m_clips, sensitivity());
}

void SceneDialog::refresh()
{
    m_apply->setText(m_mode->currentIndex() == 1 ? tr("Add markers") : tr("Split clips"));
    if (m_clips.isEmpty()) {
        m_progress->hide();
        m_result->setText(tr("Select video clips on the timeline first."));
        m_apply->setEnabled(false);
        return;
    }
    const bool known = m_ctl->scenesKnown(m_clips);
    m_progress->setVisible(!known);
    if (!known) {
        m_result->setText(tr("Reading the video... you can keep working."));
        m_apply->setEnabled(false);
        return;
    }
    const int n = m_ctl->sceneCutCount(m_clips, sensitivity());
    m_result->setText(n > 0 ? tr("Scene changes found: %1.").arg(n)
                            : tr("No scene changes found in the selected clips."));
    m_apply->setEnabled(n > 0);
}

} // namespace bc
