#pragma once

#include "domain/MarketTypes.h"
#include "domain/StrategyExecutionTypes.h"
#include "services/NotificationService.h"

#include <QHash>
#include <QObject>
#include <QStringList>

namespace pp {

// 通知配置：通道开关 + Webhook URL + 逐事件过滤。应用级设置，QSettings 持久化。
struct NotificationSettings {
    bool desktopEnabled = false;
    bool webhookEnabled = false;
    QString webhookUrl;
    bool notifySignal = true;
    bool notifyApproval = true;
    bool notifyOrderSubmitted = true;
    bool notifyExecutionRejected = true;
    bool notifyIntentExpired = true;
};

// 把高层执行事件翻译成 NotificationMessage，并按配置路由到已注册通道。
class NotificationDispatcher final : public QObject {
    Q_OBJECT

public:
    explicit NotificationDispatcher(QObject *parent = nullptr);

    void registerChannel(NotificationService *service);
    void unregisterChannel(const QString &channelName);
    QVector<NotificationService *> channels() const;

    void setSettings(const NotificationSettings &settings);
    NotificationSettings settings() const;

    // 事件入口（语义化，内部转 NotificationMessage）。
    void notifySignal(const TradeSignal &signal);
    void notifyApprovalRequired(const StrategyOrderIntent &intent);
    void notifyOrderSubmitted(const StrategyOrderIntent &intent);
    void notifyExecutionRejected(const StrategyOrderIntent &intent);
    void notifyIntentExpired(const StrategyOrderIntent &intent);

signals:
    void messageDispatched(const QString &title, const QString &body);

private:
    void dispatch(const NotificationMessage &message);
    bool channelEnabled(const NotificationService *service) const;
    bool eventEnabled(NotificationEvent event) const;

    QHash<QString, NotificationService *> m_channels;
    QStringList m_channelOrder;
    NotificationSettings m_settings;
};

} // namespace pp
