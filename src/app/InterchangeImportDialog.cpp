#include "app/InterchangeImportDialog.h"

#include "app/ProjectController.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>

#include <cmath>

namespace bc {

namespace {
constexpr const char *kModeKey = "interchange/importNested";

QString fpsText(double fps)
{
    return QString::number(fps, 'g', 6);
}
} // namespace

InterchangeImportDialog::InterchangeImportDialog(ProjectController *controller, const QString &path, QWidget *parent)
    : QDialog(parent)
    , m_ctl(controller)
    , m_path(path)
    , m_summary(new QLabel(this))
    , m_report(new QLabel(this))
    , m_asNest(new QRadioButton(this))
    , m_direct(new QRadioButton(this))
{
    setObjectName(QStringLiteral("interchangeImportDialog"));
    setWindowTitle(tr("Import from other editors"));
    m_summary->setObjectName(QStringLiteral("interchangeImportSummary"));
    m_report->setObjectName(QStringLiteral("interchangeImportReport"));
    m_asNest->setObjectName(QStringLiteral("interchangeImportNested"));
    m_direct->setObjectName(QStringLiteral("interchangeImportDirect"));
    for (QLabel *l : {m_summary, m_report}) {
        l->setWordWrap(true);
        l->setTextFormat(Qt::PlainText);
    }
    m_asNest->setText(tr("As one nested clip at the playhead (double-click it to edit)"));
    m_direct->setText(tr("Clips straight onto the open sequence at the playhead"));
    const bool nested = QSettings().value(QString::fromLatin1(kModeKey), true).toBool();
    (nested ? m_asNest : m_direct)->setChecked(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    m_import = buttons->addButton(tr("Import"), QDialogButtonBox::AcceptRole);
    m_import->setObjectName(QStringLiteral("interchangeImport"));
    connect(m_import, &QPushButton::clicked, this, [this] {
        if (importNow() > 0)
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_summary);
    lay->addWidget(m_report);
    lay->addWidget(m_asNest);
    lay->addWidget(m_direct);
    lay->addStretch(1);
    lay->addWidget(buttons);
    resize(580, 420);

    Interchange::ReadResult r;
    QString error;
    m_readable = m_ctl->readInterchange(path, &r, &error);
    QStringList info;
    info << tr("File: %1").arg(QDir::toNativeSeparators(path));
    if (!m_readable) {
        info << error;
        m_summary->setText(info.join(QLatin1Char('\n')));
        m_report->hide();
        m_asNest->setEnabled(false);
        m_direct->setEnabled(false);
        m_import->setEnabled(false);
        return;
    }
    QString format;
    switch (r.format) {
    case Interchange::Format::FcpXml: format = tr("Final Cut Pro XML"); break;
    case Interchange::Format::Otio: format = tr("OpenTimelineIO"); break;
    case Interchange::Format::Edl: format = tr("EDL (CMX 3600)"); break;
    }
    info << tr("Format: %1").arg(format);
    info << tr("Clips: %1 on %2 video and %3 audio tracks").arg(r.clips.size()).arg(r.videoTracks).arg(r.audioTracks);
    int found = 0, missing = 0;
    m_ctl->interchangeMediaCount(r, QFileInfo(path).absolutePath(), &found, &missing);
    info << tr("Media files: %1 found, %2 not found. Files not found stay offline; relink them later in the media bin.")
                .arg(found)
                .arg(missing);
    const double seqFps = m_ctl->project().sequence.fps();
    if (r.format == Interchange::Format::Edl) {
        info << tr("An EDL has no frame rate: the sequence rate (%1 fps) is used.").arg(fpsText(seqFps));
        info << tr("EDL source times are timecodes: for camera files with their own start timecode the clips may show "
                   "the wrong part. XML and OpenTimelineIO do not have this problem.");
    } else if (r.fpsNum > 0) {
        const double fileFps = double(r.fpsNum) / r.fpsDen;
        if (std::abs(fileFps - seqFps) > 1e-3)
            info << tr("The file runs at %1 fps, the sequence at %2 fps: times are converted to the sequence.")
                        .arg(fpsText(fileFps), fpsText(seqFps));
    }
    m_summary->setText(info.join(QLatin1Char('\n')));

    for (auto it = r.losses.cbegin(); it != r.losses.cend(); ++it)
        m_lines << lossText(it.key(), it.value());
    if (m_lines.isEmpty()) {
        m_report->setText(tr("Everything in the file will be imported."));
    } else {
        QString text = tr("Not imported:");
        for (const QString &l : m_lines)
            text += QStringLiteral("\n• ") + l;
        m_report->setText(text);
    }
}

void InterchangeImportDialog::setNested(bool nested)
{
    (nested ? m_asNest : m_direct)->setChecked(true);
}

QString InterchangeImportDialog::lossText(Interchange::ImportLoss loss, int n)
{
    using L = Interchange::ImportLoss;
    switch (loss) {
    case L::Generators: return tr("Titles, colour mattes and other generated clips: %1").arg(n);
    case L::Effects: return tr("Effects, motion or colour settings: %1 clips").arg(n);
    case L::Speed: return tr("Speed changes: %1 clips (they play at normal speed)").arg(n);
    case L::Nested: return tr("Nested sequences and multicamera clips: %1").arg(n);
    case L::TransitionKinds: return tr("Transitions other than dissolves: %1 (they become dissolves)").arg(n);
    case L::DisabledClips: return tr("Clips switched off: %1").arg(n);
    case L::NoMedia: return tr("Clips without a media file: %1").arg(n);
    }
    return {};
}

int InterchangeImportDialog::importNow(QString *error)
{
    if (!m_readable)
        return 0;
    const bool nested = m_asNest->isChecked();
    QSettings().setValue(QString::fromLatin1(kModeKey), nested);
    QString err;
    m_imported = m_ctl->importInterchange(m_path, nested, &err);
    if (m_imported <= 0) {
        if (error)
            *error = err;
        m_report->setText(err);
        m_report->show();
    }
    return m_imported;
}

} // namespace bc
