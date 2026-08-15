#include "services/WebhookNotificationService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace pp {

namespace {
QString eventJsonName(NotificationEvent event)
{
    switch (event) {
    case NotificationEvent::SignalGenerated: return QStringLiteral("SignalGenerated");
    case NotificationEvent::ApprovalRequired: return QStringLiteral("ApprovalRequired");
    case NotificationEvent::OrderSubmitted: return QStringLiteral("OrderSubmitted");
    case NotificationEvent::ExecutionRejected: return QStringLiteral("ExecutionRejected");
    case NotificationEvent::IntentExpired: return QStringLiteral("IntentExpired");
    }
    return QStringLiteral("Unknown");
}

QString severityJsonName(NotificationSeverity severity)
{
    switch (severity) {
    case NotificationSeverity::Info: return QStringLiteral("Info");
    case NotificationSeverity::Warning: return QStringLiteral("Warning");
    case NotificationSeverity::Critical: return QStringLiteral("Critical");
    }
    return QStringLiteral("Info");
}
} // namespace

QtNetworkWebhookTransport::QtNetworkWebhookTransport(QObject *parent)
    : m_network(new QNetworkAccessManager(parent))
{
}

void QtNetworkWebhookTransport::post(const QString &url,
                                     const QByteArray &payload,
                                     std::function<void(bool, const QString &)> done)
{
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_network->post(request, payload);
    QObject::connect(reply, &QNetworkReply::finished, m_network, [reply, done]() {
        reply->deleteLater();
        if (!done) return;
        if (reply->error() == QNetworkReply::NoError) {
            done(true, {});
        } else {
            done(false, reply->errorString());
        }
    });
}

WebhookNotificationService::WebhookNotificationService(QObject *parent)
    : NotificationService(parent)
    , m_defaultTransport(new QtNetworkWebhookTransport(this))
    , m_transport(m_defaultTransport)
{
}

WebhookNotificationService::~WebhookNotificationService() = default;

void WebhookNotificationService::setWebhookUrl(const QString &url)
{
    m_webhookUrl = url.trimmed();
}

QString WebhookNotificationService::webhookUrl() const
{
    return m_webhookUrl;
}

void WebhookNotificationService::setTransport(WebhookTransport *transport)
{
    m_transport = transport ? transport : m_defaultTransport;
}

bool WebhookNotificationService::send(const NotificationMessage &message)
{
    if (m_webhookUrl.isEmpty()) {
        emit notificationFailed(channelName(), QStringLiteral("Webhook URL 未配置"));
        return false;
    }
    if (!m_transport) {
        emit notificationFailed(channelName(), QStringLiteral("Webhook 投递通道不可用"));
        return false;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("event"), eventJsonName(message.event));
    payload.insert(QStringLiteral("eventName"), notificationEventName(message.event));
    payload.insert(QStringLiteral("title"), message.title);
    payload.insert(QStringLiteral("body"), message.body);
    payload.insert(QStringLiteral("severity"), severityJsonName(message.severity));
    payload.insert(QStringLiteral("timestamp"),
                   (message.timestamp.isValid() ? message.timestamp : QDateTime::currentDateTime())
                       .toString(Qt::ISODate));

    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    m_transport->post(m_webhookUrl, json, [this, message](bool ok, const QString &error) {
        if (ok) {
            emit notificationSent(channelName(), message.title);
        } else {
            emit notificationFailed(channelName(),
                                    QStringLiteral("Webhook 投递失败：%1").arg(error));
        }
    });
    return true;
}

} // namespace pp
