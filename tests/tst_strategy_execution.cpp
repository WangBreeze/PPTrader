#include "strategy/DefaultSignalExecutionPolicy.h"
#include "strategy/FixedAmountPositionSizer.h"
#include "strategy/StrategyExecutionCoordinator.h"
#include "strategy/StrategyEngine.h"
#include "models/StrategyIntentListModel.h"

#include <QSignalSpy>
#include <QTest>

#include <algorithm>

namespace pp {

class FakeOrderSubmissionPort final : public OrderSubmissionPort {
public:
    QString submitOrder(OrderRequest request) override
    {
        requests.append(request);
        if (failSubmission) return {};
        return QStringLiteral("ORDER-%1").arg(requests.size());
    }

    AccountSnapshot snapshot() const override { return account; }
    QVector<Position> positions() const override { return currentPositions; }

    AccountSnapshot account;
    QVector<Position> currentPositions;
    QVector<OrderRequest> requests;
    bool failSubmission = false;
};

class FakeStrategyExecutionStore final : public StrategyExecutionStore {
public:
    bool upsertStrategyConfiguration(const StrategyExecutionConfig &config) override
    {
        if (failConfigurationWrites) return false;
        for (StrategyExecutionConfig &existing : configurations) {
            if (existing.strategyId == config.strategyId) {
                existing = config;
                return true;
            }
        }
        configurations.append(config);
        return true;
    }

    bool removeStrategyConfiguration(const QString &strategyId) override
    {
        configurations.erase(std::remove_if(configurations.begin(), configurations.end(),
            [&strategyId](const StrategyExecutionConfig &config) {
                return config.strategyId == strategyId;
            }), configurations.end());
        return true;
    }

    bool upsertStrategyIntent(const StrategyOrderIntent &intent) override
    {
        for (StrategyOrderIntent &existing : storedIntents) {
            if (existing.intentId == intent.intentId) {
                existing = intent;
                return true;
            }
        }
        storedIntents.append(intent);
        return true;
    }

    QVector<StrategyExecutionConfig> loadStrategyConfigurations() override { return configurations; }
    QVector<StrategyOrderIntent> loadStrategyIntents() override { return storedIntents; }

    QVector<StrategyExecutionConfig> configurations;
    QVector<StrategyOrderIntent> storedIntents;
    bool failConfigurationWrites = false;
};

class EmittingStrategy final : public Strategy {
public:
    explicit EmittingStrategy(const QString &strategyId = QStringLiteral("test-strategy"),
                              const QStringList &symbols = {}, QObject *parent = nullptr)
        : Strategy(parent)
        , m_strategyId(strategyId)
        , m_symbols(symbols)
    {
    }

    QString id() const override { return m_strategyId; }
    QString displayName() const override { return QStringLiteral("测试策略"); }
    QStringList subscribedSymbols() const override { return m_symbols; }
    void onTick(const Tick &tick) override
    {
        TradeSignal signal;
        signal.instrument = tick.instrument;
        signal.timestamp = tick.timestamp;
        signal.price = tick.price;
        signal.side = SignalSide::Buy;
        emit signalGenerated(signal);
    }
    void onCandle(const Candle &) override {}

private:
    QString m_strategyId;
    QStringList m_symbols;
};

class StrategyExecutionTest final : public QObject {
    Q_OBJECT

private slots:
    void routesAutomaticAndManualExecutionWithoutUiDependencies();
    void rejectsMissingConfigurationAndZeroQuantity();
    void persistsConfigurationAndExpiresPendingIntentOnRestore();
    void enforcesDailyStrategyBudgetsAndBuildsOrderLifetime();
    void preservesStrategyIdentityWhenDispatchingSignals();
    void routesTicksOnlyToSubscribedStrategies();
    void recordsOptionalDispatchMetricsAndDrainsPool();
    void normalizesStrategyExecutionBounds();
    void reportsConfigurationPersistenceFailureAndSortsConfigurations();
    void exposesStrategyIntentRolesAndUpdatesRows();
};

void StrategyExecutionTest::recordsOptionalDispatchMetricsAndDrainsPool()
{
    StrategyEngine engine;
    engine.setMaxThreadCount(2);
    engine.addStrategy(new EmittingStrategy(
        QStringLiteral("metric-1"), {QStringLiteral("000001")}));
    engine.addStrategy(new EmittingStrategy(
        QStringLiteral("metric-2"), {QStringLiteral("600519")}));
    engine.setPerformanceMetricsEnabled(true);
    engine.resetPerformanceMetrics();

    Tick first;
    first.instrument.symbol = QStringLiteral("000001");
    first.timestamp = QDateTime::currentDateTimeUtc();
    first.price = 10.0;
    Tick second = first;
    second.instrument.symbol = QStringLiteral("600519");
    engine.dispatchTick(first);
    engine.dispatchTick(second);

    QCOMPARE(engine.dispatchedTaskCount(), quint64(2));
    QVERIFY(engine.waitForIdle(5000));
    QCOMPARE(engine.completedTaskCount(), quint64(2));

    engine.setPerformanceMetricsEnabled(false);
    engine.dispatchTick(first);
    QVERIFY(engine.waitForIdle(5000));
    QCOMPARE(engine.dispatchedTaskCount(), quint64(2));
    QCOMPARE(engine.completedTaskCount(), quint64(2));
}

void StrategyExecutionTest::routesAutomaticAndManualExecutionWithoutUiDependencies()
{
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("Kweichow Moutai");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");

    FakeOrderSubmissionPort port;
    port.account.initialEquity = 100000.0;
    port.account.cash = 100000.0;
    port.account.totalEquity = 100000.0;
    FixedAmountPositionSizer sizer;
    DefaultSignalExecutionPolicy policy;
    StrategyExecutionCoordinator coordinator(&port, &sizer, &policy);
    QSignalSpy approvalSpy(&coordinator, &StrategyExecutionCoordinator::approvalRequired);
    QSignalSpy submittedSpy(&coordinator, &StrategyExecutionCoordinator::orderSubmitted);

    StrategyExecutionConfig automatic;
    automatic.strategyId = QStringLiteral("auto-sma");
    automatic.enabled = true;
    automatic.mode = StrategyExecutionMode::Automatic;
    automatic.orderType = OrderType::Market;
    automatic.fixedOrderValue = 10000.0;
    automatic.maxPositionPercent = 20.0;
    automatic.cooldownSeconds = 30;
    coordinator.setConfiguration(automatic);

    TradeSignal buySignal;
    buySignal.instrument = instrument;
    buySignal.side = SignalSide::Buy;
    buySignal.price = 100.0;
    buySignal.timestamp = QDateTime::currentDateTimeUtc();
    const QString automaticIntentId = coordinator.processSignal(automatic.strategyId, buySignal);
    const auto automaticIntent = coordinator.intent(automaticIntentId);
    QVERIFY(automaticIntent.has_value());
    QVERIFY(automaticIntent->status == OrderIntentStatus::Submitted);
    QCOMPARE(port.requests.size(), 1);
    QCOMPARE(port.requests.first().quantity, 100.0);
    QVERIFY(port.requests.first().side == OrderSide::Buy);
    QCOMPARE(submittedSpy.count(), 1);

    TradeSignal cooldownSignal = buySignal;
    cooldownSignal.timestamp = buySignal.timestamp.addSecs(10);
    const auto cooldownIntent = coordinator.intent(
        coordinator.processSignal(automatic.strategyId, cooldownSignal));
    QVERIFY(cooldownIntent.has_value());
    QVERIFY(cooldownIntent->status == OrderIntentStatus::Rejected);
    QCOMPARE(port.requests.size(), 1);

    Position position;
    position.instrument = instrument;
    position.quantity = 300.0;
    position.availableQuantity = 300.0;
    position.averageCost = 95.0;
    position.marketPrice = 100.0;
    position.marketValue = 30000.0;
    port.currentPositions = {position};

    StrategyExecutionConfig manual;
    manual.strategyId = QStringLiteral("manual-exit");
    manual.enabled = true;
    manual.mode = StrategyExecutionMode::ManualApproval;
    manual.orderType = OrderType::Limit;
    manual.limitOffsetBps = 10.0;
    manual.sellAll = true;
    manual.cooldownSeconds = 0;
    coordinator.setConfiguration(manual);

    TradeSignal sellSignal = buySignal;
    sellSignal.side = SignalSide::Sell;
    sellSignal.timestamp = buySignal.timestamp.addSecs(60);
    const QString manualIntentId = coordinator.processSignal(manual.strategyId, sellSignal);
    auto manualIntent = coordinator.intent(manualIntentId);
    QVERIFY(manualIntent.has_value());
    QVERIFY(manualIntent->status == OrderIntentStatus::PendingApproval);
    QCOMPARE(approvalSpy.count(), 1);
    QCOMPARE(port.requests.size(), 1);

    QVERIFY(coordinator.approveIntent(manualIntentId));
    manualIntent = coordinator.intent(manualIntentId);
    QVERIFY(manualIntent->status == OrderIntentStatus::Submitted);
    QCOMPARE(port.requests.size(), 2);
    QCOMPARE(port.requests.last().quantity, 300.0);
    QVERIFY(port.requests.last().side == OrderSide::Sell);
    QVERIFY(qAbs(port.requests.last().limitPrice - 99.9) < 0.000001);

    sellSignal.timestamp = buySignal.timestamp.addSecs(120);
    const QString rejectedIntentId = coordinator.processSignal(manual.strategyId, sellSignal);
    QVERIFY(coordinator.rejectIntent(rejectedIntentId, QStringLiteral("用户取消")));
    const auto rejectedIntent = coordinator.intent(rejectedIntentId);
    QVERIFY(rejectedIntent->status == OrderIntentStatus::Rejected);
    QCOMPARE(port.requests.size(), 2);
}

void StrategyExecutionTest::rejectsMissingConfigurationAndZeroQuantity()
{
    FakeOrderSubmissionPort port;
    port.account.initialEquity = 100000.0;
    port.account.cash = 100000.0;
    port.account.totalEquity = 100000.0;
    FixedAmountPositionSizer sizer;
    DefaultSignalExecutionPolicy policy;
    StrategyExecutionCoordinator coordinator(&port, &sizer, &policy);

    TradeSignal signal;
    signal.instrument.symbol = QStringLiteral("600519");
    signal.instrument.market = Market::AShare;
    signal.side = SignalSide::Buy;
    signal.price = 100.0;
    signal.timestamp = QDateTime::currentDateTimeUtc();

    auto result = coordinator.intent(coordinator.processSignal(QStringLiteral("missing"), signal));
    QVERIFY(result.has_value());
    QVERIFY(result->status == OrderIntentStatus::Rejected);

    StrategyExecutionConfig tooSmall;
    tooSmall.strategyId = QStringLiteral("too-small");
    tooSmall.enabled = true;
    tooSmall.mode = StrategyExecutionMode::Automatic;
    tooSmall.fixedOrderValue = 5000.0;
    tooSmall.maxPositionPercent = 100.0;
    coordinator.setConfiguration(tooSmall);
    result = coordinator.intent(coordinator.processSignal(tooSmall.strategyId, signal));
    QVERIFY(result.has_value());
    QVERIFY(result->status == OrderIntentStatus::Rejected);
    QVERIFY(port.requests.isEmpty());
}

void StrategyExecutionTest::persistsConfigurationAndExpiresPendingIntentOnRestore()
{
    FakeOrderSubmissionPort port;
    port.account.initialEquity = 100000.0;
    port.account.cash = 100000.0;
    port.account.totalEquity = 100000.0;
    FixedAmountPositionSizer sizer;
    DefaultSignalExecutionPolicy policy;
    FakeStrategyExecutionStore store;

    StrategyExecutionCoordinator coordinator(&port, &sizer, &policy, &store);
    StrategyExecutionConfig config;
    config.strategyId = QStringLiteral("persisted-strategy");
    config.enabled = true;
    config.mode = StrategyExecutionMode::ManualApproval;
    config.fixedOrderValue = 10000.0;
    coordinator.setConfiguration(config);

    TradeSignal signal;
    signal.instrument.symbol = QStringLiteral("600519");
    signal.instrument.market = Market::AShare;
    signal.side = SignalSide::Buy;
    signal.price = 100.0;
    signal.timestamp = QDateTime::currentDateTimeUtc();
    const QString intentId = coordinator.processSignal(config.strategyId, signal);
    QCOMPARE(store.configurations.size(), 1);
    QCOMPARE(store.storedIntents.size(), 1);
    QVERIFY(store.storedIntents.first().status == OrderIntentStatus::PendingApproval);

    StrategyExecutionCoordinator restored(&port, &sizer, &policy, &store);
    restored.restore();
    const auto restoredIntent = restored.intent(intentId);
    QVERIFY(restoredIntent.has_value());
    QVERIFY(restoredIntent->status == OrderIntentStatus::Expired);
    QVERIFY(restored.configuration(config.strategyId).has_value());
    QVERIFY(store.storedIntents.first().status == OrderIntentStatus::Expired);
}

void StrategyExecutionTest::enforcesDailyStrategyBudgetsAndBuildsOrderLifetime()
{
    FakeOrderSubmissionPort port;
    port.account.initialEquity = 100000.0;
    port.account.cash = 100000.0;
    port.account.totalEquity = 100000.0;
    FixedAmountPositionSizer sizer;
    DefaultSignalExecutionPolicy policy;
    StrategyExecutionCoordinator coordinator(&port, &sizer, &policy);

    StrategyExecutionConfig countBudget;
    countBudget.strategyId = QStringLiteral("count-budget");
    countBudget.enabled = true;
    countBudget.mode = StrategyExecutionMode::Automatic;
    countBudget.timeInForce = TimeInForce::ImmediateOrCancel;
    countBudget.orderTimeoutSeconds = 45;
    countBudget.fixedOrderValue = 10000.0;
    countBudget.maxPositionPercent = 100.0;
    countBudget.cooldownSeconds = 0;
    countBudget.maxOrdersPerDay = 1;
    coordinator.setConfiguration(countBudget);

    TradeSignal signal;
    signal.instrument.symbol = QStringLiteral("600519");
    signal.instrument.market = Market::AShare;
    signal.instrument.venue = QStringLiteral("SSE");
    signal.side = SignalSide::Buy;
    signal.price = 100.0;
    signal.timestamp = QDateTime::currentDateTimeUtc();

    auto result = coordinator.intent(coordinator.processSignal(countBudget.strategyId, signal));
    QVERIFY(result.has_value());
    QVERIFY(result->status == OrderIntentStatus::Submitted);
    QCOMPARE(port.requests.size(), 1);
    QVERIFY(port.requests.first().timeInForce == TimeInForce::ImmediateOrCancel);
    QCOMPARE(port.requests.first().expiresAt, signal.timestamp.addSecs(45));

    signal.timestamp = signal.timestamp.addSecs(60);
    result = coordinator.intent(coordinator.processSignal(countBudget.strategyId, signal));
    QVERIFY(result.has_value());
    QVERIFY(result->status == OrderIntentStatus::Rejected);
    QCOMPARE(port.requests.size(), 1);

    StrategyExecutionConfig valueBudget = countBudget;
    valueBudget.strategyId = QStringLiteral("value-budget");
    valueBudget.timeInForce = TimeInForce::Day;
    valueBudget.orderTimeoutSeconds = 0;
    valueBudget.maxOrdersPerDay = 0;
    valueBudget.maxDailyOrderValue = 15000.0;
    coordinator.setConfiguration(valueBudget);

    result = coordinator.intent(coordinator.processSignal(valueBudget.strategyId, signal));
    QVERIFY(result.has_value());
    QVERIFY(result->status == OrderIntentStatus::Submitted);
    QCOMPARE(port.requests.size(), 2);

    signal.timestamp = signal.timestamp.addSecs(60);
    result = coordinator.intent(coordinator.processSignal(valueBudget.strategyId, signal));
    QVERIFY(result.has_value());
    QVERIFY(result->status == OrderIntentStatus::Rejected);
    QCOMPARE(port.requests.size(), 2);
}

void StrategyExecutionTest::preservesStrategyIdentityWhenDispatchingSignals()
{
    StrategyEngine engine;
    engine.setMaxThreadCount(1);
    engine.addStrategy(new EmittingStrategy);
    QSignalSpy signalSpy(&engine, &StrategyEngine::strategySignalGenerated);

    Tick tick;
    tick.instrument.symbol = QStringLiteral("600519");
    tick.instrument.market = Market::AShare;
    tick.price = 100.0;
    tick.timestamp = QDateTime::currentDateTimeUtc();
    engine.dispatchTick(tick);

    QTRY_COMPARE(signalSpy.count(), 1);
    QCOMPARE(signalSpy.at(0).at(0).toString(), QStringLiteral("test-strategy"));
    QCOMPARE(signalSpy.at(0).at(1).value<TradeSignal>().instrument.symbol,
             QStringLiteral("600519"));
}

void StrategyExecutionTest::routesTicksOnlyToSubscribedStrategies()
{
    StrategyEngine engine;
    engine.setMaxThreadCount(1);
    engine.addStrategy(new EmittingStrategy(QStringLiteral("maotai"), {QStringLiteral("600519")}));
    engine.addStrategy(new EmittingStrategy(QStringLiteral("ningde"), {QStringLiteral("300750")}));
    QSignalSpy signalSpy(&engine, &StrategyEngine::strategySignalGenerated);

    Tick tick;
    tick.instrument.symbol = QStringLiteral("600519");
    tick.instrument.market = Market::AShare;
    tick.price = 100.0;
    tick.timestamp = QDateTime::currentDateTimeUtc();
    engine.dispatchTick(tick);

    QTRY_COMPARE(signalSpy.count(), 1);
    QCOMPARE(signalSpy.first().at(0).toString(), QStringLiteral("maotai"));
}

void StrategyExecutionTest::normalizesStrategyExecutionBounds()
{
    FakeOrderSubmissionPort port;
    FixedAmountPositionSizer sizer;
    DefaultSignalExecutionPolicy policy;
    StrategyExecutionCoordinator coordinator(&port, &sizer, &policy);

    StrategyExecutionConfig config;
    config.strategyId = QStringLiteral("normalized");
    config.fixedOrderValue = -1.0;
    config.maxPositionPercent = -2.0;
    config.cooldownSeconds = -3;
    config.approvalTimeoutSeconds = -4;
    config.orderTimeoutSeconds = -5;
    config.maxOrdersPerDay = -6;
    config.maxDailyOrderValue = -7.0;
    coordinator.setConfiguration(config);

    const auto normalized = coordinator.configuration(config.strategyId);
    QVERIFY(normalized.has_value());
    QCOMPARE(normalized->fixedOrderValue, 0.0);
    QCOMPARE(normalized->maxPositionPercent, 0.0);
    QCOMPARE(normalized->cooldownSeconds, 0);
    QCOMPARE(normalized->approvalTimeoutSeconds, 0);
    QCOMPARE(normalized->orderTimeoutSeconds, 0);
    QCOMPARE(normalized->maxOrdersPerDay, 0);
    QCOMPARE(normalized->maxDailyOrderValue, 0.0);
}

void StrategyExecutionTest::reportsConfigurationPersistenceFailureAndSortsConfigurations()
{
    FakeOrderSubmissionPort port;
    FixedAmountPositionSizer sizer;
    DefaultSignalExecutionPolicy policy;
    FakeStrategyExecutionStore store;
    StrategyExecutionCoordinator coordinator(&port, &sizer, &policy, &store);

    StrategyExecutionConfig second;
    second.strategyId = QStringLiteral("strategy-b");
    store.failConfigurationWrites = true;
    QVERIFY(!coordinator.setConfiguration(second));
    QVERIFY(!coordinator.configuration(second.strategyId).has_value());

    store.failConfigurationWrites = false;
    QVERIFY(coordinator.setConfiguration(second));
    StrategyExecutionConfig first;
    first.strategyId = QStringLiteral("strategy-a");
    QVERIFY(coordinator.setConfiguration(first));

    const QVector<StrategyExecutionConfig> configurations = coordinator.configurations();
    QCOMPARE(configurations.size(), 2);
    QCOMPARE(configurations.at(0).strategyId, QStringLiteral("strategy-a"));
    QCOMPARE(configurations.at(1).strategyId, QStringLiteral("strategy-b"));
}

void StrategyExecutionTest::exposesStrategyIntentRolesAndUpdatesRows()
{
    StrategyIntentListModel model;
    QSignalSpy countSpy(&model, &StrategyIntentListModel::countChanged);
    QSignalSpy pendingCountSpy(&model, &StrategyIntentListModel::pendingCountChanged);
    QSignalSpy dataSpy(&model, &StrategyIntentListModel::dataChanged);
    StrategyOrderIntent intent;
    intent.intentId = QStringLiteral("INTENT-1");
    intent.strategyId = QStringLiteral("sma");
    intent.signal.instrument.symbol = QStringLiteral("600519");
    intent.signal.side = SignalSide::Buy;
    intent.signal.price = 100.0;
    intent.signal.reason = QStringLiteral("测试信号");
    intent.orderRequest.side = OrderSide::Buy;
    intent.orderRequest.type = OrderType::Limit;
    intent.orderRequest.timeInForce = TimeInForce::ImmediateOrCancel;
    intent.orderRequest.quantity = 100.0;
    intent.orderRequest.limitPrice = 99.9;
    intent.status = OrderIntentStatus::PendingApproval;
    intent.message = QStringLiteral("等待审批");
    intent.createdAt = QDateTime::currentDateTimeUtc();
    intent.updatedAt = intent.createdAt;
    model.upsertIntent(intent);

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.pendingCount(), 1);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(pendingCountSpy.count(), 1);
    const QModelIndex row = model.index(0);
    QCOMPARE(model.data(row, StrategyIntentListModel::IntentIdRole).toString(),
             QStringLiteral("INTENT-1"));
    QCOMPARE(model.data(row, StrategyIntentListModel::StrategyIdRole).toString(),
             QStringLiteral("sma"));
    QCOMPARE(model.data(row, StrategyIntentListModel::TimeInForceRole).toString(),
             timeInForceName(TimeInForce::ImmediateOrCancel));
    QCOMPARE(model.data(row, StrategyIntentListModel::StatusValueRole).toInt(),
             static_cast<int>(OrderIntentStatus::PendingApproval));
    QCOMPARE(model.roleNames().value(StrategyIntentListModel::StatusValueRole),
             QByteArray("statusValue"));
    QVERIFY(model.data(row, StrategyIntentListModel::PendingRole).toBool());
    QCOMPARE(model.indexOfIntent(QStringLiteral("INTENT-1")), 0);
    QCOMPARE(model.get(0).value(QStringLiteral("intentId")).toString(),
             QStringLiteral("INTENT-1"));
    QVERIFY(model.get(-1).isEmpty());
    QVERIFY(!model.data(QModelIndex(), StrategyIntentListModel::IntentIdRole).isValid());

    intent.status = OrderIntentStatus::Submitted;
    intent.orderId = QStringLiteral("ORDER-1");
    intent.updatedAt = intent.updatedAt.addSecs(1);
    model.upsertIntent(intent);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.pendingCount(), 0);
    QCOMPARE(dataSpy.count(), 1);
    QCOMPARE(pendingCountSpy.count(), 2);
    QVERIFY(!model.data(model.index(0), StrategyIntentListModel::PendingRole).toBool());
    QCOMPARE(model.data(model.index(0), StrategyIntentListModel::OrderIdRole).toString(),
             QStringLiteral("ORDER-1"));

    StrategyOrderIntent older;
    older.intentId = QStringLiteral("INTENT-2");
    older.createdAt = intent.createdAt.addSecs(-10);
    older.updatedAt = older.createdAt;
    older.status = OrderIntentStatus::PendingApproval;
    StrategyOrderIntent replacement = older;
    replacement.updatedAt = intent.createdAt.addSecs(5);
    replacement.status = OrderIntentStatus::Rejected;
    StrategyOrderIntent newest;
    newest.intentId = QStringLiteral("INTENT-3");
    newest.createdAt = intent.createdAt.addSecs(20);
    newest.updatedAt = newest.createdAt;
    newest.status = OrderIntentStatus::PendingApproval;
    StrategyOrderIntent invalid;

    model.loadIntents({older, intent, newest, replacement, invalid});
    QCOMPARE(model.count(), 3);
    QCOMPARE(model.pendingCount(), 1);
    QCOMPARE(model.get(0).value(QStringLiteral("intentId")).toString(),
             QStringLiteral("INTENT-3"));
    QCOMPARE(model.get(1).value(QStringLiteral("intentId")).toString(),
             QStringLiteral("INTENT-1"));
    QCOMPARE(model.get(2).value(QStringLiteral("statusValue")).toInt(),
             static_cast<int>(OrderIntentStatus::Rejected));
    QCOMPARE(model.indexOfIntent(QStringLiteral("missing")), -1);

    model.clear();
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.pendingCount(), 0);
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::StrategyExecutionTest)

#include "tst_strategy_execution.moc"
