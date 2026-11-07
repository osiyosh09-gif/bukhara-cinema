#include "app/ClaudeAssistantPanel.h"

#include "app/ClaudeAssistant.h"
#include "app/ProjectController.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace bc {

ClaudeAssistantPanel::ClaudeAssistantPanel(ProjectController *controller, QWidget *parent)
    : QWidget(parent), m_ctl(controller), m_assistant(new ClaudeAssistant(controller, this))
    , m_keyStatus(new QLabel(this)), m_keyEdit(new QLineEdit(this)), m_keySave(new QPushButton(this))
    , m_log(new QPlainTextEdit(this)), m_input(new QLineEdit(this)), m_send(new QPushButton(this))
{
    setObjectName(QStringLiteral("claudeAssistantPanel"));
    m_keyStatus->setObjectName(QStringLiteral("claudeKeyStatus"));
    m_keyStatus->setWordWrap(true);
    m_keyStatus->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    m_keyEdit->setObjectName(QStringLiteral("claudeKeyEdit"));
    m_keyEdit->setEchoMode(QLineEdit::Password);
    m_keySave->setObjectName(QStringLiteral("claudeKeySave"));
    m_keySave->setText(tr("Save"));
    connect(m_keySave, &QPushButton::clicked, this, &ClaudeAssistantPanel::saveKey);
    connect(m_keyEdit, &QLineEdit::returnPressed, this, &ClaudeAssistantPanel::saveKey);

    m_log->setObjectName(QStringLiteral("claudeLog"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);

    m_input->setObjectName(QStringLiteral("claudeInput"));
    m_input->setPlaceholderText(tr("Type a command, e.g. \"sync the cameras\""));
    m_send->setObjectName(QStringLiteral("claudeSend"));
    m_send->setText(tr("Send"));
    connect(m_input, &QLineEdit::returnPressed, this, &ClaudeAssistantPanel::sendCommand);
    connect(m_send, &QPushButton::clicked, this, &ClaudeAssistantPanel::sendCommand);

    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(m_keyEdit, 1);
    keyRow->addWidget(m_keySave);
    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_send);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_keyStatus);
    lay->addLayout(keyRow);
    lay->addWidget(m_log, 1);
    lay->addLayout(inputRow);

    connect(m_assistant, &ClaudeAssistant::busyChanged, this, [this](bool busy) {
        m_send->setEnabled(!busy);
        m_input->setEnabled(!busy);
    });
    connect(m_assistant, &ClaudeAssistant::reply, this, [this](const QString &text) {
        appendLine(tr("Claude:"), text);
    });
    connect(m_assistant, &ClaudeAssistant::actionStarted, this, [this](const QString &name) {
        appendLine(tr("Running:"), name);
    });
    connect(m_assistant, &ClaudeAssistant::failed, this, [this](const QString &message) {
        appendLine(tr("Error:"), message);
    });
    connect(m_ctl, &ProjectController::claudeCommandFinished, this, [this](bool ok, const QString &message) {
        appendLine(ok ? tr("Done:") : tr("Not done:"), message);
    });

    refreshKeyRow();
}

void ClaudeAssistantPanel::refreshKeyRow()
{
    const bool has = m_assistant->hasApiKey();
    m_keyStatus->setText(has
        ? tr("API key saved. Every command is sent to Claude over the internet and costs a little.")
        : tr("No API key entered: commands will not work. Get one from console.anthropic.com."));
    m_keyEdit->clear();
    m_keyEdit->setPlaceholderText(has ? tr("Type a new key to replace it")
                                      : tr("Anthropic API key (console.anthropic.com)"));
}

void ClaudeAssistantPanel::saveKey()
{
    const QString key = m_keyEdit->text().trimmed();
    if (key.isEmpty())
        return;
    ClaudeAssistant::setApiKey(key);
    refreshKeyRow();
    appendLine(tr("System:"), tr("API key saved."));
}

void ClaudeAssistantPanel::sendCommand()
{
    const QString text = m_input->text().trimmed();
    if (text.isEmpty())
        return;
    if (!m_assistant->hasApiKey()) {
        appendLine(tr("Error:"), tr("Enter the API key first, then press \"Save\"."));
        return;
    }
    appendLine(tr("You:"), text);
    m_input->clear();
    m_assistant->setMediaSelection(m_mediaSelectionSource ? m_mediaSelectionSource() : QList<QUuid>());
    m_assistant->setTimelineSelection(m_timelineSelectionSource ? m_timelineSelectionSource() : QList<QUuid>());
    m_assistant->send(text);
}

void ClaudeAssistantPanel::appendLine(const QString &prefix, const QString &text)
{
    m_log->appendPlainText(prefix + QLatin1Char(' ') + text);
}

} // namespace bc
