#include "services/PaperStrategyRun.h"
#include "services/PaperStrategyBook.h"
#include "services/TradingAccountService.h"
#include "services/SimulatedBrokerGateway.h"
#include "strategy/CustomStrategyValidator.h"

#include <QTest>
#include <QTimeZone>
#include <QJsonDocument>
#include <limits>

using namespace pp;

namespace {
Instrument instrument() { return {QStringLiteral("600519"), QStringLiteral("测试标的"), Market::AShare}; }
QDateTime at(int day, int minute, int second = 0)
{
    return QDateTime(QDate(2026, 9, day), QTime(10, minute, second),
                     QTimeZone("Asia/Shanghai")).toUTC();
}
CompiledCustomStrategy plan()
{
    CompiledCustomStrategy value;
    value.strategyId = QStringLiteral("paper_candidate");
    value.name = QStringLiteral("双指标模拟测试");
    value.version = 1;
    value.indicators = {{CustomIndicatorKind::Sma, 2}, {CustomIndicatorKind::Wma, 2}};
    value.operands = {{CustomOperandKind::Indicator, 0, 0},
                      {CustomOperandKind::Constant, 100},
                      {CustomOperandKind::Indicator, 0, 1},
                      {CustomOperandKind::Constant, 90}};
    value.conditions = {{CustomConditionOp::Greater, {}, 0, 1},
                        {CustomConditionOp::Greater, {}, 2, 1},
                        {CustomConditionOp::And, {0, 1}},
                        {CustomConditionOp::Less, {}, 0, 3}};
    value.entryCondition = 2;
    value.exitCondition = 3;
    value.minimumWarmupBars = 2;
    value.rules.positionFraction = 0.5;
    return value;
}
Candle bar(int minute, double price, int day = 8)
{
    Candle value;
    value.instrument = instrument(); value.startTime = at(day, minute);
    value.periodSeconds = 60;
    value.open = value.high = value.low = value.close = price;
    value.volume = 1000;
    return value;
}
Tick quote(const QDateTime &timestamp, double price)
{
    Tick value; value.instrument = instrument(); value.timestamp = timestamp;
    value.price = value.bid = value.ask = price;
    return value;
}
}

class PaperStrategyRunTest : public QObject {
    Q_OBJECT
private slots:
    void multipleStocksShareOneAccountButNotIndicatorState();
    void anotherStockCannotFillAnOldQuoteOrSpendReservedCash();
    void fullMarketStrategyUsesTheWorkstationsExistingAccount();
    void bookRefusesASecondFundAndKeepsPerStockReplayOrder();
    void replayStocksShareAClockAndRejectTimeTravel();
    void emptyMinutesWarnWithoutStoppingTheSharedAccount();
    void candidatesUseIndependentAccountsAndWaitForNewQuotes();
    void pendingSignalExpiresWithoutBeingExtendedByBars();
    void fixedVersionWarmsUpThenConsumesStableMinutesExactlyOnce();
    void wrongMarketCannotEnterTheFixedAShareRun();
    void rejectedBuyCanGenerateAnotherSignalWithoutPhantomPosition();
    void partialFillsAreDeduplicatedAndStopPreservesThePosition();
    void sameDaySellIsRejectedEvenWhenTheDefinitionDisablesTPlusOne();
    void minuteHistoryHasAnExplicitBudgetAndStopsWithoutResetting();
    void malformedQuoteCannotCreateAPaperFill();
    void catchupBarsAdvanceHoldingDurationWithoutRetroactiveSignals();
    void accountAvailabilityUsesQuoteDayRatherThanTheTestMachinesDate();
    void replayBookKeepsHistoricalClockAndRejectsLiveInput();
};

void PaperStrategyRunTest::multipleStocksShareOneAccountButNotIndicatorState()
{
    const Instrument secondStock{QStringLiteral("000001"), QStringLiteral("第二标的"), Market::AShare};
    PaperStrategyConfig config;
    config.commissionRate = config.slippageRate = 0;
    PaperStrategyRun run(QStringLiteral("portfolio"), plan(), QVector<Instrument>{instrument(), secondStock}, config);
    PaperStrategyRun independent(QStringLiteral("independent"), plan(), instrument(), config);
    auto secondBar = bar(0, 80); secondBar.instrument = secondStock;
    QVERIFY(run.acceptBar(bar(0, 110), at(8, 4), true));
    QVERIFY(run.acceptBar(secondBar, at(8, 4), true));
    QVERIFY(run.acceptBar(bar(1, 110), at(8, 5)));
    secondBar.startTime = at(8, 1);
    QVERIFY(run.acceptBar(secondBar, at(8, 5)));
    QCOMPARE(run.snapshot().value("signalCount").toInt(), 1);
    QVERIFY(run.acceptQuote(quote(at(8, 5, 1), 120), at(8, 5, 1)));
    QCOMPARE(run.snapshot().value("cash").toDouble(), 52000.0);
    secondBar = bar(2, 140); secondBar.instrument = secondStock;
    QVERIFY(run.acceptBar(secondBar, at(8, 6)));
    auto secondQuote = quote(at(8, 6, 1), 100); secondQuote.instrument = secondStock;
    QVERIFY(run.acceptQuote(secondQuote, secondQuote.timestamp));
    const auto state = run.snapshot();
    QCOMPARE(state.value("fillCount").toInt(), 2);
    QCOMPARE(state.value("buyFillCount").toInt(), 2);
    QCOMPARE(state.value("sellFillCount").toInt(), 0);
    const auto executionRecords = state.value("executionRecords").toList();
    QCOMPARE(state.value("executionRecordCount").toInt(), state.value("fillCount").toInt());
    QCOMPARE(executionRecords.size(), 2);
    QCOMPARE(executionRecords.at(0).toMap().value("recordNumber").toInt(), 1);
    QCOMPARE(executionRecords.at(0).toMap().value("symbol").toString(), QStringLiteral("600519"));
    QCOMPARE(executionRecords.at(0).toMap().value("side").toString(), QStringLiteral("买入"));
    QCOMPARE(executionRecords.at(0).toMap().value("quantity").toDouble(), 400.0);
    QCOMPARE(executionRecords.at(1).toMap().value("recordNumber").toInt(), 2);
    QCOMPARE(executionRecords.at(1).toMap().value("symbol").toString(), QStringLiteral("000001"));
    QCOMPARE(executionRecords.at(1).toMap().value("side").toString(), QStringLiteral("买入"));
    QVERIFY(!executionRecords.at(1).toMap().value("timestampUtc").toString().isEmpty());
    QCOMPARE(state.value("cash").toDouble(), 2000.0);
    QCOMPARE(state.value("initialCapital").toDouble(), 100000.0);
    QCOMPARE(state.value("marketValue").toDouble(), 98000.0);
    QCOMPARE(state.value("totalPnl").toDouble(), 0.0);
    QVERIFY(state.value("accountingIdentityValid").toBool());
    QVERIFY(std::abs(state.value("accountingIdentityDelta").toDouble()) < 0.005);
    QCOMPARE(state.value("positionPercent").toDouble(), 98.0);
    QCOMPARE(state.value("holdingSymbolCount").toInt(), 2);
    QCOMPARE(state.value("sellableSymbolCount").toInt(), 0);
    QCOMPARE(state.value("blockedSignalCount").toInt(), 0);
    QCOMPARE(state.value("orderRejectedCount").toInt(), 0);
    QCOMPARE(state.value("stockStates").toList().size(), 2);
    QCOMPARE(state.value("positionRecords").toList().size(), 2);
    const auto firstPosition = state.value("stockStates").toList().first().toMap();
    QCOMPARE(firstPosition.value("marketPrice").toDouble(), 100.0);
    QVERIFY(firstPosition.value("marketValue").toDouble() > 0.0);
    QCOMPARE(firstPosition.value("unrealizedPnl").toDouble(), 0.0);
    QCOMPARE(independent.snapshot().value("cash").toDouble(), 100000.0);
    QCOMPARE(independent.snapshot().value("fillCount").toInt(), 0);
}

void PaperStrategyRunTest::anotherStockCannotFillAnOldQuoteOrSpendReservedCash()
{
    const Instrument other{QStringLiteral("000001"), QStringLiteral("第二标的"), Market::AShare};
    PaperStrategyConfig config;
    config.commissionRate = config.slippageRate = 0;
    config.maximumFillSharesPerQuote = 100;
    auto fullBudget = plan(); fullBudget.rules.positionFraction = 1;
    PaperStrategyRun run(QStringLiteral("reserved"), fullBudget, QVector<Instrument>{instrument(), other}, config);
    for (int i = 0; i < 2; ++i) {
        QVERIFY(run.acceptBar(bar(i, 110), at(8, i + 4), i == 0));
        auto b = bar(i, 110); b.instrument = other;
        QVERIFY(run.acceptBar(b, at(8, i + 4), i == 0));
    }
    QVERIFY(run.acceptQuote(quote(at(8, 5, 1), 100), at(8, 5, 1)));
    QCOMPARE(run.snapshot().value("cash").toDouble(), 90000.0);
    QCOMPARE(run.snapshot().value("frozenCash").toDouble(), 90000.0);
    auto q = quote(at(8, 5, 2), 100); q.instrument = other;
    QVERIFY(run.acceptQuote(q, q.timestamp));
    QCOMPARE(run.snapshot().value("fillCount").toInt(), 1);
    QCOMPARE(run.snapshot().value("cash").toDouble(), 90000.0);
    QCOMPARE(run.snapshot().value("rejectedCount").toInt(), 1);
    QCOMPARE(run.snapshot().value("rejectedBuyCashCount").toInt(), 1);
    QCOMPARE(run.snapshot().value("rejectedBuyPositionLimitCount").toInt(), 0);
    QCOMPARE(run.snapshot().value("blockedSignalCount").toInt(), 1);
    QCOMPARE(run.snapshot().value("orderRejectedCount").toInt(), 0);
    QVERIFY(run.snapshot().value("error").toString().startsWith(QStringLiteral("策略未提交：")));
    QCOMPARE(run.snapshot().value("events").toList().last().toMap().value("kind").toString(),
        QStringLiteral("blocked"));
    run.stop();
    QCOMPARE(run.snapshot().value("frozenCash").toDouble(), 0.0);
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 100.0);
}

void PaperStrategyRunTest::fullMarketStrategyUsesTheWorkstationsExistingAccount()
{
    SimulatedBrokerGateway broker(0, 0);
    TradingAccountService account(&broker, 100000, 0, 0);
    QVector<Instrument> stocks;
    for (int i = 1; i <= 5207; ++i)
        stocks.append({QStringLiteral("%1").arg(i, 6, 10, QChar('0')), QStringLiteral("容量夹具"), Market::AShare});
    PaperStrategyConfig config; config.commissionRate = config.slippageRate = 0;
    config.maximumTotalPositionFraction = 0.3;
    config.maximumHoldingSymbols = 1;
    {
        PaperStrategyRun run(QStringLiteral("one-account"), plan(), stocks, config, account, broker);
        QVERIFY(!run.snapshot().value("stopped").toBool());
        for (int i = 0; i < 2; ++i) {
            for (const auto &stock : stocks) {
                auto b = bar(i, 110); b.instrument = stock;
                QVERIFY(run.acceptBar(b, at(8, i + 4), i == 0));
            }
        }
        QCOMPARE(run.snapshot().value("barCount").toInt(), 10414);
        auto q = quote(at(8, 5, 1), 100); q.instrument = stocks[0];
        QVERIFY(run.acceptQuote(q, q.timestamp));
        QCOMPARE(account.snapshot().cash, 70000.0);
        QCOMPARE(account.positions().size(), 1);
        QCOMPARE(account.positions().first().quantity, 300.0);
        q.instrument = stocks[1]; q.timestamp = at(8, 5, 2);
        QVERIFY(run.acceptQuote(q, q.timestamp));
        QCOMPARE(account.positions().size(), 1);
        run.stop();
    }
    QCOMPARE(account.snapshot().cash, 70000.0);
    QCOMPARE(account.positions().first().quantity, 300.0);
}

void PaperStrategyRunTest::bookRefusesASecondFundAndKeepsPerStockReplayOrder()
{
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY(CustomStrategyValidator{}.parseAndValidate(QString::fromUtf8(R"({"schemaVersion":1,
        "strategyId":"custom_one_fund","version":1,"name":"一仓","description":"流程","author":"Qt Test",
        "market":"A股","executionScope":"mock_execution","createdAt":"2026-09-08T00:00:00Z",
        "updatedAt":"2026-09-08T00:00:00Z","parameters":[],
        "entry":{"op":"gt","left":{"field":"close"},"right":{"value":100}},
        "exit":{"op":"lt","left":{"field":"close"},"right":{"value":90}}})"), &definition, &error));
    SimulatedBrokerGateway broker(0, 0);
    TradingAccountService account(&broker, 100000, 0, 0);
    PaperStrategyBook book(nullptr, PaperStrategyBook::InputMode::HistoricalReplay);
    book.bindAccount(account, broker);
    const Instrument other{QStringLiteral("000001"), QStringLiteral("第二标的"), Market::AShare};
    PaperStrategyConfig config; config.commissionRate = config.slippageRate = 0;
    QVERIFY(!book.createRun(definition, QVector<Instrument>{instrument(), other}, config, at(3, 0), &error).isEmpty());
    QCOMPARE(account.snapshot().tradingDay, QDate(2026, 9, 3));
    QCOMPARE(account.snapshot().initialEquity, 100000.0);
    QVERIFY(book.createRun(definition, instrument(), config, at(3, 0), &error).isEmpty());
    QVERIFY(book.acceptReplayBar(bar(0, 110, 3), at(3, 3), true));
    auto b = bar(0, 110, 3); b.instrument = other;
    QVERIFY(book.acceptReplayBar(b, at(3, 3), true));
    QVERIFY(book.acceptReplayBar(bar(1, 110, 3), at(3, 4), false));
    book.acceptReplayQuote(quote(at(3, 4, 1), 100));
    QCOMPARE(book.snapshots().size(), 1);
    QCOMPARE(book.snapshots().first().toMap().value("barCount").toInt(), 3);
    QVERIFY(account.positions().size() == 1);
    const double cash = account.snapshot().cash;
    book.stopAll();
    QVERIFY(book.createRun(definition, instrument(), config, at(3, 5), &error).isEmpty());
    QCOMPARE(account.snapshot().cash, cash);
    QCOMPARE(account.positions().size(), 1);
}

void PaperStrategyRunTest::emptyMinutesWarnWithoutStoppingTheSharedAccount()
{
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY(CustomStrategyValidator{}.parseAndValidate(QString::fromUtf8(R"({"schemaVersion":1,
        "strategyId":"custom_sparse_minutes","version":1,"name":"稀疏分钟","description":"流程","author":"Qt Test",
        "market":"A股","executionScope":"mock_execution","createdAt":"2026-09-08T00:00:00Z",
        "updatedAt":"2026-09-08T00:00:00Z","parameters":[],
        "position":{"type":"fixedPercent","percent":0.2},
        "entry":{"op":"gt","left":{"indicator":"SMA","period":2},"right":{"value":100}},
        "exit":{"op":"lt","left":{"field":"close"},"right":{"value":90}}})"), &definition, &error));
    PaperStrategyBook book(nullptr, PaperStrategyBook::InputMode::HistoricalReplay);
    const Instrument second{QStringLiteral("000001"), QStringLiteral("第二股"), Market::AShare};
    QVERIFY(!book.createRun(definition, QVector<Instrument>{instrument(), second}, {}, at(3, 0), &error).isEmpty());
    QVERIFY(book.acceptReplayBar(bar(0, 80, 3), at(3, 3), true));
    auto other = bar(0, 80, 3); other.instrument = second;
    QVERIFY(book.acceptReplayBar(other, at(3, 3), true));
    // 10:01、10:02 没有记录；SMA2 使用真实两根 80/140，不能补出价格80的两根假K。
    QVERIFY(book.acceptReplayBar(bar(3, 140, 3), at(3, 6), false));
    QVERIFY(!book.acceptReplayBar(bar(3, 140, 3), at(3, 6), false));
    other = bar(1, 140, 3); other.instrument = second;
    QVERIFY(book.acceptReplayBar(other, at(3, 6), false));
    auto q = quote(at(3, 6, 1), 100); book.acceptReplayQuote(q);
    q.instrument = second; book.acceptReplayQuote(q);
    auto state = book.snapshots().first().toMap();
    QVERIFY(!state.value("stopped").toBool());
    QCOMPARE(state.value("barCount").toInt(), 4);
    QCOMPARE(state.value("fillCount").toInt(), 2);
    QCOMPARE(state.value("missingMinuteCount").toInt(), 2);
    QVERIFY(state.value("feedError").toString().isEmpty());
    QVERIFY(!state.value("dataQualityWarning").toString().isEmpty());
    const auto warnings = state.value("minuteGapDetails").toList();
    QCOMPARE(warnings.size(), 1);
    QCOMPARE(warnings.first().toMap().value("symbol").toString(), instrument().symbol);
    QVERIFY(book.acceptReplayBar(bar(0, 140, 4), at(4, 3), true));
    QCOMPARE(book.snapshots().first().toMap().value("missingMinuteCount").toInt(), 2);

    // 实时适配层保持同一空分钟语义，但仍执行两次一致响应的确认规则。
    PaperStrategyBook live;
    QVERIFY(!live.createRun(definition, instrument(), {}, at(8, 0), &error).isEmpty());
    auto response = [](int fetchedMinute) {
        RealtimeMinuteSeriesResult result;
        result.instrument = instrument(); result.succeeded = true;
        result.providerId = QStringLiteral("tencent_public_m1");
        result.requestedAt = at(8, fetchedMinute); result.fetchedAt = at(8, fetchedMinute, 1);
        for (int minute : {0, 3, 4, 5, 6}) result.candles.append(bar(minute, 140));
        result.lastBarTime = result.candles.last().startTime;
        return result;
    };
    for (int minute : {7, 8, 9})
        QVERIFY(live.acceptMinutes(live.beginMinuteRequest(instrument().symbol), response(minute)));
    state = live.snapshots().first().toMap();
    QVERIFY(!state.value("stopped").toBool());
    QCOMPARE(state.value("barCount").toInt(), 4);
    QCOMPARE(state.value("missingMinuteCount").toInt(), 2);
    QVERIFY(state.value("feedError").toString().isEmpty());
}

void PaperStrategyRunTest::replayStocksShareAClockAndRejectTimeTravel()
{
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY(CustomStrategyValidator{}.parseAndValidate(QString::fromUtf8(R"({"schemaVersion":1,
        "strategyId":"custom_shared_clock","version":1,"name":"统一时钟","description":"流程","author":"Qt Test",
        "market":"A股","executionScope":"mock_execution","createdAt":"2026-09-08T00:00:00Z",
        "updatedAt":"2026-09-08T00:00:00Z","parameters":[],
        "position":{"type":"fixedPercent","percent":0.2},
        "entry":{"op":"gt","left":{"field":"close"},"right":{"value":100}},
        "exit":{"op":"lt","left":{"field":"close"},"right":{"value":90}}})"), &definition, &error));
    SimulatedBrokerGateway broker(0, 0);
    TradingAccountService account(&broker, 100000, 0, 0);
    PaperStrategyBook book(nullptr, PaperStrategyBook::InputMode::HistoricalReplay);
    book.bindAccount(account, broker);
    const QVector<Instrument> stocks{instrument(), {QStringLiteral("000001"), QStringLiteral("第二股"), Market::AShare},
        {QStringLiteral("000002"), QStringLiteral("第三股"), Market::AShare}};
    PaperStrategyConfig config; config.commissionRate = config.slippageRate = 0;
    QVERIFY(!book.createRun(definition, stocks, config, at(3, 0), &error).isEmpty());
    for (int minute = 0; minute < 2; ++minute) {
        for (const auto &stock : stocks) {
            auto b = bar(minute, 110, 3); b.instrument = stock;
            QVERIFY(book.acceptReplayBar(b, at(3, minute + 3), minute == 0));
        }
    }
    auto q = quote(at(3, 4, 2), 100);
    book.acceptReplayQuote(q);
    q.instrument = stocks[1];
    book.acceptReplayQuote(q); // 不同股票相同时间均须正常处理。
    QCOMPARE(account.executions().size(), 2);
    const double cash = account.snapshot().cash;
    q.instrument = stocks[2]; q.timestamp = at(3, 4, 1);
    book.acceptReplayQuote(q);
    QCOMPARE(account.executions().size(), 2);
    QCOMPARE(account.snapshot().cash, cash);
    const auto state = book.snapshots().first().toMap();
    QVERIFY(state.value("stopped").toBool());
    QVERIFY(state.value("feedError").toString().contains(QStringLiteral("倒退")));
    QCOMPARE(state.value("sourceClockRegressionCount").toInt(), 1);
    QCOMPARE(QDateTime::fromString(state.value("sourceClockUtc").toString(), Qt::ISODateWithMs), at(3, 4, 2));
}

void PaperStrategyRunTest::replayBookKeepsHistoricalClockAndRejectsLiveInput()
{
    const QByteArray json = R"({"schemaVersion":1,"strategyId":"custom_replay_flow","version":1,
        "name":"历史流程","description":"只测流程","author":"Qt Test","market":"A股",
        "executionScope":"mock_execution","createdAt":"2026-09-08T00:00:00Z",
        "updatedAt":"2026-09-08T00:00:00Z","parameters":[],
        "entry":{"op":"gt","left":{"field":"close"},"right":{"value":100}},
        "exit":{"op":"lt","left":{"field":"close"},"right":{"value":90}}})";
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY(CustomStrategyValidator{}.validate(QJsonDocument::fromJson(json).object(), &definition, &error));
    PaperStrategyBook replay(nullptr, PaperStrategyBook::InputMode::HistoricalReplay);
    QVERIFY(!replay.createRun(definition, instrument(), {}, at(3, 0), &error).isEmpty());
    QCOMPARE(replay.beginMinuteRequest(instrument().symbol), quint64(0));
    QVERIFY(!replay.acceptReplayBar(bar(0, 110, 3), at(3, 1), true));
    QVERIFY(replay.acceptReplayBar(bar(0, 110, 3), at(3, 3), true));
    QVERIFY(replay.acceptReplayBar(bar(1, 110, 3), at(3, 4), false));
    replay.acceptQuote(quote(at(3, 4, 1), 110), at(3, 4, 1));
    QCOMPARE(replay.snapshots().first().toMap().value("fillCount").toInt(), 0);
    replay.acceptReplayQuote(quote(at(3, 4, 2), 110));
    const auto row = replay.snapshots().first().toMap();
    QCOMPARE(row.value("fillCount").toInt(), 1);
    QCOMPARE(row.value("sourceMode").toString(), QStringLiteral("nas-historical-replay"));
    QCOMPARE(QDateTime::fromString(row.value("lastQuoteUtc").toString(), Qt::ISODateWithMs), at(3, 4, 2));
    QVERIFY(!replay.acceptReplayBar(bar(1, 110, 3), at(3, 5), false));
    PaperStrategyBook live;
    QVERIFY(!live.createRun(definition, instrument(), {}, at(8, 0), &error).isEmpty());
    QVERIFY(!live.acceptReplayBar(bar(0, 110, 3), at(3, 3), false));
    replay.stopAll();
    QVERIFY(!replay.acceptReplayBar(bar(2, 110, 3), at(3, 5), false));
}

void PaperStrategyRunTest::candidatesUseIndependentAccountsAndWaitForNewQuotes()
{
    PaperStrategyConfig config;
    config.commissionRate = config.slippageRate = 0;
    auto otherPlan = plan();
    otherPlan.strategyId = QStringLiteral("paper_candidate_second");
    otherPlan.version = 2;
    otherPlan.rules.positionFraction = 0.25;
    PaperStrategyRun first(QStringLiteral("A"), plan(), instrument(), config);
    PaperStrategyRun second(QStringLiteral("B"), otherPlan, instrument(), config);
    QVERIFY(first.acceptBar(bar(0, 110), at(8, 4), true));
    QVERIFY(first.acceptBar(bar(1, 110), at(8, 5)));
    QCOMPARE(first.snapshot().value("signalCount").toInt(), 1);
    QCOMPARE(first.snapshot().value("quantity").toDouble(), 0.0);
    QVERIFY(!first.acceptQuote(quote(at(8, 4, 59), 120), at(8, 5, 1)));
    QCOMPARE(first.snapshot().value("quantity").toDouble(), 0.0);
    QVERIFY(first.acceptQuote(quote(at(8, 5, 2), 120), at(8, 5, 3)));
    QCOMPARE(first.snapshot().value("quantity").toDouble(), 400.0);
    QCOMPARE(first.snapshot().value("cash").toDouble(), 52000.0);
    QCOMPARE(first.snapshot().value("averageCost").toDouble(), 120.0);
    QCOMPARE(second.snapshot().value("cash").toDouble(), 100000.0);
    QCOMPARE(second.snapshot().value("quantity").toDouble(), 0.0);
    QVERIFY(second.acceptBar(bar(0, 110), at(8, 4), true));
    QVERIFY(second.acceptBar(bar(1, 110), at(8, 5)));
    QVERIFY(second.acceptQuote(quote(at(8, 5, 2), 120), at(8, 5, 3)));
    QCOMPARE(second.snapshot().value("quantity").toDouble(), 200.0);
    QCOMPARE(second.snapshot().value("cash").toDouble(), 76000.0);
    QCOMPARE(second.snapshot().value("version").toInt(), 2);
    second.stop();
    QVERIFY(!first.snapshot().value("stopped").toBool());
    QCOMPARE(first.snapshot().value("cash").toDouble(), 52000.0);
    QCOMPARE(first.snapshot().value("quantity").toDouble(), 400.0);
}

void PaperStrategyRunTest::pendingSignalExpiresWithoutBeingExtendedByBars()
{
    PaperStrategyRun run(QStringLiteral("A"), plan(), instrument(), {});
    QVERIFY(run.acceptBar(bar(0, 110), at(8, 4), true));
    QVERIFY(run.acceptBar(bar(1, 110), at(8, 5)));
    QVERIFY(run.acceptBar(bar(2, 110), at(8, 6, 31)));
    QVERIFY(run.acceptQuote(quote(at(8, 6, 32), 120), at(8, 6, 33)));
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 0.0);
    QCOMPARE(run.snapshot().value("cancelledCount").toInt(), 1);
}

void PaperStrategyRunTest::fixedVersionWarmsUpThenConsumesStableMinutesExactlyOnce()
{
    const QByteArray json = R"({"schemaVersion":1,"strategyId":"custom_paper_threshold","version":1,
        "name":"模拟测试","description":"固定版本","author":"Qt Test","market":"A股",
        "executionScope":"mock_execution","createdAt":"2026-09-08T00:00:00Z",
        "updatedAt":"2026-09-08T00:00:00Z","parameters":[],
        "entry":{"op":"gt","left":{"field":"close"},"right":{"value":100}},
        "exit":{"op":"lt","left":{"field":"close"},"right":{"value":90}}})";
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(CustomStrategyValidator{}.validate(QJsonDocument::fromJson(json).object(),
                                               &definition, &error), qPrintable(error));
    PaperStrategyBook book;
    const QString id = book.createRun(definition, instrument(), {}, at(8, 0), &error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    auto result = [](int minute) {
        RealtimeMinuteSeriesResult value;
        value.instrument = instrument(); value.succeeded = true;
        value.providerId = QStringLiteral("tencent_public_m1");
        value.requestedAt = at(8, minute); value.fetchedAt = at(8, minute, 1);
        for (int i = 0; i < minute; ++i) value.candles.append(bar(i, 110));
        value.lastBarTime = value.candles.last().startTime;
        return value;
    };
    const auto request1 = book.beginMinuteRequest(instrument().symbol);
    QCOMPARE(book.beginMinuteRequest(instrument().symbol), quint64(0));
    QVERIFY(book.acceptMinutes(request1, result(4)));
    const auto request2 = book.beginMinuteRequest(instrument().symbol);
    QVERIFY(book.acceptMinutes(request2, result(5)));
    QCOMPARE(book.snapshots().first().toMap().value("signalCount").toInt(), 0);
    const auto request3 = book.beginMinuteRequest(instrument().symbol);
    QVERIFY(book.acceptMinutes(request3, result(6)));
    const auto state = book.snapshots().first().toMap();
    QCOMPARE(state.value("signalCount").toInt(), 1);
    QCOMPARE(state.value("version").toInt(), 1);
    QCOMPARE(state.value("lastBarUtc").toString(), at(8, 3).toString(Qt::ISODateWithMs));
    QVERIFY(!book.acceptMinutes(request2, result(5)));
    QVERIFY(!book.acceptMinutes(request3, result(6)));
    QCOMPARE(book.snapshots().first().toMap().value("signalCount").toInt(), 1);
    QCOMPARE(book.subscribedSymbols(), QStringList{instrument().symbol});
    auto wrongMarket = result(7);
    wrongMarket.instrument.market = Market::Crypto;
    QVERIFY(!book.acceptMinutes(book.beginMinuteRequest(instrument().symbol), wrongMarket));
    QCOMPARE(book.snapshots().first().toMap().value("barCount").toInt(), 4);
    auto revised = result(7);
    revised.candles[0].high = 112;
    QVERIFY(book.acceptMinutes(book.beginMinuteRequest(instrument().symbol), revised));
    QVERIFY(book.snapshots().first().toMap().value("stopped").toBool());
    QVERIFY(book.snapshots().first().toMap().value("feedError").toString().contains(QStringLiteral("修订")));
    QVERIFY(book.subscribedSymbols().isEmpty());

    PaperStrategyBook delayedBook;
    QVERIFY(!delayedBook.createRun(definition, instrument(), {}, at(8, 0), &error).isEmpty());
    QVERIFY(delayedBook.acceptMinutes(delayedBook.beginMinuteRequest(instrument().symbol), result(4)));
    QVERIFY(delayedBook.acceptMinutes(delayedBook.beginMinuteRequest(instrument().symbol), result(5)));
    auto yesterdayOnly = result(6);
    yesterdayOnly.requestedAt = at(9, 0);
    yesterdayOnly.fetchedAt = at(9, 0, 1);
    QVERIFY(delayedBook.acceptMinutes(delayedBook.beginMinuteRequest(instrument().symbol), yesterdayOnly));
    QCOMPARE(delayedBook.snapshots().first().toMap().value("signalCount").toInt(), 0);
}

void PaperStrategyRunTest::wrongMarketCannotEnterTheFixedAShareRun()
{
    PaperStrategyRun run(QStringLiteral("A"), plan(), instrument(), {});
    auto incoming = bar(0, 110);
    incoming.instrument.market = Market::Crypto;
    QVERIFY(!run.acceptBar(incoming, at(8, 4), true));
    QCOMPARE(run.snapshot().value("barCount").toInt(), 0);
}

void PaperStrategyRunTest::rejectedBuyCanGenerateAnotherSignalWithoutPhantomPosition()
{
    PaperStrategyConfig config;
    config.initialCapital = 1000;
    PaperStrategyRun run(QStringLiteral("poor"), plan(), instrument(), config);
    QVERIFY(run.acceptBar(bar(0, 110), at(8, 4), true));
    QVERIFY(run.acceptBar(bar(1, 110), at(8, 5)));
    QVERIFY(run.acceptQuote(quote(at(8, 5, 2), 120), at(8, 5, 3)));
    QCOMPARE(run.snapshot().value("rejectedCount").toInt(), 1);
    QCOMPARE(run.snapshot().value("rejectedBuyCashCount").toInt(), 1);
    QCOMPARE(run.snapshot().value("rejectedBuyPositionLimitCount").toInt(), 0);
    QCOMPARE(run.snapshot().value("rejectedSellTPlusOneCount").toInt(), 0);
    QCOMPARE(run.snapshot().value("rejectedSellNoPositionCount").toInt(), 0);
    QCOMPARE(run.snapshot().value("rejectedSubmitCount").toInt(), 0);
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 0.0);
    QVERIFY(run.acceptBar(bar(2, 110), at(8, 6)));
    QCOMPARE(run.snapshot().value("signalCount").toInt(), 2);
    QCOMPARE(run.snapshot().value("cash").toDouble(), 1000.0);
}

void PaperStrategyRunTest::partialFillsAreDeduplicatedAndStopPreservesThePosition()
{
    PaperStrategyConfig config;
    config.commissionRate = config.slippageRate = 0;
    config.maximumFillSharesPerQuote = 100;
    PaperStrategyRun run(QStringLiteral("partial"), plan(), instrument(), config);
    QVERIFY(run.acceptBar(bar(0, 110), at(8, 4), true));
    QVERIFY(run.acceptBar(bar(1, 110), at(8, 5)));
    const Tick first = quote(at(8, 5, 2), 120);
    QVERIFY(run.acceptQuote(first, at(8, 5, 3)));
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 100.0);
    QVERIFY(run.snapshot().value("activeOrder").toBool());
    QVERIFY(!run.acceptQuote(first, at(8, 5, 4)));
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 100.0);
    QVERIFY(run.acceptQuote(quote(at(8, 5, 4), 120), at(8, 5, 5)));
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 200.0);
    run.stop();
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 200.0);
    QCOMPARE(run.snapshot().value("cash").toDouble(), 76000.0);
    QVERIFY(!run.snapshot().value("activeOrder").toBool());
    QVERIFY(!run.acceptQuote(quote(at(8, 5, 6), 120), at(8, 5, 7)));
}

void PaperStrategyRunTest::sameDaySellIsRejectedEvenWhenTheDefinitionDisablesTPlusOne()
{
    auto compiled = plan();
    compiled.rules.tPlusOne = false;
    PaperStrategyConfig config;
    config.commissionRate = config.slippageRate = 0;
    PaperStrategyRun run(QStringLiteral("tplusone"), compiled, instrument(), config);
    QVERIFY(run.acceptBar(bar(0, 110), at(8, 4), true));
    QVERIFY(run.acceptBar(bar(1, 110), at(8, 5)));
    QVERIFY(run.acceptQuote(quote(at(8, 5, 2), 120), at(8, 5, 3)));
    QVERIFY(run.acceptBar(bar(2, 60), at(8, 6)));
    QVERIFY(run.acceptQuote(quote(at(8, 6, 2), 60), at(8, 6, 3)));
    QCOMPARE(run.snapshot().value("rejectedCount").toInt(), 1);
    QCOMPARE(run.snapshot().value("buyFillCount").toInt(), 1);
    QCOMPARE(run.snapshot().value("sellFillCount").toInt(), 0);
    QCOMPARE(run.snapshot().value("rejectedSellTPlusOneCount").toInt(), 1);
    QCOMPARE(run.snapshot().value("rejectedBuyCashCount").toInt(), 0);
    QCOMPARE(run.snapshot().value("rejectedBuyPositionLimitCount").toInt(), 0);
    QCOMPARE(run.snapshot().value("rejectedSellNoPositionCount").toInt(), 0);
    QCOMPARE(run.snapshot().value("rejectedSubmitCount").toInt(), 0);
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 400.0);
    QCOMPARE(run.snapshot().value("availableQuantity").toDouble(), 0.0);
    QVERIFY(run.acceptBar(bar(0, 60, 9), at(9, 4)));
    QVERIFY(run.acceptQuote(quote(at(9, 4, 2), 60), at(9, 4, 3)));
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 0.0);
    QCOMPARE(run.snapshot().value("cash").toDouble(), 76000.0);
    QCOMPARE(run.snapshot().value("buyFillCount").toInt(), 1);
    QCOMPARE(run.snapshot().value("sellFillCount").toInt(), 1);
}

void PaperStrategyRunTest::minuteHistoryHasAnExplicitBudgetAndStopsWithoutResetting()
{
    PaperStrategyRun run(QStringLiteral("bounded"), plan(), instrument(), {});
    Candle incoming = bar(0, 10);
    for (int i = 0; i < 10000; ++i) {
        incoming.startTime = at(8, 0).addSecs(60 * i);
        QVERIFY(run.acceptBar(incoming, incoming.startTime.addSecs(180), true));
    }
    incoming.startTime = at(8, 0).addSecs(600000);
    QVERIFY(!run.acceptBar(incoming, incoming.startTime.addSecs(180), true));
    const auto state = run.snapshot();
    QCOMPARE(state.value("barCount").toInt(), 10000);
    QVERIFY(state.value("stopped").toBool());
    QVERIFY(state.value("error").toString().contains(QStringLiteral("预算")));
    QCOMPARE(state.value("cash").toDouble(), 100000.0);
}

void PaperStrategyRunTest::malformedQuoteCannotCreateAPaperFill()
{
    PaperStrategyRun run(QStringLiteral("bad-quote"), plan(), instrument(), {});
    QVERIFY(run.acceptBar(bar(0, 110), at(8, 4), true));
    QVERIFY(run.acceptBar(bar(1, 110), at(8, 5)));
    auto incoming = quote(at(8, 5, 2), 120);
    incoming.ask = std::numeric_limits<double>::infinity();
    QVERIFY(!run.acceptQuote(incoming, at(8, 5, 3)));
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 0.0);
    QVERIFY(run.acceptQuote(quote(at(8, 5, 3), 120), at(8, 5, 4)));
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 400.0);
}

void PaperStrategyRunTest::catchupBarsAdvanceHoldingDurationWithoutRetroactiveSignals()
{
    auto compiled = plan();
    compiled.rules.maximumHoldingBars = 3;
    compiled.rules.tPlusOne = false;
    PaperStrategyRun run(QStringLiteral("catchup"), compiled, instrument(), {});
    QVERIFY(run.acceptBar(bar(0, 110), at(8, 4), true));
    QVERIFY(run.acceptBar(bar(1, 110), at(8, 5)));
    QVERIFY(run.acceptQuote(quote(at(8, 5, 2), 120), at(8, 5, 3)));
    QVERIFY(run.acceptBar(bar(2, 110), at(8, 8), true));
    QVERIFY(run.acceptBar(bar(3, 110), at(8, 8), true));
    QCOMPARE(run.snapshot().value("signalCount").toInt(), 1);
    QVERIFY(run.acceptBar(bar(4, 110), at(8, 8)));
    QCOMPARE(run.snapshot().value("signalCount").toInt(), 2);
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 400.0);
}

void PaperStrategyRunTest::accountAvailabilityUsesQuoteDayRatherThanTheTestMachinesDate()
{
    PaperStrategyRun run(QStringLiteral("source-day"), plan(), instrument(), {});
    QVERIFY(run.acceptBar(bar(0, 110, 7), at(7, 4), true));
    QVERIFY(run.acceptBar(bar(1, 110, 7), at(7, 5)));
    QVERIFY(run.acceptQuote(quote(at(7, 5, 2), 120), at(7, 5, 3)));
    QCOMPARE(run.snapshot().value("quantity").toDouble(), 400.0);
    QCOMPARE(run.snapshot().value("availableQuantity").toDouble(), 0.0);
}

QTEST_GUILESS_MAIN(PaperStrategyRunTest)
#include "tst_paper_strategy_run.moc"
