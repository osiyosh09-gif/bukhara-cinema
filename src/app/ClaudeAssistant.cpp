#include "app/ClaudeAssistant.h"

#include "app/ProjectController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

namespace bc {

namespace {

const char *kProviderSetting = "claude/provider"; // "anthropic" (default) | "gemini"
const char *kApiKeySettingAnthropic = "claude/apiKey";
const char *kApiKeySettingGemini = "claude/apiKeyGemini";
const char *kModelSettingAnthropic = "claude/model";
const char *kModelSettingGemini = "claude/geminiModel";
const char *kDefaultModelAnthropic = "claude-haiku-5-5"; // cheapest current Anthropic model: fits short commands well
const char *kDefaultModelGemini = "gemini-3.1-flash-lite"; // free-tier Google model with function calling
constexpr int kMaxTokens = 300;

// The only two things a command may ask the program to do. Kept tiny and
// fixed on purpose: the model only ever picks one of these two names (or
// replies in words), never arbitrary code or file paths. Shared between
// both providers' tool/function definitions below.
const QString kSyncDescription = QStringLiteral(
    "Tanlangan kamera yozuvlarini ovoz boyicha vaqt jihatidan tenglashtirib, "
    "bittalik multicam (ko'p kameralik) ketma-ketlik yaratadi. Foydalanuvchi "
    "\"sinxronla\", \"kameralarni tenglashtir\", \"multicam yarat\" kabi so'rasa chaqiriladi.");
const QString kCutDescription = QStringLiteral(
    "Mavjud multicam klipni kameralar orasida avtomatik kesadi: kim gapirayotgan "
    "bo'lsa, o'sha kamerani ko'rsatadi. Foydalanuvchi \"kes\", \"o'zi tanlab kessin\", "
    "\"montaj qil\" kabi so'rasa, va multicam allaqachon bor bo'lsa, chaqiriladi.");

// Fixed instructions to the model, not interface text: always Uzbek,
// whatever language the program's own interface is set to, because the
// commands themselves are Uzbek. Shared between both providers.
QString systemPrompt()
{
    return QStringLiteral(
        "Siz BUKHARA CINEMA video montaj dasturidagi yordamchisiz. Foydalanuvchi o'zbekcha "
        "(kirill yoki lotin) qisqa buyruq yozadi. Faqat ikki amal mavjud: sync_cameras (kameralarni "
        "ovoz boyicha sinxronlab multicam yaratish) va auto_cut_cameras (multicam klipni kameralar "
        "orasida avtomatik kesish). Buyruq shu ikkisidan biriga mos kelsa, mos vositani chaqiring, "
        "boshqa hech narsa yozmang. Mos kelmasa, vosita chaqirmasdan, qisqa va aniq javob yozing "
        "(kirill alifbosida), dastur nima qila olishini eslatib o'ting.");
}

QJsonArray anthropicToolDefinitions()
{
    QJsonObject noParams{{QStringLiteral("type"), QStringLiteral("object")},
                         {QStringLiteral("properties"), QJsonObject()}};
    QJsonObject sync{
        {QStringLiteral("name"), QStringLiteral("sync_cameras")},
        {QStringLiteral("description"), kSyncDescription},
        {QStringLiteral("input_schema"), noParams}};
    QJsonObject cut{
        {QStringLiteral("name"), QStringLiteral("auto_cut_cameras")},
        {QStringLiteral("description"), kCutDescription},
        {QStringLiteral("input_schema"), noParams}};
    return {sync, cut};
}

// Gemini: for a function with no parameters, "parameters" is left unset
// entirely (Google's own guidance) -- an empty-properties OBJECT schema is
// rejected by some Gemini model versions ("should be non-empty for OBJECT
// type"), so it is not used here.
QJsonArray geminiFunctionDeclarations()
{
    QJsonObject sync{
        {QStringLiteral("name"), QStringLiteral("sync_cameras")},
        {QStringLiteral("description"), kSyncDescription}};
    QJsonObject cut{
        {QStringLiteral("name"), QStringLiteral("auto_cut_cameras")},
        {QStringLiteral("description"), kCutDescription}};
    return {sync, cut};
}

} // namespace

ClaudeAssistant::ClaudeAssistant(ProjectController *controller, QObject *parent)
    : QObject(parent), m_ctl(controller), m_net(new QNetworkAccessManager(this))
{
}

ClaudeAssistant::~ClaudeAssistant()
{
    cancel();
}

ClaudeAssistant::Provider ClaudeAssistant::provider()
{
    const QString v = QSettings().value(QLatin1String(kProviderSetting), QStringLiteral("anthropic")).toString();
    return v == QStringLiteral("gemini") ? Provider::Gemini : Provider::Anthropic;
}

void ClaudeAssistant::setProvider(Provider p)
{
    QSettings().setValue(QLatin1String(kProviderSetting),
                          p == Provider::Gemini ? QStringLiteral("gemini") : QStringLiteral("anthropic"));
}

QString ClaudeAssistant::apiKey()
{
    const char *setting = provider() == Provider::Gemini ? kApiKeySettingGemini : kApiKeySettingAnthropic;
    return QSettings().value(QLatin1String(setting)).toString();
}

void ClaudeAssistant::setApiKey(const QString &key)
{
    QSettings s;
    const char *setting = provider() == Provider::Gemini ? kApiKeySettingGemini : kApiKeySettingAnthropic;
    if (key.isEmpty())
        s.remove(QLatin1String(setting));
    else
        s.setValue(QLatin1String(setting), key);
}

void ClaudeAssistant::cancel()
{
    if (m_reply) {
        m_reply->abort();
        m_reply = nullptr;
        emit busyChanged(false);
    }
}

void ClaudeAssistant::send(const QString &command)
{
    if (busy())
        return;
    const Provider p = provider();
    const QString key = apiKey();
    if (key.isEmpty()) {
        emit failed(p == Provider::Gemini ? tr("No Gemini API key has been entered yet (Settings).")
                                           : tr("No Claude API key has been entered yet (Settings)."));
        return;
    }
    if (command.trimmed().isEmpty())
        return;

    m_pendingProvider = p;
    if (p == Provider::Gemini)
        sendGemini(command, key);
    else
        sendAnthropic(command, key);
}

void ClaudeAssistant::sendAnthropic(const QString &command, const QString &key)
{
    const QJsonObject body{
        {QStringLiteral("model"), QSettings().value(QLatin1String(kModelSettingAnthropic), QLatin1String(kDefaultModelAnthropic)).toString()},
        {QStringLiteral("max_tokens"), kMaxTokens},
        {QStringLiteral("system"), systemPrompt()},
        {QStringLiteral("tools"), anthropicToolDefinitions()},
        {QStringLiteral("messages"),
         QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), command}}}}};

    QNetworkRequest req{QUrl(QStringLiteral("https://api.anthropic.com/v1/messages"))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("x-api-key", key.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");

    m_reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    emit busyChanged(true);
    connect(m_reply, &QNetworkReply::finished, this, &ClaudeAssistant::handleFinished);
}

void ClaudeAssistant::sendGemini(const QString &command, const QString &key)
{
    const QString model = QSettings().value(QLatin1String(kModelSettingGemini), QLatin1String(kDefaultModelGemini)).toString();

    const QJsonObject body{
        {QStringLiteral("systemInstruction"),
         QJsonObject{{QStringLiteral("parts"), QJsonArray{QJsonObject{{QStringLiteral("text"), systemPrompt()}}}}}},
        {QStringLiteral("tools"),
         QJsonArray{QJsonObject{{QStringLiteral("functionDeclarations"), geminiFunctionDeclarations()}}}},
        {QStringLiteral("generationConfig"), QJsonObject{{QStringLiteral("maxOutputTokens"), kMaxTokens}}},
        {QStringLiteral("contents"),
         QJsonArray{QJsonObject{
             {QStringLiteral("role"), QStringLiteral("user")},
             {QStringLiteral("parts"), QJsonArray{QJsonObject{{QStringLiteral("text"), command}}}}}}}};

    const QUrl url(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent").arg(model));
    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("x-goog-api-key", key.toUtf8());

    m_reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    emit busyChanged(true);
    connect(m_reply, &QNetworkReply::finished, this, &ClaudeAssistant::handleFinished);
}

void ClaudeAssistant::handleFinished()
{
    QNetworkReply *r = m_reply;
    m_reply = nullptr;
    const Provider used = m_pendingProvider;
    r->deleteLater();
    emit busyChanged(false);
    if (r->error() == QNetworkReply::OperationCanceledError)
        return; // cancel(): nothing to report
    const QByteArray data = r->readAll();
    if (r->error() != QNetworkReply::NoError) {
        // Both providers send a JSON body with a human-readable error even
        // on a non-200 reply ({"error": {"message": ...}}); show that when
        // there is one, else the network error.
        QString detail = r->errorString();
        const QJsonObject err = QJsonDocument::fromJson(data).object().value(QStringLiteral("error")).toObject();
        if (!err.value(QStringLiteral("message")).toString().isEmpty())
            detail = err.value(QStringLiteral("message")).toString();
        emit failed(used == Provider::Gemini ? tr("Error contacting Gemini: %1").arg(detail)
                                              : tr("Error contacting Claude: %1").arg(detail));
        return;
    }

    QStringList texts;
    if (used == Provider::Gemini) {
        const QJsonArray candidates = QJsonDocument::fromJson(data).object().value(QStringLiteral("candidates")).toArray();
        const QJsonArray parts = candidates.isEmpty()
            ? QJsonArray()
            : candidates.first().toObject().value(QStringLiteral("content")).toObject().value(QStringLiteral("parts")).toArray();
        for (const QJsonValue &v : parts) {
            const QJsonObject part = v.toObject();
            if (part.contains(QStringLiteral("functionCall"))) {
                const QString name = part.value(QStringLiteral("functionCall")).toObject().value(QStringLiteral("name")).toString();
                if (name == QStringLiteral("sync_cameras")) {
                    emit actionStarted(tr("Syncing the cameras"));
                    m_ctl->syncCamerasCommand(m_mediaSelection);
                } else if (name == QStringLiteral("auto_cut_cameras")) {
                    emit actionStarted(tr("Cutting the multicamera clip automatically"));
                    m_ctl->autoCutCamerasCommand(m_timelineSelection);
                }
            } else {
                const QString t = part.value(QStringLiteral("text")).toString();
                if (!t.isEmpty())
                    texts << t;
            }
        }
    } else {
        const QJsonArray content = QJsonDocument::fromJson(data).object().value(QStringLiteral("content")).toArray();
        for (const QJsonValue &v : content) {
            const QJsonObject item = v.toObject();
            const QString type = item.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("tool_use")) {
                const QString name = item.value(QStringLiteral("name")).toString();
                if (name == QStringLiteral("sync_cameras")) {
                    emit actionStarted(tr("Syncing the cameras"));
                    m_ctl->syncCamerasCommand(m_mediaSelection);
                } else if (name == QStringLiteral("auto_cut_cameras")) {
                    emit actionStarted(tr("Cutting the multicamera clip automatically"));
                    m_ctl->autoCutCamerasCommand(m_timelineSelection);
                }
            } else if (type == QStringLiteral("text")) {
                const QString t = item.value(QStringLiteral("text")).toString();
                if (!t.isEmpty())
                    texts << t;
            }
        }
    }
    if (!texts.isEmpty())
        emit reply(texts.join(QLatin1Char('\n')));
}

} // namespace bc
