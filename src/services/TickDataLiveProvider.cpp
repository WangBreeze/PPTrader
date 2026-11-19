#include "services/TickDataLiveProvider.h"

#include "domain/RawTradeDirection.h"

#include <QTime>
#include <QTimeZone>

#include <algorithm>

namespace pp {
namespace {

constexpr int kDefaultPollIntervalMs = 3000;
constexpr int kMinPollIntervalMs = 500;
constexpr int kMaxPollIntervalMs = 60000;

// TickData 落盘的成交量单位是「手」，PPTrader 内部统一用「股」。
// （与 TickDataCsvImporter 的口径一致：手 × 100 = 股。）
constexpr double kLotsToShares = 100.0;

QDateTime shanghaiToUtc(const QDate &date, const QTime &time)
{
    return QDateTime(date, time, QTimeZone("Asia/Shanghai")).toUTC();
}

RawTradeDirection directionFromSide(const QString &side, bool *known)
{
    if (side == QStringLiteral("B")) {
        if (known) *known = true;
        return RawTradeDirection::Buy;
    }
    if (side == QStringLiteral("S")) {
        if (known) *known = true;
        return RawTradeDirection::Sell;
    }
    // M=中性；以及未来出现未知取值时，一律按「中性且不可信」处理，
    // 不要假装知道方向 —— 下游的方向相关统计依赖这个标志。
    if (known) *known = false;
    return RawTradeDirection::Neutral;
}

} // namespace

TickDataLiveProvider::TickDataLiveProvider(QObject *parent)
    : MarketDataProvider(parent)
{
    m_feed = new HttpTickDataLiveFeed(this);
    m_feed->setEndpoint(QUrl(QStringLiteral("http://127.0.0.1:18765")));
    connect(m_feed, &HttpTickDataLiveFeed::batchReady,
            this, &TickDataLiveProvider::handleBatch);
    connect(&m_timer, &QTimer::timeout, this, &TickDataLiveProvider::poll);
}

TickDataLiveProvider::~TickDataLiveProvider() = default;

void TickDataLiveProvider::setEndpoint(const QUrl &endpoint)
{
    m_feed->setEndpoint(endpoint);
}

QUrl TickDataLiveProvider::endpoint() const
{
    return m_feed->endpoint();
}

void TickDataLiveProvider::setTradingDate(const QString &tradingDate)
{
    m_tradingDate = tradingDate.trimmed();
    // 换日期等于换数据源，旧游标必须作废，否则会拿新日期去比较旧日期的时刻。
    m_cursor.clear();
}

QString TickDataLiveProvider::tradingDate() const
{
    return m_tradingDate;
}

void TickDataLiveProvider::setPollIntervalMs(int intervalMs)
{
    const int clamped = std::clamp(intervalMs, kMinPollIntervalMs, kMaxPollIntervalMs);
    m_timer.setInterval(clamped);
}

int TickDataLiveProvider::pollIntervalMs() const
{
    return m_timer.interval() > 0 ? m_timer.interval() : kDefaultPollIntervalMs;
}

qint64 TickDataLiveProvider::deliveredTicks() const
{
    return static_cast<qint64>(m_totalDelivered);
}

void TickDataLiveProvider::start(const QVector<Instrument> &instruments)
{
    m_instruments = instruments;
    m_cursor.clear();
    m_pending.clear();
    m_totalDelivered = 0;

    if (m_instruments.isEmpty()) {
        emit providerError(QStringLiteral("TickData 行情源：未指定任何标的"));
        return;
    }

    // 立即拉一次，避免等到第一个定时器周期才有数据。
    poll();

    m_timer.setInterval(m_timer.interval() > 0 ? m_timer.interval() : kDefaultPollIntervalMs);
    m_timer.start();
}

void TickDataLiveProvider::stop()
{
    m_timer.stop();
    m_pending.clear();
}

void TickDataLiveProvider::poll()
{
    if (m_instruments.isEmpty()) return;
    m_roundRequested = 0;
    m_roundDelivered = 0;
    for (const Instrument &instrument : std::as_const(m_instruments)) {
        if (instrument.symbol.isEmpty()) continue;
        const QString cursor = m_cursor.value(instrument.symbol);
        const quint64 requestId = m_feed->fetch(instrument.symbol, cursor, m_tradingDate);
        m_pending.insert(requestId, instrument.symbol);
        ++m_roundRequested;
    }
    // 本轮待响应数 = 已发出的请求数（响应到达时递减，归零即本轮结束）
    m_roundRemaining = m_roundRequested;
}

void TickDataLiveProvider::handleBatch(quint64 requestId, const LiveTickBatch &batch)
{
    const QString symbol = m_pending.take(requestId);
    if (symbol.isEmpty()) return;

    if (!batch.succeeded) {
        emit providerError(QStringLiteral("TickData %1: %2").arg(symbol, batch.error));
        return;
    }
    if (!batch.available) {
        // 当日还没有该标的的数据文件（未采集/停牌/未上市），不是错误，静默跳过。
        return;
    }

    // 游标只在有新增时前进；服务端保证 nextAfter 单调不减，这里直接采用。
    if (!batch.nextAfter.isEmpty()) m_cursor.insert(symbol, batch.nextAfter);

    const Instrument instrument = [&]() {
        for (const Instrument &candidate : std::as_const(m_instruments)) {
            if (candidate.symbol == symbol) return candidate;
        }
        Instrument fallback;
        fallback.symbol = symbol;
        return fallback;
    }();

    int published = 0;
    for (const LiveTickRecord &record : batch.ticks) {
        publish(instrument, record, batch.tradingDate);
        ++published;
    }

    if (published > 0) {
        m_totalDelivered += static_cast<quint64>(published);
        m_roundDelivered += published;
    }
    // 只有本轮所有响应都回来后才汇总一次（原来在这里逐个 batch 发，
    // 结果 5,000 多个标的就变成 5,000 多条日志，且符号数统计也没有意义）。
    if (m_roundRemaining > 0) --m_roundRemaining;
    if (m_roundRemaining == 0) {
        emit pollCompleted(m_roundRequested, m_roundDelivered);
    }
}

void TickDataLiveProvider::publish(const Instrument &instrument,
                                   const LiveTickRecord &record,
                                   const QString &tradingDate)
{
    const QDate date = QDate::fromString(tradingDate, QStringLiteral("yyyyMMdd"));
    const QTime time = QTime::fromString(record.time, QStringLiteral("HH:mm:ss"));
    if (!date.isValid() || !time.isValid()) {
        emit providerError(QStringLiteral("TickData %1: 时间戳非法（date=%2 time=%3）")
                               .arg(instrument.symbol, tradingDate, record.time));
        return;
    }

    Tick tick;
    tick.instrument = instrument;
    tick.timestamp = shanghaiToUtc(date, time);
    tick.batchId = QStringLiteral("tickdata:%1:%2").arg(tradingDate, record.time);
    tick.price = record.price;
    tick.volume = record.volume * kLotsToShares;   // 手 → 股
    tick.amount = record.amount;

    bool known = false;
    tick.rawTradeDirection = directionFromSide(record.side, &known);
    tick.rawTradeDirectionKnown = known;

    emit tickReceived(tick);
}

} // namespace pp
