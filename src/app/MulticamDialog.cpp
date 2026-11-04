#include "app/MulticamDialog.h"

#include "app/ProjectController.h"
#include "core/AudioSync.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <cmath>

namespace bc {

MulticamDialog::MulticamDialog(ProjectController *controller, const QList<QUuid> &mediaIds, QWidget *parent)
    : QDialog(parent)
    , m_ctl(controller)
    , m_media(mediaIds)
    , m_sync(new QComboBox(this))
    , m_audio(new QComboBox(this))
    , m_list(new QTreeWidget(this))
    , m_progress(new QProgressBar(this))
    , m_info(new QLabel(this))
{
    setObjectName(QStringLiteral("multicamDialog"));
    setWindowTitle(tr("Create multicamera sequence"));
    m_sync->setObjectName(QStringLiteral("multicamSync"));
    m_sync->addItem(tr("By sound (the cameras recorded the same sound)"));
    m_sync->addItem(tr("By file start (all start together)"));
    m_audio->setObjectName(QStringLiteral("multicamAudio"));
    m_list->setObjectName(QStringLiteral("multicamList"));
    m_list->setRootIsDecorated(false);
    m_list->setHeaderLabels({tr("Angle"), tr("File"), tr("Shift")});
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int i = 0; i < m_media.size(); ++i) {
        const MediaItem *m = m_ctl->project().item(m_media[i]);
        auto *row = new QTreeWidgetItem(m_list);
        row->setText(0, QString::number(i + 1));
        row->setText(1, m ? m->name : QString());
        if (m && m->info.hasAudio())
            m_audio->addItem(QStringLiteral("%1. %2").arg(i + 1).arg(m->name), i);
    }
    m_progress->setRange(0, std::max(1, int(m_media.size())));
    m_progress->setValue(0);
    m_progress->hide();
    m_info->setWordWrap(true);
    m_info->setText(tr("Angle 1 is shown first. You can change the angle of each part of the clip in the Clip tab; "
                       "double-click the clip to open the sequence and fine-tune the sync by hand."));

    auto *form = new QFormLayout;
    form->addRow(tr("Sync:"), m_sync);
    form->addRow(tr("Sound from:"), m_audio);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_create = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
    m_create->setObjectName(QStringLiteral("multicamCreate"));
    m_create->setEnabled(m_media.size() >= 2 && m_audio->count() > 0);
    connect(m_create, &QPushButton::clicked, this, &MulticamDialog::start);
    connect(buttons, &QDialogButtonBox::rejected, this, &MulticamDialog::reject);
    connect(m_sync, &QComboBox::currentIndexChanged, this, [this] {
        m_measured = false;
        m_create->setText(tr("Create"));
    });

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(m_list, 1);
    lay->addWidget(m_progress);
    lay->addWidget(m_info);
    lay->addWidget(buttons);
    resize(560, 360);
}

MulticamDialog::~MulticamDialog()
{
    if (m_cancel)
        m_cancel->store(true);
}

void MulticamDialog::setSyncBySound(bool on)
{
    m_sync->setCurrentIndex(on ? 0 : 1);
}

void MulticamDialog::start()
{
    if (m_running || !m_create->isEnabled())
        return;
    if (m_sync->currentIndex() != 0 || m_measured) {
        create();
        return;
    }
    QStringList paths;
    for (const QUuid &id : m_media) {
        const MediaItem *m = m_ctl->project().item(id);
        paths << ((m && m->info.hasAudio() && m_ctl->isOnline(id)) ? m->path : QString());
    }
    m_running = true;
    m_create->setEnabled(false);
    m_sync->setEnabled(false);
    m_progress->setValue(0);
    m_progress->show();
    m_info->setText(tr("Listening to the sound of each recording..."));
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancel;
    QPointer<MulticamDialog> self(this);
    (void)QtConcurrent::run([self, paths, cancel] {
        QList<std::vector<float>> envelopes;
        for (const QString &path : paths) {
            std::vector<float> env;
            if (!path.isEmpty() && !cancel->load())
                AudioSync::envelope(path, env, nullptr, cancel.get());
            envelopes << env;
            const int done = int(envelopes.size());
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, done] {
                if (self)
                    self->m_progress->setValue(done);
            }, Qt::QueuedConnection);
        }
        if (cancel->load())
            return;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, envelopes] {
            if (self)
                self->measured(envelopes);
        }, Qt::QueuedConnection);
    });
}

void MulticamDialog::measured(const QList<std::vector<float>> &envelopes)
{
    m_running = false;
    m_measured = true;
    m_progress->hide();
    m_sync->setEnabled(true);
    m_create->setEnabled(true);
    const QList<AudioSync::Match> matches = AudioSync::align(envelopes);
    m_offsets.clear();
    m_found.clear();
    for (const AudioSync::Match &m : matches) {
        m_offsets << m.offset;
        m_found << m.valid;
    }
    showOffsets();
    if (!m_found.contains(false)) {
        create();
        return;
    }
    m_info->setText(tr("The sound of some recordings did not match (marked below); they start with angle 1. "
                       "Press Create to make the sequence anyway and line them up by hand, or choose sync by file start."));
    m_create->setText(tr("Create anyway"));
    emit finished(false);
}

void MulticamDialog::showOffsets()
{
    for (int i = 0; i < m_list->topLevelItemCount() && i < m_offsets.size(); ++i) {
        QTreeWidgetItem *row = m_list->topLevelItem(i);
        row->setText(2, m_found.value(i) ? tr("%1 s").arg(m_offsets[i], 0, 'f', 2) : tr("not found"));
    }
}

void MulticamDialog::create()
{
    const double fps = m_ctl->project().sequence.fps() > 0 ? m_ctl->project().sequence.fps() : 25.0;
    QList<qint64> starts;
    const bool bySound = m_sync->currentIndex() == 0;
    for (int i = 0; i < m_media.size(); ++i)
        starts << (bySound ? qint64(std::llround(m_offsets.value(i) * fps)) : 0);
    m_result = m_ctl->createMulticam(m_media, starts, m_audio->currentData().toInt());
    if (m_result.isNull()) {
        m_info->setText(tr("Could not create the multicamera sequence."));
        emit finished(false);
        return;
    }
    emit finished(true);
    accept();
}

void MulticamDialog::reject()
{
    if (m_cancel)
        m_cancel->store(true);
    m_running = false;
    QDialog::reject();
}

} // namespace bc
