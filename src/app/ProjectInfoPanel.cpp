#include "app/ProjectInfoPanel.h"

#include "app/Formatting.h"
#include "app/ProjectController.h"

#include <QDir>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace bc {

ProjectInfoPanel::ProjectInfoPanel(ProjectController *controller, QWidget *parent)
    : QWidget(parent)
    , m_ctl(controller)
    , m_text(new QLabel(this))
    , m_seqBtn(new QPushButton(this))
{
    setObjectName(QStringLiteral("projectInfo"));
    m_text->setTextFormat(Qt::RichText);
    m_text->setWordWrap(true);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_text->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_text);
    lay->addWidget(m_seqBtn, 0, Qt::AlignLeft);
    lay->addStretch();

    connect(m_seqBtn, &QPushButton::clicked, this, &ProjectInfoPanel::sequenceSettingsRequested);
    for (auto sig : {&ProjectController::projectReset, &ProjectController::binChanged,
                     &ProjectController::sequenceChanged})
        connect(m_ctl, sig, this, &ProjectInfoPanel::refresh);
    connect(m_ctl, &ProjectController::itemUpdated, this, &ProjectInfoPanel::refresh);
    connect(m_ctl, &ProjectController::fileNameChanged, this, &ProjectInfoPanel::refresh);
    refresh();
}

void ProjectInfoPanel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        refresh();
    QWidget::changeEvent(event);
}

void ProjectInfoPanel::refresh()
{
    const Project &p = m_ctl->project();
    int video = 0, audio = 0, image = 0;
    qint64 bytes = 0;
    for (const MediaItem &m : p.items) {
        video += m.info.kind == MediaInfo::Kind::Video;
        audio += m.info.kind == MediaInfo::Kind::Audio;
        image += m.info.kind == MediaInfo::Kind::Image;
        bytes += m.info.fileSize;
    }
    const QString file = m_ctl->filePath().isEmpty() ? tr("not saved yet")
                                                     : QDir::toNativeSeparators(m_ctl->filePath()).toHtmlEscaped();
    QString html;
    html += QStringLiteral("<p><b>%1</b><br>%2</p>").arg(m_ctl->displayName().toHtmlEscaped(), file);
    html += QStringLiteral("<p><b>%1</b><br>%2</p>").arg(tr("Sequence"), Fmt::sequenceSummary(p.sequence));
    html += QStringLiteral("<p><b>%1</b><br>%2<br>%3</p>")
                .arg(tr("Media"),
                     tr("Video: %1 · Audio: %2 · Images: %3").arg(video).arg(audio).arg(image),
                     tr("Total size: %1").arg(Fmt::fileSize(bytes)));
    if (m_ctl->offlineCount() > 0)
        html += QStringLiteral("<p style='color:#e06060;'>%1</p>")
                    .arg(tr("Files not found: %1. Right-click them in the media bin and choose Relink.")
                             .arg(m_ctl->offlineCount()));
    m_text->setText(html);
    m_seqBtn->setText(tr("Sequence settings..."));
}

} // namespace bc
