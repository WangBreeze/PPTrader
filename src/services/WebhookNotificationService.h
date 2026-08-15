#pragma once

#include "services/NotificationService.h"

#include <QByteArray>
#include <functional>

class QNetworkAccessManager;

namespace pp {

// Webhook HTTP 投递抽象；默认实现基于 QNetworkAccessManager，测试可注入 fake。
class WebhookTransport {
public:
    virtual ~WebhookTransport() = default;
    virtual void post(const QString &url, const QByteArray &payload,
                      std::function<void(bool, const QString &)> done) = 0;
};

class QtNetworkWebhookTransport final : public WebhookTransport {
public:
    explicit QtNetworkWebhookTransport(QObject *parent = nullptr);
    void post(const QString &url, const QByteArray &payload,
              std::function<void(bool, const QString &)> done) override;

private:
    QNetworkAccessManager *m_network = nullptr;
};

// Webhook 通知通道：向配置的 URL POST 一条 JSON（event/title/body/severity/timestamp）。
class WebhookNotificationService final : public NotificationService {
    Q_OBJECT

public:
    explicit WebhookNotificationService(QObject *parent = nullptr);
    ~WebhookNotificationService() override;

    QString channelName() const override { return QStringLiteral("webhook"); }

    void setWebhookUrl(const QString &url);
    QString webhookUrl() const;

    // 测试注入；服务不取得所有权，调用方保证生命周期覆盖服务生命周期。
    void setTransport(WebhookTransport *transport);

    bool send(const NotificationMessage &message) override;

private:
    QString m_webhookUrl;
    WebhookTransport *m_transport = nullptr;
    WebhookTransport *m_defaultTransport = nullptr;
};

} // namespace pp
