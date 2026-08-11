#pragma once

#include "domain/TradingTypes.h"

namespace pp {

struct ExchangeRuleContext {
    QDateTime evaluationTime;
    double previousClose = 0.0;
    bool suspended = false;
    bool enforceTradingSession = false;
};

class ExchangeRuleService {
public:
    virtual ~ExchangeRuleService() = default;

    virtual RiskCheckResult validate(const OrderRequest &request,
                                     const ExchangeRuleContext &context) const = 0;
    virtual int defaultLotSize(const Instrument &instrument, OrderSide side) const = 0;
    virtual double tickSize(const Instrument &instrument) const = 0;
};

} // namespace pp
