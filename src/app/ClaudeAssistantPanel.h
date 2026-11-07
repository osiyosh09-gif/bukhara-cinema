#pragma once

#include <QList>
#include <QUuid>
#include <QWidget>

#include <functional>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;

namespace bc {

class ProjectController;
class ClaudeAssistant;

// Window > Claude yordamchisi (phase 17a): a small chat box where a typed
// Uzbek command runs one of two actions (see ClaudeAssistant) through the
// Claude Haiku model. Needs an internet connection and an Anthropic API key
// (kept on this computer only, in Settings); every command has a small
// cost. Off by default, like the scopes and the multicamera monitor.
class ClaudeAssistantPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ClaudeAssistantPanel(ProjectController *controller, QWidget *parent = nullptr);

    // sync_cameras runs on the media bin's selection, auto_cut_cameras on
    // the timeline's; MainWindow supplies both, the same way it supplies
    // the existing Sequence > Create multicamera sequence action.
    void setMediaSelectionSource(std::function<QList<QUuid>()> f) { m_mediaSelectionSource = std::move(f); }
    void setTimelineSelectionSource(std::function<QList<QUuid>()> f) { m_timelineSelectionSource = std::move(f); }

private:
    void sendCommand();
    void appendLine(const QString &prefix, const QString &text);
    void refreshKeyRow();
    void saveKey();

    ProjectController *m_ctl;
    ClaudeAssistant *m_assistant;
    QLabel *m_keyStatus;
    QLineEdit *m_keyEdit;
    QPushButton *m_keySave;
    QPlainTextEdit *m_log;
    QLineEdit *m_input;
    QPushButton *m_send;
    std::function<QList<QUuid>()> m_mediaSelectionSource;
    std::function<QList<QUuid>()> m_timelineSelectionSource;
};

} // namespace bc
