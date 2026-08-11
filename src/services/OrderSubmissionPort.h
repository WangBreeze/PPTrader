#pragma once

#include "domain/TradingTypes.h"

namespace pp {

class OrderSubmissionPort {
public:
    virtual ~OrderSubmissionPort() = default;

    virtual QString submitOrder(OrderRequest request) = 0;
    virtual AccountSnapshot snapshot() const = 0;
    virtual QVector<Position> positions() const = 0;
};

} // namespace pp
