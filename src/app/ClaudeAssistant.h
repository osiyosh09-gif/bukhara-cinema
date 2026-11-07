#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

class QNetworkAccessManager;
class QNetworkReply;

namespace bc {

class ProjectController;

// Claude command assistant (phase 17a): turns one typed Uzbek command into a
// tool call for a small, fixed set of actions --
//   sync_cameras:     sync the chosen recordings by sound into a
//                      multicamera sequence (Sequence > Create multicamera
//                      sequence, sync by sound, without the dialog);
//   auto_cut_cameras: cut a multicamera clip between its cameras (Sequence >
//                      Automatic multicamera edit, with the last used, or
//                      default, settings, without the dialog) --
// using the Claude Haiku model over the internet (Anthropic's paid API: an
// internet connection and a small cost per command). The API key is kept in
// QSettings on this computer only, never in the project file or in git.
// Anything the model does not recognise as one of those two actions comes
// back as a short reply, shown as text; no project change is made for it.
class ClaudeAssistant : public QObject
{
    Q_OBJECT
public:
    explicit ClaudeAssistant(ProjectController *controller, QObject *parent = nullptr);
    ~ClaudeAssistant() override;

    static QString apiKey();
    static void setApiKey(const QString &key);
    bool hasApiKey() const { return !apiKey().isEmpty(); }

    // What sync_cameras / auto_cut_cameras run on: the panel sets these from
    // the media bin / timeline selection right before send().
    void setMediaSelection(const QList<QUuid> &ids) { m_mediaSelection = ids; }
    void setTimelineSelection(const QList<QUuid> &ids) { m_timelineSelection = ids; }

    bool busy() const { return m_reply != nullptr; }
    void send(const QString &command);
    void cancel();

signals:
    void reply(const QString &text);        // Claude's own words (no action taken)
    void actionStarted(const QString &name); // a tool was recognised; the controller is running it
    void failed(const QString &message);     // network / API problem, translated
    void busyChanged(bool busy);

private:
    void handleFinished();

    ProjectController *m_ctl;
    QNetworkAccessManager *m_net;
    QNetworkReply *m_reply = nullptr;
    QList<QUuid> m_mediaSelection;
    QList<QUuid> m_timelineSelection;
};

} // namespace bc
