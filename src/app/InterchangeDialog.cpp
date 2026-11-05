#include "app/InterchangeDialog.h"

#include "app/ProjectController.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>

namespace bc {

namespace {
constexpr const char *kDirKey = "interchange/dir";
constexpr const char *kFormatKey = "interchange/format";
} // namespace

InterchangeDialog::InterchangeDialog(ProjectController *controller, QWidget *parent)
    : QDialog(parent)
    , m_ctl(controller)
    , m_format(new QComboBox(this))
    , m_report(new QLabel(this))
    , m_info(new QLabel(this))
{
    setObjectName(QStringLiteral("interchangeDialog"));
    setWindowTitle(tr("Export for other editors"));
    m_format->setObjectName(QStringLiteral("interchangeFormat"));
    m_report->setObjectName(QStringLiteral("interchangeReport"));
    m_info->setObjectName(QStringLiteral("interchangeInfo"));

    m_format->addItem(tr("Final Cut Pro 7 XML (Premiere Pro, DaVinci Resolve)"), int(Interchange::Format::FcpXml));
    m_format->addItem(tr("OpenTimelineIO (.otio)"), int(Interchange::Format::Otio));
    m_format->addItem(tr("EDL (CMX 3600): video track V1 only"), int(Interchange::Format::Edl));
    const int saved = QSettings().value(QString::fromLatin1(kFormatKey), 0).toInt();
    m_format->setCurrentIndex(std::max(0, m_format->findData(saved)));

    auto *intro = new QLabel(tr("Writes the main sequence for Premiere Pro, DaVinci Resolve and other editors. "
                                "Only the edit travels: which part of which file lies where. The media files "
                                "are not copied; keep them where they are."),
                             this);
    intro->setWordWrap(true);
    m_report->setWordWrap(true);
    m_report->setTextFormat(Qt::PlainText);
    m_info->setWordWrap(true);
    m_info->setTextFormat(Qt::PlainText);
    m_info->setStyleSheet(QStringLiteral("color:#8a8a8a;"));

    auto *form = new QFormLayout;
    form->addRow(tr("Format:"), m_format);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    m_export = buttons->addButton(tr("Export..."), QDialogButtonBox::AcceptRole);
    m_export->setObjectName(QStringLiteral("interchangeExport"));
    connect(m_export, &QPushButton::clicked, this, &InterchangeDialog::chooseAndExport);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(intro);
    lay->addLayout(form);
    lay->addWidget(m_report);
    lay->addWidget(m_info);
    lay->addStretch(1);
    lay->addWidget(buttons);
    resize(560, 380);

    connect(m_format, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(QString::fromLatin1(kFormatKey), m_format->currentData().toInt());
        refresh();
    });
    refresh();
}

Interchange::Format InterchangeDialog::format() const
{
    return Interchange::Format(m_format->currentData().toInt());
}

void InterchangeDialog::setFormat(Interchange::Format format)
{
    m_format->setCurrentIndex(std::max(0, m_format->findData(int(format))));
}

QString InterchangeDialog::lossText(Interchange::Loss loss, int n)
{
    using L = Interchange::Loss;
    switch (loss) {
    case L::Titles: return tr("Text titles: %1").arg(n);
    case L::PictureEffects: return tr("Picture effects, chroma key and masks: %1 clips").arg(n);
    case L::Color: return tr("Colour correction and LUTs: %1 clips").arg(n);
    case L::Motion: return tr("Position, scale, rotation and opacity: %1 clips").arg(n);
    case L::Speed: return tr("Speed changes: %1 clips (they play at normal speed from the same first frame)").arg(n);
    case L::TransitionKinds: return tr("Dips to black or white and wipes: %1 (they become cross dissolves)").arg(n);
    case L::Transitions: return tr("Transitions: %1").arg(n);
    case L::EndFades: return tr("Fades at free clip ends: %1").arg(n);
    case L::SoundFades: return tr("Sound fades in and out: %1 clips").arg(n);
    case L::ClipVolume: return tr("Clip volume: %1 clips").arg(n);
    case L::SoundEffects: return tr("Sound effects (equalizer, compressor, noise, hum): %1 clips").arg(n);
    case L::Mixer: return tr("Track volume and balance, master volume");
    case L::Subtitles: return tr("Subtitles: %1 (save them with Export SRT in the Subtitles tab)").arg(n);
    case L::Markers: return tr("Markers: %1").arg(n);
    case L::OtherTracks: return tr("Clips on tracks other than V1, and all sound: %1").arg(n);
    }
    return {};
}

void InterchangeDialog::refresh()
{
    const Interchange::Result r = m_ctl->interchangeResult(format());
    m_lines.clear();
    for (auto it = r.losses.cbegin(); it != r.losses.cend(); ++it)
        m_lines << lossText(it.key(), it.value());
    if (m_lines.isEmpty()) {
        m_report->setText(tr("Everything in this sequence will be transferred."));
    } else {
        QString text = tr("Will not be transferred:");
        for (const QString &l : m_lines)
            text += QStringLiteral("\n• ") + l;
        m_report->setText(text);
    }
    QStringList info;
    info << tr("Clips to write: %1").arg(r.clips);
    if (r.flattened > 0)
        info << tr("Nested and multicamera clips: %1. They are written as the clips inside them.").arg(r.flattened);
    if (r.missingMedia > 0)
        info << tr("Missing media files: %1. The other editor will ask for them.").arg(r.missingMedia);
    m_info->setText(info.join(QLatin1Char('\n')));
    m_export->setEnabled(r.clips > 0);
}

bool InterchangeDialog::exportTo(const QString &path, QString *error)
{
    QString err;
    if (!m_ctl->exportInterchange(format(), path, &err)) {
        if (error)
            *error = err;
        return false;
    }
    m_saved = path;
    QSettings().setValue(QString::fromLatin1(kDirKey), QFileInfo(path).absolutePath());
    return true;
}

void InterchangeDialog::chooseAndExport()
{
    QString dir = QSettings().value(QString::fromLatin1(kDirKey)).toString();
    if (dir.isEmpty())
        dir = m_ctl->filePath().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                          : QFileInfo(m_ctl->filePath()).absolutePath();
    const QString suffix = Interchange::fileSuffix(format());
    QString filter;
    switch (format()) {
    case Interchange::Format::FcpXml: filter = tr("Final Cut Pro XML (*.xml)"); break;
    case Interchange::Format::Otio: filter = tr("OpenTimelineIO (*.otio)"); break;
    case Interchange::Format::Edl: filter = tr("EDL (*.edl)"); break;
    }
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export for other editors"),
        QDir(dir).filePath(m_ctl->project().name + QLatin1Char('.') + suffix), filter);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QLatin1Char('.') + suffix;
    QString error;
    if (!exportTo(path, &error)) {
        QMessageBox::warning(this, tr("Export for other editors"), error);
        return;
    }
    accept();
}

} // namespace bc
