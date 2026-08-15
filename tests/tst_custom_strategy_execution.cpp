#include "strategy/CustomStrategyValidator.h"
#include "strategy/CustomStrategyCompiler.h"
#include "strategy/CustomExecutionStrategy.h"
#include "strategy/CustomStrategyRuntimeManager.h"
#include "strategy/StrategyEngine.h"
#include "strategy/StrategyExecutionCoordinator.h"
#include "strategy/DefaultSignalExecutionPolicy.h"
#include "strategy/FixedAmountPositionSizer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

namespace pp {

class FakeOrderSubmissionPort final : public OrderSubmissionPort {
public:
    QString submitOrder(OrderRequest request) override
    {
        requests.append(request);
        return QStringLiteral("ORDER-%1").arg(requests.size());
    }

    AccountSnapshot snapshot() const override { return account; }
    QVector<Position> positions() const override { return currentPositions; }

    AccountSnapshot account;
    QVector<Position> currentPositions;
    QVector<OrderRequest> requests;
};

namespace {

// 单标的、mock_execution 的确定性 DSL：close 上穿/下穿固定阈值。
QJsonObject executionDocument()
{
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("strategyId"), QStringLiteral("custom_exec_close")},
            {QStringLiteral("version"), 1},
            {QStringLiteral("name"), QStringLiteral("自定义阈值执行")},
            {QStringLiteral("description"), QStringLiteral("mock_execution 示例")},
            {QStringLiteral("author"), QStringLiteral("Qt Test")},
            {QStringLiteral("market"), QStringLiteral("A股")},
            {QStringLiteral("executionScope"), QStringLiteral("mock_execution")},
            {QStringLiteral("createdAt"), QStringLiteral("2026-08-15T00:00:00.000Z")},
            {QStringLiteral("updatedAt"), QStringLiteral("2026-08-15T00:00:00.000Z")},
            {QStringLiteral("parameters"), QJsonArray{}},
            {QStringLiteral("entry"), QJsonObject{
                {QStringLiteral("op"), QStringLiteral("gt")},
                {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}},
                {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 100.0}}}}},
            {QStringLiteral("exit"), QJsonObject{
                {QStringLiteral("op"), QStringLiteral("lt")},
                {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}},
                {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 90.0}}}}}};
}

Instrument makeInstrument()
{
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("贵州茅台");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");
    return instrument;
}

Candle makeCandle(const Instrument &instrument, double close, int dayOffset)
{
    Candle candle;
    candle.instrument = instrument;
    candle.startTime = QDateTime(QDate(2026, 8, 17), QTime(9, 30), Qt::UTC).addDays(dayOffset);
    candle.periodSeconds = 60;
    candle.open = candle.high = candle.low = candle.close = close;
    candle.volume = 1000.0;
    return candle;
}

std::optional<CompiledCustomStrategy> compilePlan(QString *error)
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    if (!validator.validate(executionDocument(), &definition, error)) return std::nullopt;
    CustomStrategyCompiler compiler;
    return compiler.compile(definition, {}, error);
}

} // namespace

class CustomStrategyExecutionTest : public QObject {
    Q_OBJECT

private slots:
    void validatorAcceptsMockExecutionScope();
    void executionStrategySubscribesInstrumentSymbol();
    void executionStrategyEmitsEntryAndExit();
    void strategyEngineManagesExecutionStrategy();
    void coordinatorProcessesExecutionSignal();
    void runtimeManagerManagesPluginLifecycle();
};

void CustomStrategyExecutionTest::validatorAcceptsMockExecutionScope()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY(validator.validate(executionDocument(), &definition, &error));
    QCOMPARE(definition.executionScope, QStringLiteral("mock_execution"));

    QJsonObject backtestOnly = executionDocument();
    backtestOnly.insert(QStringLiteral("executionScope"), QStringLiteral("backtest_only"));
    QVERIFY(validator.validate(backtestOnly, nullptr, &error));

    QJsonObject unsafe = executionDocument();
    unsafe.insert(QStringLiteral("executionScope"), QStringLiteral("automatic_trading"));
    QVERIFY(!validator.validate(unsafe, nullptr, &error));
}

void CustomStrategyExecutionTest::executionStrategySubscribesInstrumentSymbol()
{
    QString error;
    const auto plan = compilePlan(&error);
    QVERIFY2(plan.has_value(), qPrintable(error));

    CustomExecutionStrategy strategy(*plan, makeInstrument());
    QCOMPARE(strategy.id(), QStringLiteral("custom_exec_close"));
    QCOMPARE(strategy.subscribedSymbols(), QStringList{QStringLiteral("600519")});
    QVERIFY(strategy.displayName().contains(QStringLiteral("自定义阈值执行")));
}

void CustomStrategyExecutionTest::executionStrategyEmitsEntryAndExit()
{
    QString error;
    const auto plan = compilePlan(&error);
    QVERIFY2(plan.has_value(), qPrintable(error));

    const Instrument instrument = makeInstrument();
    CustomExecutionStrategy strategy(*plan, instrument);
    qRegisterMetaType<TradeSignal>();
    QSignalSpy signalSpy(&strategy, &Strategy::signalGenerated);

    const QVector<double> closes {50.0, 90.0, 110.0, 120.0, 85.0, 80.0};
    for (int index = 0; index < closes.size(); ++index) {
        strategy.onCandle(makeCandle(instrument, closes.at(index), index));
    }

    QCOMPARE(signalSpy.count(), 2);
    const TradeSignal buy = signalSpy.at(0).at(0).value<TradeSignal>();
    const TradeSignal sell = signalSpy.at(1).at(0).value<TradeSignal>();
    QCOMPARE(buy.side, SignalSide::Buy);
    QCOMPARE(sell.side, SignalSide::Sell);
    QCOMPARE(buy.price, 110.0);
    QCOMPARE(sell.price, 85.0);
}

void CustomStrategyExecutionTest::strategyEngineManagesExecutionStrategy()
{
    QString error;
    const auto plan = compilePlan(&error);
    QVERIFY2(plan.has_value(), qPrintable(error));

    StrategyEngine engine;
    QCOMPARE(engine.strategyCount(), 0);

    auto *strategy = new CustomExecutionStrategy(*plan, makeInstrument());
    engine.addStrategy(strategy);
    QCOMPARE(engine.strategyCount(), 1);
    QVERIFY(engine.strategyById(QStringLiteral("custom_exec_close")) != nullptr);
    QVERIFY(engine.isStrategyEnabled(QStringLiteral("custom_exec_close")));

    QVERIFY(engine.removeStrategy(QStringLiteral("custom_exec_close")));
    QCOMPARE(engine.strategyCount(), 0);
    QVERIFY(engine.strategyById(QStringLiteral("custom_exec_close")) == nullptr);
    QVERIFY(!engine.removeStrategy(QStringLiteral("missing")));
}

void CustomStrategyExecutionTest::coordinatorProcessesExecutionSignal()
{
    QString error;
    const auto plan = compilePlan(&error);
    QVERIFY2(plan.has_value(), qPrintable(error));

    FakeOrderSubmissionPort port;
    port.account.initialEquity = 1000000.0;
    port.account.cash = 1000000.0;
    port.account.totalEquity = 1000000.0;
    FixedAmountPositionSizer sizer;
    DefaultSignalExecutionPolicy policy;
    StrategyExecutionCoordinator coordinator(&port, &sizer, &policy);

    StrategyExecutionConfig config;
    config.strategyId = QStringLiteral("custom_exec_close");
    config.enabled = true;
    config.mode = StrategyExecutionMode::Automatic;
    config.fixedOrderValue = 100000.0;
    config.maxPositionPercent = 20.0;
    config.cooldownSeconds = 0;
    QVERIFY(coordinator.setConfiguration(config));

    const Instrument instrument = makeInstrument();
    CustomExecutionStrategy strategy(*plan, instrument);
    qRegisterMetaType<TradeSignal>();
    QSignalSpy signalSpy(&strategy, &Strategy::signalGenerated);

    // 仅推进到首个入场信号（close 突破 100），避免卖出无持仓的复杂性。
    const QVector<double> closes {50.0, 90.0, 110.0};
    for (int index = 0; index < closes.size(); ++index) {
        strategy.onCandle(makeCandle(instrument, closes.at(index), index));
    }
    QCOMPARE(signalSpy.count(), 1);

    const TradeSignal signal = signalSpy.at(0).at(0).value<TradeSignal>();
    const QString intentId = coordinator.processSignal(config.strategyId, signal);
    const auto intent = coordinator.intent(intentId);
    QVERIFY(intent.has_value());
    QVERIFY(intent->status == OrderIntentStatus::Submitted);
    QCOMPARE(port.requests.size(), 1);
    QVERIFY(port.requests.first().side == OrderSide::Buy);
    QVERIFY(port.requests.first().quantity > 0.0);
}

void CustomStrategyExecutionTest::runtimeManagerManagesPluginLifecycle()
{
    StrategyEngine engine;
    CustomStrategyRuntimeManager manager(&engine);

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY(validator.validate(executionDocument(), &definition, &error));

    const Instrument instrument = makeInstrument();
    QVERIFY(manager.load(definition, instrument, &error));
    QCOMPARE(manager.state(QStringLiteral("custom_exec_close")),
             CustomStrategyRuntimeState::Loaded);
    QCOMPARE(engine.strategyCount(), 0); // 加载但未激活

    QVERIFY(manager.activate(QStringLiteral("custom_exec_close")));
    QCOMPARE(manager.state(QStringLiteral("custom_exec_close")),
             CustomStrategyRuntimeState::Active);
    QCOMPARE(engine.strategyCount(), 1);

    QVERIFY(manager.deactivate(QStringLiteral("custom_exec_close")));
    QCOMPARE(manager.state(QStringLiteral("custom_exec_close")),
             CustomStrategyRuntimeState::Loaded);
    QCOMPARE(engine.strategyCount(), 0); // 已退出但保留编译产物

    QVERIFY(manager.activate(QStringLiteral("custom_exec_close")));
    QCOMPARE(engine.strategyCount(), 1);

    QVERIFY(manager.unload(QStringLiteral("custom_exec_close")));
    QCOMPARE(manager.state(QStringLiteral("custom_exec_close")),
             CustomStrategyRuntimeState::Unloaded);
    QCOMPARE(engine.strategyCount(), 0);
    QVERIFY(manager.plugin(QStringLiteral("custom_exec_close")) == nullptr);

    // load 幂等：重复加载不重复计数。
    QVERIFY(manager.load(definition, instrument, &error));
    QVERIFY(manager.load(definition, instrument, &error));
    QCOMPARE(manager.plugins().size(), 1);
    QVERIFY(manager.plugins().first().toVariantMap().value(QStringLiteral("strategyId")).toString()
            == QStringLiteral("custom_exec_close"));
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::CustomStrategyExecutionTest)

#include "tst_custom_strategy_execution.moc"
