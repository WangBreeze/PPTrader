#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

namespace pp {

enum class Market {
    AShare,
    USStock,
    Crypto,
    HongKong
};

enum class SignalSide {
    Buy,
    Sell
};

struct Instrument {
    QString symbol;
    QString name;
    Market market = Market::USStock;
    QString venue;
};

struct Tick {
    Instrument instrument;
    QDateTime timestamp;
    QString batchId;
    double price = 0.0;
    double previousClose = 0.0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double volume = 0.0;
    double amount = 0.0;
    double bid = 0.0;
    double ask = 0.0;
};

struct Candle {
    Instrument instrument;
    QDateTime startTime;
    int periodSeconds = 60;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    double buyVolume = 0.0;
    double sellVolume = 0.0;
    double neutralVolume = 0.0;
    bool directionalVolumeKnown = false;
    QString source;
    QString adjustment = QStringLiteral("none");
    QDateTime downloadedAt;
};

struct TradeSignal {
    Instrument instrument;
    QDateTime timestamp;
    SignalSide side = SignalSide::Buy;
    double price = 0.0;
    double volume = 0.0;
    QString reason;
};

inline QString marketName(Market market)
{
    switch (market) {
    case Market::AShare:
        return QStringLiteral("A股");
    case Market::USStock:
        return QStringLiteral("美股");
    case Market::Crypto:
        return QStringLiteral("加密货币");
    case Market::HongKong:
        return QStringLiteral("港股");
    }

    return QStringLiteral("未知");
}

inline QString signalSideName(SignalSide side)
{
    return side == SignalSide::Buy ? QStringLiteral("买入") : QStringLiteral("卖出");
}

} // 命名空间 pp

Q_DECLARE_METATYPE(pp::Instrument)
Q_DECLARE_METATYPE(pp::Tick)
Q_DECLARE_METATYPE(pp::Candle)
Q_DECLARE_METATYPE(pp::TradeSignal)
