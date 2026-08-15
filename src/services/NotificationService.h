#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

namespace pp {

// 通知事件的语义类型；dispatcher 据此路由到不同通道，界面据此展示。
enum class NotificationEvent {
    SignalGenerated,    // 实时策略信号生成
    ApprovalRequired,   // 执行意图等待人工审批
    OrderSubmitted,     // 策略订单已提交（模拟账户）
    ExecutionRejected,  // 执行被风控/人工拒绝
    IntentExpired       // 审批意图过期
};

enum class NotificationSeverity {
    Info,
    Warning,
    Critical
};

inline QString notificationEventName(NotificationEvent event)
{
    switch (event) {
    case NotificationEvent::SignalGenerated: return QStringLiteral("信号生成");
    case NotificationEvent::ApprovalRequired: return QStringLiteral("待审批");
    case NotificationEvent::OrderSubmitted: return QStringLiteral("订单提交");
    case NotificationEvent::ExecutionRejected: return QStringLiteral("执行拒绝");
    case NotificationEvent::IntentExpired: return QStringLiteral("审批过期");
    }
    return QStringLiteral("未知");
}

// 与通道无关的通知载荷；具体通道（桌面/Webhook）负责渲染与投递。
struct NotificationMessage {
    NotificationEvent event = NotificationEvent::SignalGenerated;
    NotificationSeverity severity = NotificationSeverity::Info;
    QString title;
    QString body;
    QDateTime timestamp;
};

// 通知通道抽象：每个实现代表一条投递通道（桌面气泡 / Webhook 等）。
class NotificationService : public QObject {
    Q_OBJECT

public:
    explicit NotificationService(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~NotificationService() override = default;

    virtual QString channelName() const = 0;
    virtual bool send(const NotificationMessage &message) = 0;

signals:
    void notificationSent(const QString &channel, const QString &messageTitle);
    void notificationFailed(const QString &channel, const QString &error);
};

} // namespace pp
