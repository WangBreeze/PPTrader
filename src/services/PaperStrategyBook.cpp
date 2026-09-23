#include "services/PaperStrategyBook.h"
#include "strategy/CustomStrategyValidator.h"

#include <QCryptographicHash>
#include <QMap>
#include <QTimeZone>
#include <QUuid>
#include <cmath>
#include <map>
#include <vector>

namespace pp {
namespace {
bool sameBar(const Candle &a, const Candle &b)
{
    return a.open == b.open && a.high == b.high && a.low == b.low
        && a.close == b.close && a.volume == b.volume;
}
bool interiorGap(const QDateTime &previous, const QDateTime &next)
{
    if (!previous.isValid() || previous.secsTo(next) <= 60) return false;
    const auto a = previous.toTimeZone(QTimeZone("Asia/Shanghai"));
    const auto b = next.toTimeZone(QTimeZone("Asia/Shanghai"));
    // 午休/跨日不按自然分钟补造行情；本规则仅识别同一连续交易段内部缺口。
    return a.date() == b.date()
        && ((a.time() >= QTime(9, 30) && b.time() <= QTime(11, 30))
            || (a.time() >= QTime(13, 0) && b.time() <= QTime(15, 0)));
}
}

struct PaperStrategyBook::State {
    struct Feed {
        quint64 requested = 0;
        bool inFlight = false;
        QDateTime previousAt;
        QDateTime lastSuccess;
        QMap<QDateTime, Candle> previous;
        QString error;
        int successful = 0;
        int failed = 0;
    };
    struct Entry {
        std::shared_ptr<PaperStrategyRun> run;
        QString symbol;
        QString definitionSha256;
        QDateTime startedAt;
        QDateTime lastDelivered;
        QMap<QDateTime, Candle> consumed;
        bool seeded = false;
        QString blockedReason;
        qint64 missingMinutes = 0;
        QDateTime lastGapPrevious;
        QDateTime lastGapNext;
        void recordGap(const QDateTime &next)
        {
            if (!interiorGap(lastDelivered, next)) return;
            missingMinutes += lastDelivered.secsTo(next) / 60 - 1;
            lastGapPrevious = lastDelivered;
            lastGapNext = next;
        }
    };
    quint64 nextRequest = 0;
    QDateTime sourceClock;
    int sourceClockRegressionCount = 0;
    TradingAccountService *account = nullptr;
    SimulatedBrokerGateway *broker = nullptr;
    std::map<QString, Feed> feeds;
    std::vector<Entry> entries;
};
PaperStrategyBook::PaperStrategyBook(QObject *parent, InputMode mode)
    : QObject(parent), m_state(std::make_unique<State>()), m_mode(mode) {}
PaperStrategyBook::~PaperStrategyBook() { stopAll(); }
bool PaperStrategyBook::hasRun() const { return !m_state->entries.empty(); }

void PaperStrategyBook::bindAccount(TradingAccountService &account, SimulatedBrokerGateway &broker)
{
    if (!m_state->entries.empty()) return;
    m_state->account = &account;
    m_state->broker = &broker;
}

QString PaperStrategyBook::createRun(const CustomStrategyDefinition &definition,
                                    const Instrument &instrument, const PaperStrategyConfig &config,
                                    const QDateTime &startedAt, QString *error)
{
    return createRun(definition, QVector<Instrument>{instrument}, config, startedAt, error);
}

QString PaperStrategyBook::createRun(const CustomStrategyDefinition &definition,
                                    const QVector<Instrument> &instruments, const PaperStrategyConfig &config,
                                    const QDateTime &startedAt, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return QString{};
    };
    if (!m_state->entries.empty()) return fail(QStringLiteral("工作台已有策略运行记录；停止不重置资金，安全接管验收前禁止再次启动"));
    CustomStrategyDefinition validated;
    QString detail;
    if (!CustomStrategyValidator{}.parseAndValidate(definition.canonicalJson(), &validated, &detail))
        return fail(detail);
    if (validated.executionScope != QStringLiteral("mock_execution"))
        return fail(QStringLiteral("仅回测版本不可直接运行；请显式发布新的模拟版本"));
    const auto plan = CustomStrategyCompiler{}.compile(validated, {}, &detail);
    if (!plan) return fail(detail);
    if (!plan->crossIndicators.isEmpty() || !plan->universeSymbols.isEmpty()
        || plan->minimumWarmupBars > 300 || !startedAt.isValid())
        return fail(QStringLiteral("当前支持股票池逐股计算、1m、最多 300 根预热；横截面策略另行接入"));
    const QString id = QStringLiteral("paper-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    PaperStrategyConfig settings = config;
    settings.historicalSourceStart = m_mode == InputMode::HistoricalReplay ? startedAt : QDateTime{};
    auto run = m_state->account
        ? std::make_shared<PaperStrategyRun>(id, *plan, instruments, settings, *m_state->account, *m_state->broker)
        : std::make_shared<PaperStrategyRun>(id, *plan, instruments, settings);
    if (run->isStopped()) return fail(run->snapshot().value("error").toString());
    if (m_mode == InputMode::HistoricalReplay) m_state->sourceClock = startedAt.toUTC();
    const auto digest = QString::fromLatin1(QCryptographicHash::hash(
        validated.canonicalJson().toUtf8(), QCryptographicHash::Sha256).toHex());
    for (const auto &instrument : instruments) {
        State::Entry entry;
        entry.run = run;
        entry.symbol = instrument.symbol;
        entry.startedAt = startedAt;
        entry.definitionSha256 = digest;
        m_state->feeds.try_emplace(entry.symbol);
        m_state->entries.push_back(std::move(entry));
    }
    if (error) error->clear();
    emit changed();
    return id;
}

quint64 PaperStrategyBook::beginMinuteRequest(const QString &symbol)
{
    if (m_mode != InputMode::Live) return 0;
    auto iterator = m_state->feeds.find(symbol);
    if (iterator == m_state->feeds.end() || !subscribedSymbols().contains(symbol)) return 0;
    auto &feed = iterator->second;
    if (feed.inFlight) return 0;
    feed.requested = ++m_state->nextRequest;
    feed.inFlight = true;
    return feed.requested;
}

bool PaperStrategyBook::acceptMinutes(quint64 requestId, const RealtimeMinuteSeriesResult &result)
{
    if (m_mode != InputMode::Live) return false;
    const auto iterator = m_state->feeds.find(result.instrument.symbol);
    if (iterator == m_state->feeds.end()) return false;
    auto &feed = iterator->second;
    if (!feed.inFlight || requestId == 0 || requestId != feed.requested) return false;
    feed.inFlight = false;
    const auto fail = [&](const QString &message) {
        feed.error = message;
        ++feed.failed;
        emit changed();
        return false;
    };
    if (!result.succeeded || result.candles.isEmpty())
        return fail(result.error.isEmpty() ? QStringLiteral("分钟数据为空，暂停模拟成交") : result.error);
    if (result.instrument.market != Market::AShare
        || result.providerId != QStringLiteral("tencent_public_m1") || result.candles.size() > 320
        || !result.requestedAt.isValid() || !result.fetchedAt.isValid()
        || result.fetchedAt < result.requestedAt
        || (feed.lastSuccess.isValid() && result.fetchedAt <= feed.lastSuccess))
        return fail(QStringLiteral("分钟来源或响应时刻无效"));
    QMap<QDateTime, Candle> current;
    QDateTime previousTime;
    for (const Candle &candle : result.candles) {
        if (candle.instrument.symbol != result.instrument.symbol || candle.periodSeconds != 60
            || candle.instrument.market != result.instrument.market
            || !candle.startTime.isValid() || (previousTime.isValid() && candle.startTime <= previousTime)
            || candle.startTime.time().second() != 0 || candle.startTime.time().msec() != 0
            || !std::isfinite(candle.open) || !std::isfinite(candle.high)
            || !std::isfinite(candle.low) || !std::isfinite(candle.close)
            || !std::isfinite(candle.volume) || candle.open <= 0 || candle.close <= 0
            || candle.low <= 0 || candle.high < candle.low || candle.volume < 0
            || candle.high < std::max(candle.open, candle.close)
            || candle.low > std::min(candle.open, candle.close))
            return fail(QStringLiteral("分钟序列包含错品种、乱序、重复或非法 OHLCV"));
        current.insert(candle.startTime, candle);
        previousTime = candle.startTime;
    }
    QVector<Candle> stable;
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        const auto old = feed.previous.constFind(it.key());
        if (old != feed.previous.cend() && sameBar(*old, *it)
            && feed.previousAt.secsTo(result.fetchedAt) >= 30
            && it.key().addSecs(180) <= result.fetchedAt
            && it.key() < current.lastKey()) stable.append(*it);
    }
    for (auto &entry : m_state->entries) {
        if (entry.symbol != result.instrument.symbol || entry.run->isStopped()) continue;
        for (auto it = entry.consumed.cbegin(); it != entry.consumed.cend(); ++it) {
            const auto incoming = current.constFind(it.key());
            if (incoming != current.cend() && !sameBar(*incoming, *it)) {
                entry.blockedReason = QStringLiteral("已计算的分钟发生修订，停止本实验；不重写成交或伪造连续收益");
                entry.run->stop();
                break;
            }
        }
        if (!entry.blockedReason.isEmpty()) continue;
        const bool initial = !entry.seeded;
        for (qsizetype i = 0; i < stable.size(); ++i) {
            const auto &candle = stable.at(i);
            if (entry.lastDelivered.isValid() && candle.startTime <= entry.lastDelivered) continue;
            const QTimeZone marketZone("Asia/Shanghai");
            const bool freshToday = candle.startTime.toTimeZone(marketZone).date()
                    == result.fetchedAt.toTimeZone(marketZone).date()
                && candle.startTime.secsTo(result.fetchedAt) <= 300;
            const bool warmup = initial || !freshToday || i + 1 != stable.size()
                || candle.startTime < entry.startedAt;
            if (entry.run->acceptBar(candle, result.fetchedAt, warmup)) {
                entry.recordGap(candle.startTime);
                entry.lastDelivered = candle.startTime;
                entry.consumed.insert(candle.startTime, candle);
                while (entry.consumed.size() > 320) entry.consumed.erase(entry.consumed.begin());
                entry.seeded = true;
            }
        }
    }
    feed.previous = current;
    feed.previousAt = result.fetchedAt;
    feed.lastSuccess = result.fetchedAt;
    feed.error.clear();
    ++feed.successful;
    emit changed();
    return true;
}

void PaperStrategyBook::acceptQuote(const Tick &quote, const QDateTime &receivedAt)
{
    if (m_mode != InputMode::Live) return;
    const auto feed = m_state->feeds.find(quote.instrument.symbol);
    if (feed == m_state->feeds.end() || !feed->second.error.isEmpty()
        || !feed->second.lastSuccess.isValid() || feed->second.lastSuccess.secsTo(receivedAt) > 90) return;
    bool changedState = false;
    for (auto &entry : m_state->entries)
        if (entry.symbol == quote.instrument.symbol)
            changedState = entry.run->acceptQuote(quote, receivedAt) || changedState;
    if (changedState) emit changed();
}

bool PaperStrategyBook::validateReplayClock(const QDateTime &sourceClock)
{
    if (!sourceClock.isValid() || m_state->entries.empty()
        || m_state->entries.front().run->isStopped()) return false;
    if (sourceClock < m_state->sourceClock) {
        ++m_state->sourceClockRegressionCount;
        m_state->entries.front().blockedReason = QStringLiteral("跨股票源时钟倒退，停止回放并保留资金持仓；不得逐股倒灌历史");
        stopAll();
        emit changed();
        return false;
    }
    return true;
}

bool PaperStrategyBook::acceptReplayBar(const Candle &bar, const QDateTime &sourceClock, bool warmup)
{
    if (m_mode != InputMode::HistoricalReplay) return false;
    if (!m_state->feeds.contains(bar.instrument.symbol) || !validateReplayClock(sourceClock)) return false;
    bool accepted = false;
    for (auto &entry : m_state->entries) {
        if (entry.symbol != bar.instrument.symbol) continue;
        if (entry.run->acceptBar(bar, sourceClock, warmup)) {
            entry.recordGap(bar.startTime);
            entry.lastDelivered = bar.startTime;
            entry.seeded = true;
            accepted = true;
        }
    }
    if (accepted) {
        m_state->sourceClock = sourceClock.toUTC();
        emit changed();
    }
    return accepted;
}

void PaperStrategyBook::acceptReplayQuote(const Tick &quote)
{
    if (m_mode != InputMode::HistoricalReplay) return;
    if (!m_state->feeds.contains(quote.instrument.symbol) || !validateReplayClock(quote.timestamp)) return;
    bool accepted = false;
    for (auto &entry : m_state->entries)
        if (entry.symbol == quote.instrument.symbol)
            accepted = entry.run->acceptQuote(quote, quote.timestamp) || accepted;
    if (accepted) {
        m_state->sourceClock = quote.timestamp.toUTC();
        emit changed();
    }
}

QStringList PaperStrategyBook::subscribedSymbols() const
{
    QStringList symbols;
    for (const auto &entry : m_state->entries)
        if (!entry.run->isStopped())
            symbols.append(entry.symbol);
    return symbols;
}

QVariantList PaperStrategyBook::snapshots() const
{
    QVariantList result;
    if (!m_state->entries.empty()) {
        const auto &entry = m_state->entries.front();
        auto row = entry.run->snapshot();
        int successful = 0, failed = 0, seeded = 0;
        quint64 requested = 0;
        QString feedError;
        qint64 missingMinutes = 0;
        QVariantList gapDetails;
        for (const auto &stock : m_state->entries) {
            const auto &feed = m_state->feeds.at(stock.symbol);
            successful += feed.successful;
            failed += feed.failed;
            seeded += stock.seeded ? 1 : 0;
            requested = std::max(requested, feed.requested);
            const auto detail = stock.blockedReason.isEmpty() ? feed.error : stock.blockedReason;
            if (feedError.isEmpty() && !detail.isEmpty()) feedError = stock.symbol + QStringLiteral(": ") + detail;
            missingMinutes += stock.missingMinutes;
            if (stock.missingMinutes > 0) gapDetails.append(QVariantMap{
                {"symbol", stock.symbol}, {"missingMinutes", stock.missingMinutes},
                {"lastGapPreviousUtc", stock.lastGapPrevious.toString(Qt::ISODateWithMs)},
                {"lastGapNextUtc", stock.lastGapNext.toString(Qt::ISODateWithMs)}});
        }
        row.insert("definitionSha256", entry.definitionSha256);
        row.insert("startedAtUtc", entry.startedAt.toString(Qt::ISODateWithMs));
        row.insert("feedError", feedError);
        row.insert("missingMinuteCount", missingMinutes);
        row.insert("minuteGapDetails", gapDetails);
        row.insert("dataQualityWarning", missingMinutes > 0
            ? QStringLiteral("存在 %1 个无记录分钟（原因未知），继续计算真实 Bar；不补造行情").arg(missingMinutes) : QString{});
        row.insert("indicatorWindowBasis", QStringLiteral("按实际收到的 N 根 Bar 计算，空分钟不补齐；不等于连续 N 分钟"));
        row.insert("minuteSuccessCount", successful);
        row.insert("minuteFailureCount", failed);
        row.insert("minuteRequestId", QVariant::fromValue(requested));
        row.insert("seeded", seeded == static_cast<int>(m_state->entries.size()));
        row.insert("seededSymbolCount", seeded);
        row.insert("sourceClockUtc", m_state->sourceClock.toString(Qt::ISODateWithMs));
        row.insert("sourceClockRegressionCount", m_state->sourceClockRegressionCount);
        row.insert("sourceMode", m_mode == InputMode::Live ? QStringLiteral("live-paper") : QStringLiteral("nas-historical-replay"));
        row.insert("barConfirmation", m_mode == InputMode::Live
            ? QStringLiteral("两次相隔≥30秒一致＋后继标签＋标签后≥180秒；非交易所闭合证明")
            : QStringLiteral("历史原始 Tick 派生 1m；源时钟到标签后≥180秒才交付，不改写历史时刻"));
        row.insert("profitabilityAccepted", false);
        result.append(row);
    }
    return result;
}

bool PaperStrategyBook::stopRun(const QString &runId)
{
    for (auto &entry : m_state->entries) {
        if (entry.run->snapshot().value("runId").toString() != runId) continue;
        entry.run->stop();
        emit changed();
        return true;
    }
    return false;
}

void PaperStrategyBook::stopAll()
{
    if (!m_state->entries.empty()) m_state->entries.front().run->stop();
    for (auto &[symbol, feed] : m_state->feeds) feed.inFlight = false;
}
} // namespace pp
