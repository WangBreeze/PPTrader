#pragma once

#include "domain/BacktestTypes.h"

#include <QHash>
#include <QStringList>

#include <algorithm>
#include <memory>
#include <utility>

namespace pp {

// 批量回测只让最近的少量完整结果停留在内存，列表摘要由模型独立保留。
// 字节数是基于连续容器元素的保守估计，用于防止大 Tick 结果绕过纯数量上限。
class BatchBacktestResultRetention final {
public:
    static constexpr int DefaultMaximumItems = 32;
    static constexpr qint64 DefaultMaximumBytes = 256LL * 1024LL * 1024LL;

    explicit BatchBacktestResultRetention(
        int maximumItems = DefaultMaximumItems,
        qint64 maximumBytes = DefaultMaximumBytes)
        : m_maximumItems(std::max(1, maximumItems)),
          m_maximumBytes(std::max<qint64>(1, maximumBytes))
    {
    }

    QStringList insert(const QString &symbol, BacktestResult result)
    {
        const QString normalized = symbol.trimmed();
        if (normalized.isEmpty()) return {};

        remove(normalized);
        const qint64 resultBytes = estimateBytes(result);
        m_results.insert(
            normalized, std::make_shared<BacktestResult>(std::move(result)));
        m_resultBytes.insert(normalized, resultBytes);
        m_order.append(normalized);
        m_estimatedBytes += resultBytes;

        QStringList evicted;
        while (m_order.size() > 1
               && (m_order.size() > m_maximumItems
                   || m_estimatedBytes > m_maximumBytes)) {
            const QString oldest = m_order.first();
            remove(oldest);
            evicted.append(oldest);
        }
        return evicted;
    }

    bool contains(const QString &symbol) const
    {
        return m_results.contains(symbol.trimmed());
    }

    const BacktestResult *find(const QString &symbol) const
    {
        const auto iterator = m_results.constFind(symbol.trimmed());
        return iterator == m_results.constEnd() ? nullptr : iterator.value().get();
    }

    std::shared_ptr<const BacktestResult> sharedResult(const QString &symbol) const
    {
        const auto iterator = m_results.constFind(symbol.trimmed());
        return iterator == m_results.constEnd()
            ? std::shared_ptr<const BacktestResult>{} : iterator.value();
    }

    int size() const { return m_results.size(); }
    qint64 estimatedBytes() const { return m_estimatedBytes; }

    void clear()
    {
        m_results.clear();
        m_resultBytes.clear();
        m_order.clear();
        m_estimatedBytes = 0;
    }

    static qint64 estimateBytes(const BacktestResult &result)
    {
        qint64 bytes = sizeof(BacktestResult);
        bytes += static_cast<qint64>(result.rawTicks.size())
            * static_cast<qint64>(sizeof(RawTradeTick));
        bytes += static_cast<qint64>(result.equityCurve.size())
            * static_cast<qint64>(sizeof(EquityPoint));
        bytes += static_cast<qint64>(result.trades.size())
            * static_cast<qint64>(sizeof(BacktestTrade));
        bytes += static_cast<qint64>(result.candles.size())
            * static_cast<qint64>(sizeof(Candle));
        bytes += static_cast<qint64>(result.chartSignals.size())
            * static_cast<qint64>(sizeof(TradeSignal));
        return std::max<qint64>(1, bytes);
    }

private:
    void remove(const QString &symbol)
    {
        const auto byteIterator = m_resultBytes.find(symbol);
        if (byteIterator != m_resultBytes.end()) {
            m_estimatedBytes = std::max<qint64>(
                0, m_estimatedBytes - byteIterator.value());
            m_resultBytes.erase(byteIterator);
        }
        m_results.remove(symbol);
        m_order.removeAll(symbol);
    }

    int m_maximumItems = DefaultMaximumItems;
    qint64 m_maximumBytes = DefaultMaximumBytes;
    QHash<QString, std::shared_ptr<BacktestResult>> m_results;
    QHash<QString, qint64> m_resultBytes;
    QStringList m_order;
    qint64 m_estimatedBytes = 0;
};

} // namespace pp
