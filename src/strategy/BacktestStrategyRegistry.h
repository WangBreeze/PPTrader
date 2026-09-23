#pragma once

#include "strategy/BacktestStrategy.h"

#include <memory>
#include <optional>

namespace pp {

class BacktestStrategyRegistry {
public:
    QVector<BacktestStrategyDescriptor> descriptors() const;
    std::optional<BacktestStrategyDescriptor> descriptor(const QString &strategyId) const;
    QVariantList catalog() const;
    bool supportsDataMode(const QString &strategyId, const QString &dataMode) const;
    bool normalizeParameters(const QString &strategyId, const QVariantMap &parameters,
                             QVariantMap *normalized, QString *error) const;
    std::unique_ptr<BacktestStrategy> create(const QString &strategyId,
                                             const QVariantMap &parameters,
                                             QString *error = nullptr) const;
};

} // namespace pp
