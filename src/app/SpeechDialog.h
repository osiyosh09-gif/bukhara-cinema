#pragma once

#include <QDialog>
#include <QList>
#include <QUuid>

class QCheckBox;
class QComboBox;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QSaveFile;
class QSpinBox;

namespace bc {

class ProjectController;

// Graphics > Subtitles from speech (phase 14b): hears the chosen clips with a
// Vosk model on this computer and puts the words on the sequence as
// subtitles (one undo step). A model is downloaded once (or a folder chosen).
class SpeechDialog : public QDialog
{
    Q_OBJECT
public:
    SpeechDialog(ProjectController *controller, const QList<QUuid> &selection, QWidget *parent = nullptr);
    ~SpeechDialog() override;

    QString modelDir() const; // empty when the chosen entry is not installed
    void start();             // what the Make subtitles button does
    int added() const { return m_added; }
    static QString languageName(const QString &lang); // "uz" -> Uzbek


private:
    void fillModels(const QString &select = {});
    void refresh();
    void download();
    void chooseFolder();
    void heard(bool ok, const QString &error);

    ProjectController *m_ctl;
    QList<QUuid> m_clips;
    QComboBox *m_model;
    QPushButton *m_download;
    QPushButton *m_folder;
    QCheckBox *m_cyrillic;
    QSpinBox *m_chars;
    QCheckBox *m_replace;
    QProgressBar *m_progress;
    QLabel *m_status;
    QPushButton *m_start;
    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;
    QSaveFile *m_file = nullptr;
    bool m_waiting = false; // started and listening
    int m_added = -1;
};

} // namespace bc
