#pragma once

#include "services/MarketDataProvider.h"

#include <QHash>
#include <QTimer>

namespace pp {

class SimulatedMarketDataProvider final : public MarketDataProvider {
    Q_OBJECT

public:
    explicit SimulatedMarketDataProvider(QObject *parent = nullptr);

    void start(const QVector<Instrument> &instruments) override;
    void stop() override;

private:
    void emitNextTicks();
    double nextPrice(const Instrument &instrument);

    QVector<Instrument> m_instruments;
    QHash<QString, double> m_prices;
    QTimer m_timer;
};

} // 命名空间 pp
