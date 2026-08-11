#pragma once

#include "domain/MarketTypes.h"

#include <QObject>

namespace pp {

class NotificationService : public QObject {
    Q_OBJECT

public:
    explicit NotificationService(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~NotificationService() override = default;

    virtual void notifySignal(const TradeSignal &signal) = 0;

signals:
    void notificationFailed(const QString &message);
};

} // 命名空间 pp
