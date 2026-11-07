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

const char *kApiKeySetting = "claude/apiKey";
const char *kModelSetting = "claude/model";
const char *kDefaultModel = "claude-haiku-5-5"; // cheapest current model: fits short commands well
constexpr int kMaxTokens = 300;

// The only two things a command may ask the program to do. Kept tiny and
// fixed on purpose: Claude only ever picks one of these two names (or
// replies in words), never arbitrary code or file paths.
QJsonArray toolDefinitions()
{
    QJsonObject noParams{{QStringLiteral("type"), QStringLiteral("object")},
                         {QStringLiteral("properties"), QJsonObject()}};
    QJsonObject sync{
        {QStringLiteral("name"), QStringLiteral("sync_cameras")},
        {QStringLiteral("description"),
         QStringLiteral("Tanlangan kamera yozuvlarini ovoz boyicha vaqt jihatidan tenglashtirib, "
                        "bittalik multicam (ko'p kameralik) ketma-ketlik yaratadi. Foydalanuvchi "
                        "\"sinxronla\", \"kameralarni tenglashtir\", \"multicam yarat\" kabi so'rasa chaqiriladi.")},
        {QStringLiteral("input_schema"), noParams}};
    QJsonObject cut{
        {QStringLiteral("name"), QStringLiteral("auto_cut_cameras")},
        {QStringLiteral("description"),
         QStringLiteral("Mavjud multicam klipni kameralar orasida avtomatik kesadi: kim gapirayotgan "
                        "bo'lsa, o'sha kamerani ko'rsatadi. Foydalanuvchi \"kes\", \"o'zi tanlab kessin\", "
                        "\"montaj qil\" kabi so'rasa, va multicam allaqachon bor bo'lsa, chaqiriladi.")},
        {QStringLiteral("input_schema"), noParams}};
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

QString ClaudeAssistant::apiKey()
{
    return QSettings().value(QLatin1String(kApiKeySetting)).toString();
}

void ClaudeAssistant::setApiKey(const QString &key)
{
    QSettings s;
    if (key.isEmpty())
        s.remove(QLatin1String(kApiKeySetting));
    else
        s.setValue(QLatin1String(kApiKeySetting), key);
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
    const QString key = apiKey();
    if (key.isEmpty()) {
        emit failed(tr("No Claude API key has been entered yet (Settings)."));
        return;
    }
    if (command.trimmed().isEmpty())
        return;

    // Fixed instructions to the model, not interface text: always Uzbek,
    // whatever language the program's own interface is set to, because the
    // commands themselves are Uzbek.
    const QString system = QStringLiteral(
        "Siz BUKHARA CINEMA video montaj dasturidagi yordamchisiz. Foydalanuvchi o'zbekcha "
        "(kirill yoki lotin) qisqa buyruq yozadi. Faqat ikki amal mavjud: sync_cameras (kameralarni "
        "ovoz boyicha sinxronlab multicam yaratish) va auto_cut_cameras (multicam klipni kameralar "
        "orasida avtomatik kesish). Buyruq shu ikkisidan biriga mos kelsa, mos vositani chaqiring, "
        "boshqa hech narsa yozmang. Mos kelmasa, vosita chaqirmasdan, qisqa va aniq javob yozing "
        "(kirill alifbosida), dastur nima qila olishini eslatib o'ting.");

    const QJsonObject body{
        {QStringLiteral("model"), QSettings().value(QLatin1String(kModelSetting), QLatin1String(kDefaultModel)).toString()},
        {QStringLiteral("max_tokens"), kMaxTokens},
        {QStringLiteral("system"), system},
        {QStringLiteral("tools"), toolDefinitions()},
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

void ClaudeAssistant::handleFinished()
{
    QNetworkReply *r = m_reply;
    m_reply = nullptr;
    r->deleteLater();
    emit busyChanged(false);
    if (r->error() == QNetworkReply::OperationCanceledError)
        return; // cancel(): nothing to report
    const QByteArray data = r->readAll();
    if (r->error() != QNetworkReply::NoError) {
        // Anthropic sends a JSON body with a human-readable error even on a
        // non-200 reply; show that when there is one, else the network error.
        QString detail = r->errorString();
        const QJsonObject err = QJsonDocument::fromJson(data).object().value(QStringLiteral("error")).toObject();
        if (!err.value(QStringLiteral("message")).toString().isEmpty())
            detail = err.value(QStringLiteral("message")).toString();
        emit failed(tr("Error contacting Claude: %1").arg(detail));
        return;
    }

    const QJsonArray content = QJsonDocument::fromJson(data).object().value(QStringLiteral("content")).toArray();
    QStringList texts;
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
    if (!texts.isEmpty())
        emit reply(texts.join(QLatin1Char('\n')));
}

} // namespace bc
