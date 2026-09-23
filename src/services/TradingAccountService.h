#pragma once

#include "services/BrokerGateway.h"
#include "services/DefaultExchangeRuleService.h"
#include "services/OrderSubmissionPort.h"
#include "services/PreTradeRiskService.h"

#include <QHash>

namespace pp {

class TradingAccountService final : public QObject, public OrderSubmissionPort {
    Q_OBJECT

public:
    explicit TradingAccountService(BrokerGateway *gateway,
                                   double initialCapital = 1000000.0,
                                   double commissionRate = 0.0003,
                                   double slippageRate = 0.0002,
                                   QObject *parent = nullptr);

    QString submitOrder(OrderRequest request) override;
    void cancelOrder(const QString &orderId);
    void updateMarketPrice(const Tick &tick);
    bool initializeHistoricalClock(const QDateTime &sourceStart);
    void reset(double initialCapital = 1000000.0);
    void restoreTradingState(const TradingState &state);
    RiskLimits riskLimits() const;
    void setRiskLimits(const RiskLimits &limits);
    void setExchangeRuleService(ExchangeRuleService *service);

    AccountSnapshot snapshot() const override;
    QVector<Order> orders() const;
    QVector<Execution> executions() const;
    QVector<Position> positions() const override;

signals:
    void orderChanged(const pp::Order &order);
    void executionAdded(const pp::Execution &execution);
    void positionChanged(const pp::Position &position);
    void accountChanged(const pp::AccountSnapshot &snapshot);
    void accountReset();
    void operationFailed(const QString &message);

private:
    struct Reservation {
        OrderSide side = OrderSide::Buy;
        QString symbol;
        double remainingQuantity = 0.0;
        double reservedCash = 0.0;
        double reservedQuantity = 0.0;
    };

    bool validateOrder(const OrderRequest &request, QString *error) const;
    void handleOrderUpdated(const Order &order);
    void handleExecution(const Execution &execution);
    void reserveForOrder(const Order &order);
    void releaseReservation(const QString &orderId);
    void recalculateAccount();
    void ensureTradingDay(const QDate &tradingDay);

    BrokerGateway *m_gateway = nullptr;
    QHash<QString, Order> m_orders;
    QVector<QString> m_orderSequence;
    QVector<Execution> m_executions;
    QHash<QString, Position> m_positions;
    QHash<QString, double> m_symbolRealizedPnl;
    QHash<QString, Reservation> m_reservations;
    AccountSnapshot m_snapshot;
    QDateTime m_historicalClockStart;
    double m_commissionRate = 0.0003;
    double m_slippageRate = 0.0002;
    PreTradeRiskService m_riskService;
    DefaultExchangeRuleService m_defaultExchangeRuleService;
    ExchangeRuleService *m_exchangeRuleService = &m_defaultExchangeRuleService;
};

} // namespace pp
