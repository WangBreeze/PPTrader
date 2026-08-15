#include "services/NotificationDispatcher.h"

namespace pp {

NotificationDispatcher::NotificationDispatcher(QObject *parent)
    : QObject(parent)
{
}

void NotificationDispatcher::registerChannel(NotificationService *service)
{
    if (!service) return;
    if (m_channels.contains(service->channelName())) {
        unregisterChannel(service->channelName());
    }
    m_channels.insert(service->channelName(), service);
    m_channelOrder.append(service->channelName());
}

void NotificationDispatcher::unregisterChannel(const QString &channelName)
{
    if (!m_channels.contains(channelName)) return;
    m_channels.remove(channelName);
    m_channelOrder.removeAll(channelName);
}

QVector<NotificationService *> NotificationDispatcher::channels() const
{
    QVector<NotificationService *> result;
    result.reserve(m_channelOrder.size());
    for (const QString &name : m_channelOrder) {
        if (NotificationService *service = m_channels.value(name)) result.append(service);
    }
    return result;
}

void NotificationDispatcher::setSettings(const NotificationSettings &settings)
{
    m_settings = settings;
}

NotificationSettings NotificationDispatcher::settings() const
{
    return m_settings;
}

bool NotificationDispatcher::channelEnabled(const NotificationService *service) const
{
    if (!service) return false;
    const QString name = service->channelName();
    if (name == QStringLiteral("desktop")) return m_settings.desktopEnabled;
    if (name == QStringLiteral("webhook")) return m_settings.webhookEnabled;
    return true; // 未知通道默认放行，便于扩展自定义通道。
}

bool NotificationDispatcher::eventEnabled(NotificationEvent event) const
{
    switch (event) {
    case NotificationEvent::SignalGenerated: return m_settings.notifySignal;
    case NotificationEvent::ApprovalRequired: return m_settings.notifyApproval;
    case NotificationEvent::OrderSubmitted: return m_settings.notifyOrderSubmitted;
    case NotificationEvent::ExecutionRejected: return m_settings.notifyExecutionRejected;
    case NotificationEvent::IntentExpired: return m_settings.notifyIntentExpired;
    }
    return true;
}

void NotificationDispatcher::dispatch(const NotificationMessage &message)
{
    if (!eventEnabled(message.event)) return;
    for (NotificationService *service : channels()) {
        if (!channelEnabled(service)) continue;
        service->send(message);
    }
    emit messageDispatched(message.title, message.body);
}

void NotificationDispatcher::notifySignal(const TradeSignal &signal)
{
    NotificationMessage message;
    message.event = NotificationEvent::SignalGenerated;
    message.severity = NotificationSeverity::Info;
    message.title = QStringLiteral("策略信号：%1 %2")
        .arg(signal.instrument.symbol, signalSideName(signal.side));
    message.body = QStringLiteral("%1 %2 股 @ %3 — %4")
        .arg(signalSideName(signal.side))
        .arg(signal.volume, 0, 'f', 0)
        .arg(signal.price, 0, 'f', 2)
        .arg(signal.reason.isEmpty() ? QStringLiteral("无说明") : signal.reason);
    message.timestamp = signal.timestamp.isValid()
        ? signal.timestamp : QDateTime::currentDateTime();
    dispatch(message);
}

namespace {
QString intentSummary(const StrategyOrderIntent &intent)
{
    return QStringLiteral("%1 %2 %3 股 @ %4")
        .arg(intent.signal.instrument.symbol,
             signalSideName(intent.signal.side))
        .arg(intent.orderRequest.quantity, 0, 'f', 0)
        .arg(intent.signal.price, 0, 'f', 2);
}
} // namespace

void NotificationDispatcher::notifyApprovalRequired(const StrategyOrderIntent &intent)
{
    NotificationMessage message;
    message.event = NotificationEvent::ApprovalRequired;
    message.severity = NotificationSeverity::Warning;
    message.title = QStringLiteral("策略执行待审批：%1").arg(intent.strategyId);
    message.body = intentSummary(intent);
    if (intent.expiresAt.isValid()) {
        message.body += QStringLiteral("，%1 前审批有效")
            .arg(intent.expiresAt.toLocalTime().toString(QStringLiteral("HH:mm:ss")));
    }
    message.timestamp = intent.createdAt.isValid()
        ? intent.createdAt : QDateTime::currentDateTime();
    dispatch(message);
}

void NotificationDispatcher::notifyOrderSubmitted(const StrategyOrderIntent &intent)
{
    NotificationMessage message;
    message.event = NotificationEvent::OrderSubmitted;
    message.severity = NotificationSeverity::Info;
    message.title = QStringLiteral("策略订单已提交：%1").arg(intent.orderId);
    message.body = intentSummary(intent);
    message.timestamp = QDateTime::currentDateTime();
    dispatch(message);
}

void NotificationDispatcher::notifyExecutionRejected(const StrategyOrderIntent &intent)
{
    NotificationMessage message;
    message.event = NotificationEvent::ExecutionRejected;
    message.severity = NotificationSeverity::Critical;
    message.title = QStringLiteral("策略执行被拒绝：%1").arg(intent.strategyId);
    message.body = intent.message.isEmpty() ? intentSummary(intent) : intent.message;
    message.timestamp = QDateTime::currentDateTime();
    dispatch(message);
}

void NotificationDispatcher::notifyIntentExpired(const StrategyOrderIntent &intent)
{
    NotificationMessage message;
    message.event = NotificationEvent::IntentExpired;
    message.severity = NotificationSeverity::Warning;
    message.title = QStringLiteral("审批意图已过期：%1").arg(intent.strategyId);
    message.body = intentSummary(intent);
    message.timestamp = QDateTime::currentDateTime();
    dispatch(message);
}

} // namespace pp
