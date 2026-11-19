#pragma once

#include "services/MarketDataProvider.h"
#include "services/TickDataLiveFeed.h"

#include <QDateTime>
#include <QHash>
#include <QTimer>
#include <QUrl>
#include <QVector>

namespace pp {

// 把 TickData 的实时接口接成 PPTrader 的行情源。
//
// 模型：定时器每隔 pollIntervalMs 对每个关注的标的调用一次 `/v1/live/<symbol>`，
// 携带上一次拿到的 `nextAfter` 作为游标 —— 服务端只返回新增 tick，
// 因此同一笔成交不会被重复投递（重复投递会污染策略与回测）。
//
// 职责边界：TickData 负责采集、补采、校验；本类**只读**它的结果，
// 不做重试补偿、不改数据。缺数据由 TickData 侧负责发现与修复。
//
// 不做交易时段门控：TickData 的数据就是「它采到的」，盘后/收盘后读取也应当
// 原样交付（例如晚间启动 PPTrader 复盘当日）。需要门控的调用方自行用
// `MarketSession` 过滤。
class TickDataLiveProvider final : public MarketDataProvider {
    Q_OBJECT

public:
    explicit TickDataLiveProvider(QObject *parent = nullptr);
    ~TickDataLiveProvider() override;

    void start(const QVector<Instrument> &instruments) override;
    void stop() override;

    // 数据节点地址，默认 http://127.0.0.1:18765
    void setEndpoint(const QUrl &endpoint);
    QUrl endpoint() const;

    // 交易日覆盖：空 = 用服务端「当日」。
    // 用途：① 非交易日/盘前验证；② 回看某个已采集的交易日。
    void setTradingDate(const QString &tradingDate);
    QString tradingDate() const;

    // 轮询间隔（毫秒），默认 3000 —— 与腾讯分笔的 3 秒聚合粒度对齐
    void setPollIntervalMs(int intervalMs);
    int pollIntervalMs() const;

    // 已投递的 tick 总数（观测用）
    qint64 deliveredTicks() const;

signals:
    // 一轮轮询结束：symbols=本轮请求数，newTicks=本轮新投递数
    void pollCompleted(int symbols, int newTicks);

private:
    void poll();
    void handleBatch(quint64 requestId, const pp::LiveTickBatch &batch);
    void publish(const Instrument &instrument, const LiveTickRecord &record,
                 const QString &tradingDate);

    QVector<Instrument> m_instruments;
    QString m_tradingDate;                   // 空 = 服务端当日
    QHash<QString, QString> m_cursor;        // symbol -> nextAfter
    QHash<quint64, QString> m_pending;       // requestId -> symbol
    int m_roundRequested = 0;                // 本轮发出的请求数
    int m_roundRemaining = 0;                // 本轮尚未响应的请求数
    int m_roundDelivered = 0;                // 本轮已投递的 tick 数
    HttpTickDataLiveFeed *m_feed = nullptr;
    QTimer m_timer;
    quint64 m_totalDelivered = 0;
};

} // namespace pp
