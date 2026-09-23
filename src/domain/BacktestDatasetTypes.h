#pragma once

#include "domain/MarketTypes.h"

#include <QDate>
#include <QVariantMap>
#include <QVector>

namespace pp {

// 单个交易日的事实指纹与派生缓存状态。新身份使用 rawContentSha256；旧归档缺少该字段时
// 仍按 rawCount/rawMaximumId 解释，以保持历史结果可恢复。
struct RawTickDayFingerprint {
    QDate tradingDate;
    qint64 rawCount = 0;
    qint64 rawMaximumId = 0;
    QString rawContentSha256;
    int barCount = 0;
    int cacheVersion = 0;
    bool cacheFresh = false;
};

// 一个可回测数据集的不可变身份：原始日期集合 + 每交易日 Tick 指纹 + 聚合算法版本 + 周期。
// 归档结果携带完整身份（toJson），不依赖当前数据库解释历史结果。
struct BacktestDatasetIdentity {
    Instrument instrument;
    int periodSeconds = 0;
    QString source = QStringLiteral("raw_tick_cache_v1");
    int aggregatorVersion = 1;
    int fingerprintVersion = 2;
    QVector<RawTickDayFingerprint> days;
    QString datasetVersion;
    QDate firstTradingDate;
    QDate lastTradingDate;
    int expectedTradingDays = 0;
    int gapDayCount = 0;
    int staleDayCount = 0;
    bool calendarCoverageKnown = false;
    QString calendarSource;
    QString calendarNote;

    int rawTickTotal() const;
    int derivedBarTotal() const;
    bool isEmpty() const;
    bool hasStaleCache() const;
    QVariantMap toVariantMap() const;
    QString toJson() const;
    bool fromJson(const QString &json);
};

} // namespace pp

Q_DECLARE_METATYPE(pp::BacktestDatasetIdentity)
