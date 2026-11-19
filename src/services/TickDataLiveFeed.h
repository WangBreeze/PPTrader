#pragma once

#include <QMetaType>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

namespace pp {

// ── TickData 实时接口的数据契约 ──────────────────────────────────────────────
// 与 `GET /v1/live/<symbol>` 的 JSON 字段一一对应。
// 这是 PPTrader 与 TickData 之间**唯一**的耦合面：服务端换语言、换实现、换传输，
// 只要这些字段不变，调用方就不用改。
struct LiveTickRecord {
    QString time;           // HH:MM:SS（交易所本地时间，非 UTC）
    double price = 0.0;
    int volume = 0;         // 手（与 TickData 落盘一致，不是股）
    double amount = 0.0;    // 元
    QString side;           // B 买 / S 卖 / M 中性
};

struct LiveTickBatch {
    bool succeeded = false;
    QString error;              // succeeded=false 时的原因（网络/解析/协议错误）

    QString symbol;
    QString tradingDate;        // YYYYMMDD
    QString serverTime;         // 服务端本地时间，便于排查时钟偏移
    bool available = false;     // 当日是否已存在该标的的数据文件
    bool sessionClosed = false; // 服务端按时钟判定「已过盘后时段」（15:30）
    bool overflow = false;      // 本次因 limit 被截断，还有更多数据没取
    int totalRows = 0;          // 当日文件总行数
    QString lastTime;           // 文件里最后一条的时间
    QString after;              // 本次请求携带的游标
    QString nextAfter;          // 下次应携带的游标（保证不重复、不遗漏）
    QVector<LiveTickRecord> ticks;
};

// ── 接口 ────────────────────────────────────────────────────────────────────
// 调用方只依赖这个抽象，不依赖传输方式（HTTP / 共享内存 / 文件回放皆可实现）。
//
// 增量语义（服务端与调用方共同遵守的约定）：
//   · `fetch()` 异步，结果通过 `batchReady` 返回，`requestId` 用于配对
//   · `after` 传空表示「从头取」；此后每次传上一批的 `nextAfter`
//   · `nextAfter` 可能与 `after` 相同（无新增数据），此时继续按原游标轮询即可
//   · 同一 `time` 的记录只会返回一次，不会重复投递
//   · `sessionClosed` 为真且 `overflow` 为假时，可停止轮询
class ITickDataLiveFeed {
public:
    virtual ~ITickDataLiveFeed() = default;

    virtual void setEndpoint(const QUrl &endpoint) = 0;
    virtual QUrl endpoint() const = 0;

    // 拉取 `symbol` 在 `tradingDate`（空=服务端当日）上 `time > after` 的 tick。
    virtual quint64 fetch(const QString &symbol,
                          const QString &after,
                          const QString &tradingDate = QString(),
                          int limit = 2000) = 0;
};

// HTTP 实现：轮询 `GET /v1/live/<symbol>`。
class HttpTickDataLiveFeed final : public QObject, public ITickDataLiveFeed {
    Q_OBJECT

public:
    explicit HttpTickDataLiveFeed(QObject *parent = nullptr);

    void setEndpoint(const QUrl &endpoint) override;
    QUrl endpoint() const override;

    quint64 fetch(const QString &symbol,
                  const QString &after,
                  const QString &tradingDate = QString(),
                  int limit = 2000) override;

signals:
    void batchReady(quint64 requestId, const pp::LiveTickBatch &batch);

private:
    QUrl m_endpoint;
    QNetworkAccessManager m_network;
    quint64 m_nextRequestId = 1;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::LiveTickRecord)
Q_DECLARE_METATYPE(pp::LiveTickBatch)
