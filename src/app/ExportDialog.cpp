#include "app/ExportDialog.h"

#include "app/Formatting.h"
#include "app/ProjectController.h"
#include "engine/Exporter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace bc {

namespace {
const char *kLastDirKey = "export/lastDir";
const char *kQualityKey = "export/quality";

QString minutesSeconds(qint64 ms)
{
    const qint64 s = (ms + 999) / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

ExportDialog::ExportDialog(ProjectController *controller, QWidget *parent)
    : QDialog(parent)
    , m_ctl(controller)
    , m_encoders(MediaProbe::usableVideoEncoders())
    , m_path(new QLineEdit(this))
    , m_browse(new QPushButton(this))
    , m_quality(new QComboBox(this))
    , m_encoder(new QComboBox(this))
    , m_burnSubs(new QCheckBox(this))
    , m_writeSrt(new QCheckBox(this))
    , m_info(new QLabel(this))
    , m_warning(new QLabel(this))
    , m_progress(new QProgressBar(this))
    , m_status(new QLabel(this))
    , m_start(new QPushButton(this))
    , m_close(new QPushButton(this))
    , m_openFolder(new QPushButton(this))
{
    setObjectName(QStringLiteral("exportDialog"));
    setWindowTitle(tr("Export to MP4"));
    setMinimumWidth(520);
    m_path->setObjectName(QStringLiteral("exportPath"));
    m_quality->setObjectName(QStringLiteral("exportQuality"));
    m_encoder->setObjectName(QStringLiteral("exportEncoder"));
    m_start->setObjectName(QStringLiteral("exportStart"));
    m_close->setObjectName(QStringLiteral("exportClose"));
    m_progress->setObjectName(QStringLiteral("exportProgress"));
    m_status->setObjectName(QStringLiteral("exportStatus"));
    m_openFolder->setObjectName(QStringLiteral("exportOpenFolder"));

    auto *lay = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(m_browse);
    m_browse->setText(tr("Browse..."));
    form->addRow(tr("File:"), pathRow);
    m_quality->addItems({tr("High (large file)"), tr("Medium"), tr("Small file")});
    m_quality->setCurrentIndex(QSettings().value(QString::fromLatin1(kQualityKey), 1).toInt());
    form->addRow(tr("Quality:"), m_quality);
    for (const auto &e : m_encoders)
        m_encoder->addItem(e.label, e.codec);
    form->addRow(tr("Video codec:"), m_encoder);
    // Subtitles (phase 11): only when the sequence has some.
    m_burnSubs->setObjectName(QStringLiteral("exportBurnSubtitles"));
    m_writeSrt->setObjectName(QStringLiteral("exportWriteSrt"));
    m_burnSubs->setText(tr("Burn the subtitles into the picture"));
    m_writeSrt->setText(tr("Also save the subtitles as an .srt file next to the video"));
    m_burnSubs->setChecked(m_ctl->project().timeline.subtitleStyle.show);
    m_writeSrt->setChecked(QSettings().value(QStringLiteral("export/writeSrt"), false).toBool());
    if (!m_ctl->project().timeline.subtitles.isEmpty()) {
        form->addRow(tr("Subtitles:"), m_burnSubs);
        form->addRow(QString(), m_writeSrt);
    } else {
        m_burnSubs->hide();
        m_writeSrt->hide();
    }
    lay->addLayout(form);
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color:#9a9a9a;"));
    lay->addWidget(m_info);
    m_warning->setWordWrap(true);
    m_warning->setStyleSheet(QStringLiteral("color:#d1a54a;"));
    lay->addWidget(m_warning);
    lay->addWidget(m_progress);
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    auto *buttons = new QHBoxLayout;
    m_openFolder->setText(tr("Open folder"));
    m_openFolder->hide();
    buttons->addWidget(m_openFolder);
    buttons->addStretch();
    m_start->setText(tr("Export"));
    m_start->setDefault(true);
    buttons->addWidget(m_start);
    m_close->setText(tr("Close"));
    buttons->addWidget(m_close);
    lay->addLayout(buttons);

    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->hide();
    m_path->setText(defaultPath());

    connect(m_browse, &QPushButton::clicked, this, &ExportDialog::browse);
    connect(m_quality, &QComboBox::currentIndexChanged, this, &ExportDialog::updateInfo);
    connect(m_encoder, &QComboBox::currentIndexChanged, this, &ExportDialog::updateInfo);
    connect(m_start, &QPushButton::clicked, this, &ExportDialog::startExport);
    connect(m_close, &QPushButton::clicked, this, &ExportDialog::reject);
    connect(m_openFolder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path()).absolutePath()));
    });
    connect(m_ctl, &ProjectController::exportProgress, this, &ExportDialog::onProgress);
    connect(m_ctl, &ProjectController::exportFinished, this, &ExportDialog::onFinished);
    updateInfo();
}

QString ExportDialog::defaultPath() const
{
    QString dir = QSettings().value(QString::fromLatin1(kLastDirKey)).toString();
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QString name = m_ctl->displayName();
    if (name.endsWith(QLatin1String(".bcproj"), Qt::CaseInsensitive))
        name.chop(7);
    if (name.isEmpty())
        name = QStringLiteral("BUKHARA CINEMA");
    return QDir(dir).filePath(name + QStringLiteral(".mp4"));
}

QString ExportDialog::path() const
{
    return m_path->text().trimmed();
}

void ExportDialog::setPath(const QString &path)
{
    m_path->setText(path);
}

void ExportDialog::browse()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Export to MP4"), path(),
                                                      tr("MP4 video (*.mp4)"));
    if (!file.isEmpty())
        m_path->setText(file.endsWith(QLatin1String(".mp4"), Qt::CaseInsensitive) ? file : file + QStringLiteral(".mp4"));
}

void ExportDialog::updateInfo()
{
    const SequenceSettings &seq = m_ctl->project().sequence;
    const double fps = seq.fps();
    const int kbps = Exporter::suggestedKbps(seq, m_quality->currentIndex());
    const qint64 frames = m_ctl->timeline().duration();
    const double seconds = fps > 0 ? frames / fps : 0;
    const double mb = seconds * (kbps + 192) / 8.0 / 1000.0;
    m_info->setText(tr("Picture: %1, %2 fps (sequence settings), %3 Mbit/s. Sound: AAC 192 kbit/s, %4 Hz. "
                       "Length %5, file about %6 MB.")
                        .arg(Fmt::resolution(seq.width, seq.height), Fmt::frameRate(seq.fpsNum, seq.fpsDen))
                        .arg(QString::number(kbps / 1000.0, 'f', 1))
                        .arg(seq.sampleRate)
                        .arg(Fmt::frameTimecode(frames, fps), QString::number(std::max(1.0, mb), 'f', 0)));
    const bool h264 = m_encoder->currentIndex() >= 0 && m_encoders.value(m_encoder->currentIndex()).isH264;
    m_warning->setText(h264 ? QString()
                            : tr("H.264 is not available on this computer (no Intel Quick Sync or Windows Media "
                                 "Foundation encoder was found). The file will use MPEG-4 Part 2: it plays in most "
                                 "players, but some websites may not accept it."));
    m_warning->setVisible(!h264);
}

bool ExportDialog::startExport()
{
    QString file = path();
    if (file.isEmpty()) {
        m_status->setText(tr("Choose a file name."));
        return false;
    }
    if (!file.endsWith(QLatin1String(".mp4"), Qt::CaseInsensitive)) {
        file += QStringLiteral(".mp4");
        m_path->setText(file);
    }
    if (QFileInfo::exists(file) && isVisible()
        && QMessageBox::question(this, windowTitle(), tr("The file already exists. Replace it?"))
               != QMessageBox::Yes)
        return false;
    QDir().mkpath(QFileInfo(file).absolutePath());

    ExportSettings s;
    s.path = file;
    s.encoder = m_encoders.value(std::max(0, m_encoder->currentIndex()));
    s.videoKbps = Exporter::suggestedKbps(m_ctl->project().sequence, m_quality->currentIndex());
    s.burnSubtitles = m_burnSubs->isChecked();
    s.writeSrt = m_writeSrt->isChecked();
    QString error;
    if (!m_ctl->startExport(s, &error)) {
        m_status->setText(error);
        return false;
    }
    QSettings settings;
    settings.setValue(QString::fromLatin1(kLastDirKey), QFileInfo(file).absolutePath());
    settings.setValue(QString::fromLatin1(kQualityKey), m_quality->currentIndex());
    if (!m_ctl->project().timeline.subtitles.isEmpty())
        settings.setValue(QStringLiteral("export/writeSrt"), m_writeSrt->isChecked());
    m_openFolder->hide();
    m_status->setText(tr("Exporting..."));
    setRunning(true);
    return true;
}

void ExportDialog::setRunning(bool running)
{
    m_path->setEnabled(!running);
    m_browse->setEnabled(!running);
    m_quality->setEnabled(!running);
    m_encoder->setEnabled(!running);
    m_burnSubs->setEnabled(!running);
    m_writeSrt->setEnabled(!running);
    m_start->setEnabled(!running);
    m_progress->setVisible(true);
    m_close->setText(running ? tr("Cancel export") : tr("Close"));
}

void ExportDialog::onProgress(qint64 frame, qint64 total, qint64 remainingMs)
{
    if (total <= 0)
        return;
    m_progress->setValue(int(frame * 1000 / total));
    QString text = tr("Frame %1 of %2").arg(frame).arg(total);
    if (remainingMs >= 0 && frame < total)
        text += QStringLiteral(" · ") + tr("about %1 left").arg(minutesSeconds(remainingMs));
    m_status->setText(text);
}

void ExportDialog::onFinished(bool ok, const QString &error)
{
    setRunning(false);
    if (ok) {
        m_progress->setValue(1000);
        m_status->setText(tr("Done: %1").arg(QDir::toNativeSeparators(path())));
        m_openFolder->show();
    } else if (error.isEmpty()) {
        m_progress->setValue(0);
        m_status->setText(tr("Export cancelled. The unfinished file was deleted."));
    } else {
        m_status->setText(tr("Export failed: %1").arg(error));
    }
}

void ExportDialog::reject()
{
    if (m_ctl->isExporting()) {
        m_ctl->cancelExport(); // the dialog stays open and says so
        return;
    }
    QDialog::reject();
}

} // namespace bc
