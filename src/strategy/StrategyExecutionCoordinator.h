#pragma once

#include "services/OrderSubmissionPort.h"
#include "services/StrategyExecutionStore.h"
#include "strategy/PositionSizer.h"
#include "strategy/SignalExecutionPolicy.h"

#include <QHash>
#include <QObject>

#include <optional>

namespace pp {

class StrategyExecutionCoordinator final : public QObject {
    Q_OBJECT

public:
    StrategyExecutionCoordinator(OrderSubmissionPort *orderPort,
                                 PositionSizer *positionSizer,
                                 SignalExecutionPolicy *executionPolicy,
                                 StrategyExecutionStore *store = nullptr,
                                 QObject *parent = nullptr);

    bool setConfiguration(const StrategyExecutionConfig &config);
    void removeConfiguration(const QString &strategyId);
    std::optional<StrategyExecutionConfig> configuration(const QString &strategyId) const;
    QVector<StrategyExecutionConfig> configurations() const;

    QString processSignal(const QString &strategyId, const TradeSignal &signal);
    bool approveIntent(const QString &intentId);
    bool rejectIntent(const QString &intentId, const QString &reason);
    std::optional<StrategyOrderIntent> intent(const QString &intentId) const;
    QVector<StrategyOrderIntent> intents() const;
    void restore();

signals:
    void intentUpdated(const pp::StrategyOrderIntent &intent);
    void approvalRequired(const pp::StrategyOrderIntent &intent);
    void orderSubmitted(const pp::StrategyOrderIntent &intent);
    void executionRejected(const pp::StrategyOrderIntent &intent);

private:
    struct DailyStrategyUsage {
        QDate tradingDay;
        int submittedOrders = 0;
        double submittedValue = 0.0;
    };

    std::optional<Position> findPosition(const QString &symbol) const;
    bool evaluateAndBuildOrder(StrategyOrderIntent &intent, QString *error);
    bool submitIntent(StrategyOrderIntent &intent);
    void updateIntent(StrategyOrderIntent &intent, OrderIntentStatus status,
                      const QString &message = {});

    OrderSubmissionPort *m_orderPort = nullptr;
    PositionSizer *m_positionSizer = nullptr;
    SignalExecutionPolicy *m_executionPolicy = nullptr;
    StrategyExecutionStore *m_store = nullptr;
    QHash<QString, StrategyExecutionConfig> m_configurations;
    QHash<QString, StrategyOrderIntent> m_intents;
    QVector<QString> m_intentSequence;
    QHash<QString, QDateTime> m_lastSubmissionTimes;
    QHash<QString, DailyStrategyUsage> m_dailyUsage;
};

} // namespace pp
