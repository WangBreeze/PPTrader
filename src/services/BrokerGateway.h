#pragma once

#include "domain/TradingTypes.h"

#include <QObject>

namespace pp {

class BrokerGateway : public QObject {
    Q_OBJECT

public:
    explicit BrokerGateway(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~BrokerGateway() override = default;

    virtual QString brokerName() const = 0;
    virtual bool isLoggedIn() const = 0;
    virtual void login() = 0;
    virtual void logout() = 0;
    virtual QString submitOrder(const OrderRequest &request) = 0;
    virtual void cancelOrder(const QString &orderId) = 0;
    virtual void updateMarketPrice(const Tick &tick) = 0;

signals:
    void loginStateChanged(bool loggedIn, const QString &message);
    void orderUpdated(const pp::Order &order);
    void executionReceived(const pp::Execution &execution);
    void orderRejected(const QString &reason);
};

} // 命名空间 pp
