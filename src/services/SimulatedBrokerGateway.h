#pragma once

#include "services/BrokerGateway.h"

#include <QHash>

namespace pp {

class SimulatedBrokerGateway final : public BrokerGateway {
    Q_OBJECT

public:
    explicit SimulatedBrokerGateway(double commissionRate = 0.0003,
                                    double slippageRate = 0.0002,
                                    double liquidityParticipationRate = 1.0,
                                    QObject *parent = nullptr);

    QString brokerName() const override;
    bool isLoggedIn() const override;
    void login() override;
    void logout() override;
    QString submitOrder(const OrderRequest &request) override;
    void cancelOrder(const QString &orderId) override;
    void updateMarketPrice(const Tick &tick) override;
    void expireOrders(const QDateTime &currentTime);

    double commissionRate() const;
    double slippageRate() const;

private:
    void tryFill(const QString &orderId, double *availableLiquidity = nullptr);
    void rejectOrder(Order &order, const QString &reason);
    void cancelRemaining(Order &order, const QString &reason);
    QDateTime defaultExpiry(const OrderRequest &request) const;

    QHash<QString, Order> m_orders;
    QHash<QString, Tick> m_latestTicks;
    bool m_loggedIn = false;
    double m_commissionRate = 0.0003;
    double m_slippageRate = 0.0002;
    double m_liquidityParticipationRate = 1.0;
};

} // namespace pp
