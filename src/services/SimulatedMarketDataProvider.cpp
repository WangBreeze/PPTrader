#include "services/SimulatedMarketDataProvider.h"

#include "services/MarketSession.h"

#include <QRandomGenerator>

#include <algorithm>

namespace pp {

namespace {

double randomBetween(double low, double high)
{
    return low + (high - low) * QRandomGenerator::global()->generateDouble();
}

} // 匿名命名空间

SimulatedMarketDataProvider::SimulatedMarketDataProvider(QObject *parent)
    : MarketDataProvider(parent)
{
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &SimulatedMarketDataProvider::emitNextTicks);
}

void SimulatedMarketDataProvider::start(const QVector<Instrument> &instruments)
{
    m_instruments = instruments;
    if (m_instruments.isEmpty()) {
        emit providerError(QStringLiteral("未配置监测标的"));
        return;
    }

    m_timer.start();
}

void SimulatedMarketDataProvider::stop()
{
    m_timer.stop();
}

void SimulatedMarketDataProvider::emitNextTicks()
{
    const QDateTime utcNow = QDateTime::currentDateTimeUtc();
    for (const Instrument &instrument : std::as_const(m_instruments)) {
        if (!MarketSession::isOpen(instrument, utcNow)) {
            continue;
        }

        const double price = nextPrice(instrument);
        const double spread = std::max(0.01, price * 0.0002);

        Tick tick;
        tick.instrument = instrument;
        tick.timestamp = utcNow;
        tick.price = price;
        tick.volume = randomBetween(100.0, 12000.0);
        tick.bid = price - spread;
        tick.ask = price + spread;

        emit tickReceived(tick);
    }
}

double SimulatedMarketDataProvider::nextPrice(const Instrument &instrument)
{
    double price = m_prices.value(instrument.symbol, 0.0);
    if (price <= 0.0) {
        switch (instrument.market) {
        case Market::AShare:
            price = randomBetween(20.0, 180.0);
            break;
        case Market::USStock:
            price = randomBetween(80.0, 420.0);
            break;
        case Market::Crypto:
            price = randomBetween(20000.0, 90000.0);
            break;
        case Market::HongKong:
            price = randomBetween(15.0, 420.0);
            break;
        }
    }

    const double volatility = instrument.market == Market::Crypto ? 0.008 : 0.0025;
    const double drift = randomBetween(-volatility, volatility);
    price = std::max(0.01, price * (1.0 + drift));
    m_prices.insert(instrument.symbol, price);
    return price;
}

} // 命名空间 pp
