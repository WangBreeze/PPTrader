#pragma once

#include "services/MarketDataProvider.h"

#include <QThread>

namespace pp {

class RealtimeProviderRouter;

class CompositeMarketDataProvider final : public MarketDataProvider {
    Q_OBJECT

public:
    explicit CompositeMarketDataProvider(QObject *parent = nullptr);
    ~CompositeMarketDataProvider() override;

    void start(const QVector<Instrument> &instruments) override;
    void stop() override;

private:
    RealtimeProviderRouter *m_router = nullptr;
    QThread m_workerThread;
};

} // namespace pp
