#pragma once

#include "domain/BacktestTypes.h"
#include "domain/MarketTypes.h"

#include <QDateTime>
#include <QVariantMap>
#include <QVector>

namespace pp {

struct PortfolioLeg {
    Instrument instrument;
    double targetWeight = 0.0; // 0 表示等权均分
};

struct PortfolioDefinition {
    QString portfolioId;
    QString name;
    QVector<PortfolioLeg> legs;
    int rebalanceEveryTradingDays = 1;
    double initialCapital = 1000000.0;
    double commissionRate = 0.0003;
    double slippageRate = 0.0002;
    int lotSize = 100;
    bool tPlusOne = true;
    double maximumLegWeight = 1.0;
    int maximumActiveLegs = 0;

    bool isValid(QString *error = nullptr) const;
    QVariantMap toVariantMap() const;
    QVector<Instrument> instruments() const;
};

struct PortfolioPosition {
    Instrument instrument;
    double quantity = 0.0;
    double lastPrice = 0.0;
    double marketValue = 0.0;
    double weight = 0.0;
};

struct PortfolioBar {
    QDateTime timestamp;
    double equity = 0.0;
    double cash = 0.0;
    QVector<PortfolioPosition> positions;
};

struct PortfolioTrade {
    Instrument instrument;
    QDateTime timestamp;
    QString side; // buy / sell
    double quantity = 0.0;
    double price = 0.0;
    double commission = 0.0;
    double amount = 0.0;
    QString reason;
};

struct PortfolioResult {
    PortfolioDefinition definition;
    QStringList datasetVersions;
    PerformanceSummary summary;
    QVector<PortfolioBar> equityCurve;
    QVector<PortfolioTrade> trades;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::PortfolioDefinition)
Q_DECLARE_METATYPE(pp::PortfolioResult)
