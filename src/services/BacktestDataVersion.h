#pragma once

#include "domain/BacktestDatasetTypes.h"
#include "domain/MarketTypes.h"
#include "domain/RawTradeTypes.h"

#include <QString>
#include <QVector>
#include <QCryptographicHash>

namespace pp {

class RawTickBacktestDataVersionBuilder final {
public:
    explicit RawTickBacktestDataVersionBuilder(const QString &symbol);

    bool append(const RawTradeTick &tick);
    QString finish();
    int count() const;

private:
    QString m_symbol;
    QCryptographicHash m_hash;
    QDateTime m_previousTimestamp;
    qint64 m_previousSourceLine = -1;
    int m_count = 0;
    bool m_valid = true;
    bool m_finished = false;
};

QString computeBacktestDataVersion(const QVector<Candle> &candles,
                                   const QString &symbol,
                                   int periodSeconds);
QString computeBacktestDataVersion(const QVector<RawTradeTick> &ticks,
                                   const QString &symbol);

// v2 数据集身份哈希纳入逐日规范化 Tick 内容 SHA-256。旧归档没有内容摘要时继续使用
// v1 的 date/count/maximumId 算法，派生缓存是否新鲜不参与哈希。
QString computeBacktestDatasetVersion(const BacktestDatasetIdentity &identity);

} // namespace pp
