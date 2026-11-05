#include "app/SpeechDialog.h"

#include "app/ProjectController.h"
#include "core/Speech.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace bc {

QString SpeechDialog::languageName(const QString &lang)
{
    if (lang == QLatin1String("uz"))
        return tr("Uzbek");
    if (lang == QLatin1String("ru"))
        return tr("Russian");
    if (lang == QLatin1String("en"))
        return tr("English");
    return tr("Other language");
}

SpeechDialog::SpeechDialog(ProjectController *controller, const QList<QUuid> &selection, QWidget *parent)
    : QDialog(parent)
    , m_ctl(controller)
    , m_clips(controller->speechClips(selection))
    , m_model(new QComboBox(this))
    , m_download(new QPushButton(this))
    , m_folder(new QPushButton(this))
    , m_cyrillic(new QCheckBox(this))
    , m_chars(new QSpinBox(this))
    , m_replace(new QCheckBox(this))
    , m_progress(new QProgressBar(this))
    , m_status(new QLabel(this))
    , m_start(nullptr)
{
    setObjectName(QStringLiteral("speechDialog"));
    setWindowTitle(tr("Subtitles from speech"));
    m_model->setObjectName(QStringLiteral("speechModel"));
    m_download->setObjectName(QStringLiteral("speechDownload"));
    m_folder->setObjectName(QStringLiteral("speechFolder"));
    m_cyrillic->setObjectName(QStringLiteral("speechCyrillic"));
    m_chars->setObjectName(QStringLiteral("speechChars"));
    m_replace->setObjectName(QStringLiteral("speechReplace"));
    m_progress->setObjectName(QStringLiteral("speechProgress"));
    m_status->setObjectName(QStringLiteral("speechStatus"));

    QSettings s;
    m_download->setText(tr("Download"));
    m_folder->setText(tr("Model folder..."));
    m_folder->setToolTip(tr("Use a Vosk model you already have (an unpacked folder)"));
    m_cyrillic->setText(tr("Write Uzbek in Cyrillic letters"));
    m_cyrillic->setChecked(s.value(QStringLiteral("speech/cyrillic"), true).toBool());
    m_chars->setRange(20, 80);
    m_chars->setValue(s.value(QStringLiteral("speech/chars"), 42).toInt());
    m_chars->setSuffix(tr(" letters"));
    m_replace->setText(tr("Replace the subtitles there were where these clips are"));
    m_replace->setChecked(s.value(QStringLiteral("speech/replace"), true).toBool());
    m_progress->setRange(0, 1000);
    m_status->setWordWrap(true);

    auto *modelRow = new QHBoxLayout;
    modelRow->addWidget(m_model, 1);
    modelRow->addWidget(m_download);
    modelRow->addWidget(m_folder);
    auto *form = new QFormLayout;
    form->addRow(tr("Language:"), modelRow);
    form->addRow(QString(), m_cyrillic);
    form->addRow(tr("Line length:"), m_chars);
    form->addRow(QString(), m_replace);

    auto *note = new QLabel(tr("Speech is recognized on this computer with Vosk (free, Apache 2.0); the sound is not "
                               "sent anywhere. A model is downloaded once (about 40-50 MB) from alphacephei.com. "
                               "About one word in eight may be wrong: read the subtitles through in the Subtitles "
                               "panel. Each recording is heard once and remembered."),
                            this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#8a8a8a;"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Close"));
    m_start = buttons->addButton(tr("Make subtitles"), QDialogButtonBox::ActionRole);
    m_start->setObjectName(QStringLiteral("speechStart"));
    connect(m_start, &QPushButton::clicked, this, [this] {
        if (m_waiting) {
            m_ctl->cancelSpeech();
            return;
        }
        start();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(m_progress);
    lay->addWidget(m_status);
    lay->addWidget(note);
    lay->addStretch();
    lay->addWidget(buttons);
    setMinimumWidth(560);

    connect(m_model, &QComboBox::currentIndexChanged, this, &SpeechDialog::refresh);
    connect(m_download, &QPushButton::clicked, this, &SpeechDialog::download);
    connect(m_folder, &QPushButton::clicked, this, &SpeechDialog::chooseFolder);
    connect(m_ctl, &ProjectController::speechProgress, this, [this](double f) {
        if (m_waiting)
            m_progress->setValue(int(f * 1000));
    });
    connect(m_ctl, &ProjectController::speechReady, this, &SpeechDialog::heard);

    fillModels(s.value(QStringLiteral("speech/model")).toString());
    refresh();
}

SpeechDialog::~SpeechDialog()
{
    if (m_reply)
        m_reply->abort();
    if (m_waiting)
        m_ctl->cancelSpeech();
    QSettings s;
    s.setValue(QStringLiteral("speech/cyrillic"), m_cyrillic->isChecked());
    s.setValue(QStringLiteral("speech/chars"), m_chars->value());
    s.setValue(QStringLiteral("speech/replace"), m_replace->isChecked());
    if (!modelDir().isEmpty())
        s.setValue(QStringLiteral("speech/model"), modelDir());
}

void SpeechDialog::fillModels(const QString &select)
{
    m_model->blockSignals(true);
    m_model->clear();
    const QStringList installed = Speech::installedModels();
    QStringList installedIds;
    for (const QString &dir : installed) {
        installedIds << QFileInfo(dir).fileName();
        m_model->addItem(tr("%1 (%2)").arg(languageName(Speech::languageOf(dir)), QFileInfo(dir).fileName()), dir);
    }
    // Models that can be downloaded: data "download:<id>".
    for (const Speech::ModelInfo &m : Speech::knownModels())
        if (!installedIds.contains(m.id))
            m_model->addItem(tr("%1: download (%2 MB)").arg(languageName(m.language)).arg(m.sizeMb),
                             QStringLiteral("download:") + m.id);
    int at = m_model->findData(select);
    if (at < 0) // Uzbek first, if there
        for (int i = 0; i < m_model->count() && at < 0; ++i)
            if (m_model->itemData(i).toString().contains(QLatin1String("-uz-")))
                at = i;
    m_model->setCurrentIndex(std::max(0, at));
    m_model->blockSignals(false);
}

QString SpeechDialog::modelDir() const
{
    const QString data = m_model->currentData().toString();
    return data.startsWith(QLatin1String("download:")) ? QString() : data;
}

void SpeechDialog::refresh()
{
    const QString dir = modelDir();
    const bool downloading = m_reply != nullptr;
    m_download->setVisible(dir.isEmpty());
    m_download->setEnabled(!downloading && !m_waiting);
    m_folder->setEnabled(!downloading && !m_waiting);
    m_model->setEnabled(!downloading && !m_waiting);
    m_cyrillic->setVisible(dir.isEmpty() ? m_model->currentData().toString().contains(QLatin1String("-uz-"))
                                         : Speech::languageOf(dir) == QLatin1String("uz"));
    m_progress->setVisible(downloading || m_waiting);
    m_start->setText(m_waiting ? tr("Stop") : tr("Make subtitles"));
    if (downloading || m_waiting)
        return;
    QString why;
    if (m_clips.isEmpty())
        m_status->setText(tr("Select the clips to hear on the timeline (or none: then the first audio track is heard)."));
    else if (dir.isEmpty())
        m_status->setText(tr("Download a model for this language first, or choose a model folder."));
    else if (!Speech::libraryAvailable(&why))
        m_status->setText(tr("The speech recognition library (libvosk) is missing: %1").arg(why));
    else
        m_status->setText(tr("Clips to hear: %1.").arg(m_clips.size()));
    m_start->setEnabled(!m_clips.isEmpty() && !dir.isEmpty() && Speech::libraryAvailable());
}

void SpeechDialog::download()
{
    const QString id = m_model->currentData().toString().mid(QStringLiteral("download:").size());
    QString url;
    for (const Speech::ModelInfo &m : Speech::knownModels())
        if (m.id == id)
            url = m.url;
    if (url.isEmpty())
        return;
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    const QString zip = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(id + QStringLiteral(".zip"));
    m_file = new QSaveFile(zip, this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        m_status->setText(tr("Cannot write %1.").arg(QDir::toNativeSeparators(zip)));
        delete m_file;
        m_file = nullptr;
        return;
    }
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_net->get(req);
    m_status->setText(tr("Downloading the model..."));
    m_progress->setValue(0);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] { m_file->write(m_reply->readAll()); });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0)
            m_progress->setValue(int(got * 1000 / total));
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, zip, id] {
        m_file->write(m_reply->readAll());
        const bool ok = m_reply->error() == QNetworkReply::NoError;
        const QString err = m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        if (!ok || !m_file->commit()) {
            m_file->cancelWriting();
            m_status->setText(tr("The model could not be downloaded: %1").arg(ok ? m_file->errorString() : err));
        } else {
            m_status->setText(tr("Unpacking the model..."));
            QString why;
            const QString dir = Speech::installZip(zip, &why);
            QFile::remove(zip);
            if (dir.isEmpty()) {
                m_status->setText(tr("The model could not be unpacked: %1").arg(why));
            } else {
                fillModels(dir);
            }
        }
        delete m_file;
        m_file = nullptr;
        refresh();
    });
    refresh();
}

void SpeechDialog::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder of a Vosk model"));
    if (dir.isEmpty())
        return;
    if (!Speech::isModelFolder(dir)) {
        m_status->setText(tr("This folder holds no Vosk model (no am/final.mdl in it)."));
        return;
    }
    Speech::addModelFolder(dir);
    fillModels(QFileInfo(dir).absoluteFilePath());
    refresh();
}

void SpeechDialog::start()
{
    const QString dir = modelDir();
    if (dir.isEmpty() || m_clips.isEmpty() || m_waiting)
        return;
    m_waiting = true;
    m_progress->setValue(0);
    m_status->setText(tr("Listening... you can keep working. A minute of sound takes about half a minute."));
    refresh();
    m_ctl->transcribeClips(m_clips, dir); // speechReady comes now or later
}

void SpeechDialog::heard(bool ok, const QString &error)
{
    if (!m_waiting)
        return;
    m_waiting = false;
    if (!ok) {
        refresh();
        m_status->setText(error.isEmpty() ? tr("Stopped.") : tr("Speech could not be recognized: %1").arg(error));
        return;
    }
    Speech::CueSettings set;
    set.maxChars = m_chars->value();
    m_added = m_ctl->addSpeechSubtitles(m_clips, modelDir(), set, m_cyrillic->isChecked() && m_cyrillic->isVisibleTo(this),
                                        m_replace->isChecked());
    refresh();
    m_status->setText(m_added > 0 ? tr("Subtitles added: %1. Ctrl+Z removes them.").arg(m_added)
                                  : tr("No speech was found in these clips."));
    if (m_added > 0)
        accept();
}

} // namespace bc
