#pragma once

#include "domain/PortfolioTypes.h"

#include <QDate>
#include <QDateTime>
#include <QVector>

namespace pp {

// 组合执行器：抽取自 PortfolioBacktestEngine，封装组合调仓（先卖后买）与逐日估值，
// 供单标的/多股组合回测引擎复用。语义与原引擎完全一致：整手、费用、滑点、T+1、
// 缺报价前向填充 stale。
class PortfolioExecutor {
public:
    struct LegState {
        Instrument instrument;
        double quantity = 0.0;
        double lastPrice = 0.0;
        QDate lastPriceDay;
        double weight = 0.0;
        bool boughtToday = false;
        QDate lastBuyDay;
        bool quoteAvailable = false; // 当前执行帧存在真实报价；旧价仅允许估值
        QString marketStatus = QStringLiteral("missing_unknown");
    };

    struct ExecutionState {
        QVector<LegState> legs;
        double cash = 0.0;
        double totalCommission = 0.0;
    };

    // 权益 = 现金 + Σ 持仓数量 × 最近价格。
    static double equity(const ExecutionState &state);

    // 构建各腿持仓快照：weight 以传入权益为分母；priceStale 在最近报价日期早于估值日时置位。
    static QVector<PortfolioPosition> positions(const ExecutionState &state,
                                                const QDate &day,
                                                double equity);

    // 调仓：按目标权重先卖后买，就地更新 state，成交追加到 tradesOut。
    static void rebalance(ExecutionState &state,
                          double equity,
                          double slippageRate,
                          double commissionRate,
                          int lotSize,
                          bool tPlusOne,
                          const QDateTime &barTime,
                          QVector<PortfolioTrade> &tradesOut);
};

} // namespace pp
