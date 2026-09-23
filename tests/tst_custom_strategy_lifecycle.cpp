#include "services/AsyncCustomStrategyWorkspaceService.h"
#include "services/BacktestEngine.h"
#include "services/BacktestDatasetService.h"
#include "services/SqliteLocalDataStore.h"
#include "services/TickDataCsvImporter.h"
#include "strategy/CustomBacktestStrategy.h"
#include "strategy/CustomStrategyCompiler.h"
#include "strategy/CustomStrategyValidator.h"

#include <QFile>
#include <QDir>
#include <QJSEngine>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace pp {

namespace {

Instrument fixtureInstrument()
{
    return {QStringLiteral("600519"), QStringLiteral("合成生命周期测试标的"),
            Market::AShare, QStringLiteral("上交所")};
}

QVector<Candle> syntheticOfflineCandles()
{
    // 明确的合成离线样本，不是 NAS 行情，也不冒充原始 Tick 派生缓存。
    const QVector<double> prices {100.0, 110.0, 120.0, 100.0, 100.0};
    QVector<Candle> candles;
    for (int index = 0; index < prices.size(); ++index) {
        Candle candle;
        candle.instrument = fixtureInstrument();
        candle.startTime = QDateTime(QDate(2026, 8, 17).addDays(index),
                                    QTime(1, 30), Qt::UTC);
        candle.periodSeconds = 86400;
        candle.open = candle.high = candle.low = candle.close = prices.at(index);
        candle.volume = 1000.0;
        candle.source = QStringLiteral("synthetic_custom_strategy_lifecycle_fixture");
        candles.append(candle);
    }
    return candles;
}

BacktestConfig configFor(const CompiledCustomStrategy &plan, const QString &taskId)
{
    BacktestConfig config;
    config.taskId = taskId;
    config.strategyId = plan.strategyId;
    config.strategyVersion = plan.version;
    config.strategyParameters = plan.normalizedParameters;
    config.ruleSnapshot = plan.rules.toVariantMap();
    config.instrument = fixtureInstrument();
    config.frequency = QStringLiteral("day");
    config.periodSeconds = 86400;
    config.requestedBars = 5;
    config.initialCapital = 100000.0;
    config.commissionRate = 0.0;
    config.slippageRate = 0.0;
    config.lotSize = 100;
    return config;
}

} // namespace

class CustomStrategyLifecycleTest final : public QObject {
    Q_OBJECT

private slots:
    void wizardBiasStrategySurvivesRestartAndRunsWithIndependentAccounts();
    void wizardBiasWmaSurvivesRawTickImportRestartArchiveAndAccountReuse();
};

void CustomStrategyLifecycleTest::
    wizardBiasStrategySurvivesRestartAndRunsWithIndependentAccounts()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));
    QJSEngine scriptEngine;
    const QJSValue evaluated = scriptEngine.evaluate(
        QString::fromUtf8(contract.readAll()), contract.fileName());
    QVERIFY2(!evaluated.isError(), qPrintable(evaluated.toString()));
    const QJSValue builder = scriptEngine.globalObject().property(
        QStringLiteral("buildDefinition"));
    QVERIFY(builder.isCallable());

    const auto buildWizardJson = [&scriptEngine, &builder](int version,
                                                          double entryThreshold) {
        const QVariantMap metadata {
            {QStringLiteral("strategyId"), QStringLiteral("custom_bias_lifecycle")},
            {QStringLiteral("version"), version},
            {QStringLiteral("name"), QStringLiteral("乖离率生命周期回归")},
            {QStringLiteral("description"), QStringLiteral("仅验证合成离线数据上的策略行为")},
            {QStringLiteral("author"), QStringLiteral("Qt Test")},
            {QStringLiteral("selectedSymbol"), QStringLiteral("600519")},
            {QStringLiteral("selectedFrequency"), QStringLiteral("day")}
        };
        const QVariantList series {
            QVariantMap{{QStringLiteral("alias"), QStringLiteral("bias")},
                        {QStringLiteral("indicator"), QStringLiteral("BIAS")},
                        {QStringLiteral("period"), 2}}
        };
        const QVariantList entry {
            QVariantMap{{QStringLiteral("op"), QStringLiteral("gt")},
                        {QStringLiteral("left"), QVariantMap{
                             {QStringLiteral("series"), QStringLiteral("bias")}}},
                        {QStringLiteral("right"), QVariantMap{
                             {QStringLiteral("value"), entryThreshold}}}}
        };
        const QVariantList exit {
            QVariantMap{{QStringLiteral("op"), QStringLiteral("lt")},
                        {QStringLiteral("left"), QVariantMap{
                             {QStringLiteral("series"), QStringLiteral("bias")}}},
                        {QStringLiteral("right"), QVariantMap{
                             {QStringLiteral("value"), 0.0}}}}
        };
        const QVariantMap rules {
            {QStringLiteral("position"), QVariantMap{
                 {QStringLiteral("type"), QStringLiteral("fixedPercent")},
                 {QStringLiteral("percent"), 1.0}}},
            {QStringLiteral("risk"), QVariantMap{
                 {QStringLiteral("tPlusOne"), true}}},
            {QStringLiteral("signal"), QVariantMap{
                 {QStringLiteral("priceMode"), QStringLiteral("triggerClose")}}}
        };
        const QJSValue result = builder.call({
            scriptEngine.toScriptValue(metadata), scriptEngine.toScriptValue(series),
            scriptEngine.toScriptValue(entry), scriptEngine.toScriptValue(exit),
            scriptEngine.toScriptValue(rules),
            QJSValue(QStringLiteral("2026-09-07T00:00:00.000Z"))});
        return result;
    };

    const QJSValue versionOneDocument = buildWizardJson(1, 4.0);
    const QJSValue versionTwoDocument = buildWizardJson(2, 50.0);
    QVERIFY2(!versionOneDocument.isError(), qPrintable(versionOneDocument.toString()));
    QVERIFY2(!versionTwoDocument.isError(), qPrintable(versionTwoDocument.toString()));
    const QString versionOneJson = QString::fromUtf8(
        QJsonDocument::fromVariant(versionOneDocument.toVariant())
            .toJson(QJsonDocument::Compact));
    const QString versionTwoJson = QString::fromUtf8(
        QJsonDocument::fromVariant(versionTwoDocument.toVariant())
            .toJson(QJsonDocument::Compact));
    CustomStrategyValidator validator;
    CustomStrategyDefinition versionOne;
    CustomStrategyDefinition versionTwo;
    QString error;
    QVERIFY2(validator.parseAndValidate(versionOneJson, &versionOne, &error),
             qPrintable(error));
    QVERIFY2(validator.parseAndValidate(versionTwoJson, &versionTwo, &error),
             qPrintable(error));
    QCOMPARE(versionOne.version, 1);
    QCOMPARE(versionTwo.version, 2);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("lifecycle.db"));
    {
        SqliteLocalDataStore fixtureStore;
        QVERIFY(fixtureStore.initialize(databasePath));
        QVERIFY(fixtureStore.upsertCandles(syntheticOfflineCandles()));
    }
    {
        AsyncCustomStrategyWorkspaceService workspace;
        QVERIFY(workspace.start(databasePath));
        QSignalSpy saved(&workspace, &AsyncCustomStrategyWorkspaceService::saveFinished);
        QVERIFY(workspace.requestSave(41, versionOneJson));
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 5000);
        const QList<QVariant> completion = saved.takeFirst();
        QCOMPARE(completion.at(0).toULongLong(), quint64(41));
        const QVariantMap result = completion.at(1).toMap();
        QVERIFY2(result.value(QStringLiteral("success")).toBool(),
                 qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("version")).toInt(), 1);
        workspace.stop();
    }
    {
        SqliteLocalDataStore reopened;
        QVERIFY(reopened.initialize(databasePath));
        const QVector<CustomStrategyDefinition> restored = reopened.loadCustomStrategies();
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored.first().canonicalJson(), versionOne.canonicalJson());
        QCOMPARE(restored.first().strategyId, versionOne.strategyId);
        QCOMPARE(restored.first().version, 1);
    }
    {
        AsyncCustomStrategyWorkspaceService restartedWorkspace;
        QVERIFY(restartedWorkspace.start(databasePath));
        QSignalSpy saved(&restartedWorkspace,
                        &AsyncCustomStrategyWorkspaceService::saveFinished);
        QVERIFY(restartedWorkspace.requestSave(42, versionTwoJson));
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 5000);
        const QList<QVariant> completion = saved.takeFirst();
        QCOMPARE(completion.at(0).toULongLong(), quint64(42));
        const QVariantMap result = completion.at(1).toMap();
        QVERIFY2(result.value(QStringLiteral("success")).toBool(),
                 qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("version")).toInt(), 2);
        restartedWorkspace.stop();
    }

    QVector<CustomStrategyDefinition> restored;
    QVector<Candle> candles;
    {
        SqliteLocalDataStore reopened;
        QVERIFY(reopened.initialize(databasePath));
        restored = reopened.loadCustomStrategies();
        candles = reopened.loadRecentCandles(fixtureInstrument(), 86400, 5);
    }
    QCOMPARE(restored.size(), 2);
    QCOMPARE(restored.at(0).canonicalJson(), versionTwo.canonicalJson());
    QCOMPARE(restored.at(1).canonicalJson(), versionOne.canonicalJson());
    QCOMPARE(candles.size(), 5);
    QCOMPARE(candles.first().source,
             QStringLiteral("synthetic_custom_strategy_lifecycle_fixture"));

    CustomStrategyCompiler compiler;
    const auto activePlan = compiler.compile(restored.at(1), {}, &error);
    QVERIFY2(activePlan.has_value(), qPrintable(error));
    const auto inactivePlan = compiler.compile(restored.at(0), {}, &error);
    QVERIFY2(inactivePlan.has_value(), qPrintable(error));

    BacktestEngine activeRun;
    BacktestEngine inactiveRun;
    QSignalSpy activeFinished(&activeRun, &BacktestEngine::finished);
    QSignalSpy inactiveFinished(&inactiveRun, &BacktestEngine::finished);
    QSignalSpy activeErrors(&activeRun, &BacktestEngine::errorOccurred);
    QSignalSpy inactiveErrors(&inactiveRun, &BacktestEngine::errorOccurred);
    QVERIFY(activeRun.start(configFor(*activePlan, QStringLiteral("bias-active")), candles,
                            std::make_unique<CustomBacktestStrategy>(*activePlan)));
    QVERIFY(inactiveRun.start(configFor(*inactivePlan, QStringLiteral("bias-inactive")),
                              candles,
                              std::make_unique<CustomBacktestStrategy>(*inactivePlan)));
    QTRY_COMPARE_WITH_TIMEOUT(activeFinished.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(inactiveFinished.count(), 1, 5000);
    QCOMPARE(activeErrors.count(), 0);
    QCOMPARE(inactiveErrors.count(), 0);

    // 手算基线：第二日 BIAS=4.7619% 买入 900 股，第四日负乖离卖出；
    // 110 买、100 卖，零费用时最终资金 91,000。阈值 50% 的独立账户不交易。
    const BacktestResult first = activeRun.result();
    QCOMPARE(first.config.strategyVersion, 1);
    QCOMPARE(first.summary.initialCapital, 100000.0);
    QCOMPARE(first.trades.size(), 1);
    QCOMPARE(first.trades.first().entryTime, candles.at(1).startTime);
    QCOMPARE(first.trades.first().exitTime, candles.at(3).startTime);
    QCOMPARE(first.trades.first().entryPrice, 110.0);
    QCOMPARE(first.trades.first().exitPrice, 100.0);
    QCOMPARE(first.trades.first().quantity, 900.0);
    QCOMPARE(first.trades.first().netPnl, -9000.0);
    QCOMPARE(first.summary.finalEquity, 91000.0);
    QCOMPARE(first.finalPosition.quantity, 0.0);
    QVERIFY(!first.equityCurve.isEmpty());
    QCOMPARE(first.equityCurve.first().equity, 100000.0);
    QCOMPARE(first.equityCurve.last().cash, 91000.0);
    const BacktestResult inactive = inactiveRun.result();
    QCOMPARE(inactive.config.strategyVersion, 2);
    QCOMPARE(inactive.summary.initialCapital, 100000.0);
    QCOMPARE(inactive.trades.size(), 0);
    QCOMPARE(inactive.summary.finalEquity, 100000.0);
    QCOMPARE(inactive.finalPosition.quantity, 0.0);

    BacktestEngine repeatedRun;
    QSignalSpy repeatedFinished(&repeatedRun, &BacktestEngine::finished);
    QVERIFY(repeatedRun.start(configFor(*activePlan, QStringLiteral("bias-repeat")), candles,
                              std::make_unique<CustomBacktestStrategy>(*activePlan)));
    QTRY_COMPARE_WITH_TIMEOUT(repeatedFinished.count(), 1, 5000);
    const BacktestResult repeated = repeatedRun.result();
    QCOMPARE(repeated.config.dataVersion, first.config.dataVersion);
    QCOMPARE(repeated.summary.initialCapital, 100000.0);
    QCOMPARE(repeated.summary.finalEquity, 91000.0);
    QCOMPARE(repeated.trades.size(), 1);
    QCOMPARE(repeated.trades.first().quantity, 900.0);
    QCOMPARE(repeated.equityCurve.size(), first.equityCurve.size());
    for (int index = 0; index < first.equityCurve.size(); ++index) {
        QCOMPARE(repeated.equityCurve.at(index).timestamp,
                 first.equityCurve.at(index).timestamp);
        QCOMPARE(repeated.equityCurve.at(index).equity, first.equityCurve.at(index).equity);
        QCOMPARE(repeated.equityCurve.at(index).cash, first.equityCurve.at(index).cash);
    }
    QCOMPARE(inactiveRun.result().summary.finalEquity, 100000.0);
}

void CustomStrategyLifecycleTest::
    wizardBiasWmaSurvivesRawTickImportRestartArchiveAndAccountReuse()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("synthetic-bias-wma.db"));
    const QString syntheticSource = QStringLiteral("synthetic_wizard_fixture_v1");
    const QVector<double> prices {100.0, 104.0, 108.0, 102.0, 102.0};
    const QDate firstDate(2026, 8, 17);

    // 这是合成成交样本，不是市场历史。使用真实解析器及事务审计，
    // 仅显式标注原始来源；不手写派生 Candle、缓存标签或内容指纹。
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(databasePath));
        for (int index = 0; index < prices.size(); ++index) {
            const QDate date = firstDate.addDays(index);
            QVERIFY(QDir(directory.path()).mkpath(date.toString(QStringLiteral("yyyyMMdd"))));
            TickDataImportRequest request;
            request.rootDirectory = directory.path();
            request.instrument = fixtureInstrument();
            request.tradingDate = date;
            request.format = TickDataCsvFormat::Tick;
            request.periodSeconds = 60;
            QFile file(TickDataCsvImporter::expectedFilePath(request));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            const QByteArray csv = QStringLiteral(
                "time,price,volume,amount,type\n"
                "09:30:00,%1,10,%2,M\n"
                "14:59:00,%1,10,%2,M\n")
                .arg(prices.at(index), 0, 'f', 2)
                .arg(prices.at(index) * 1000.0, 0, 'f', 2).toUtf8();
            QCOMPARE(file.write(csv), qint64(csv.size()));
            file.close();
            QVERIFY(store.beginRawTradeTickImport(fixtureInstrument(), date, syntheticSource));
            const auto imported = TickDataCsvImporter::parse(request, {},
                [&store, &syntheticSource](const QVector<RawTradeTick> &parsed) {
                    QVector<RawTradeTick> marked = parsed;
                    for (auto &tick : marked) tick.source = syntheticSource;
                    return store.appendRawTradeTicks(marked);
                });
            QVERIFY(imported.succeeded);
            QCOMPARE(imported.validRecords, qint64(2));
            QCOMPARE(imported.skippedRecords, qint64(0));
            QCOMPARE(imported.outOfOrderRecords, qint64(0));
            // 稀疏合成样本诚实保留覆盖警告，不伪造 healthy 或停牌事实。
            QCOMPARE(imported.qualityStatus, QStringLiteral("warning"));
            QVERIFY(store.commitRawTradeTickImport(imported));
        }
        const auto audits = store.loadRawTickImportDayAudits(
            fixtureInstrument(), firstDate, firstDate.addDays(4));
        QCOMPARE(audits.size(), 5);
        for (const auto &audit : audits) {
            QCOMPARE(audit.source, syntheticSource);
            QCOMPARE(audit.validRecords, qint64(2));
            QCOMPARE(audit.skippedRecords, qint64(0));
            QVERIFY(audit.rawContentSha256.startsWith(QStringLiteral("sha256:")));
        }
        QVector<RawTradeTick> restoredTicks;
        QVERIFY(store.forEachRawTradeTick(fixtureInstrument(), firstDate,
            [&restoredTicks](const RawTradeTick &tick) {
                restoredTicks.append(tick);
                return true;
            }));
        QCOMPARE(restoredTicks.size(), 2);
        QCOMPARE(restoredTicks.first().source, syntheticSource);
        QCOMPARE(restoredTicks.first().volumeShares, 1000.0);
    }

    BacktestDatasetIdentity identity;
    QVector<Candle> candles;
    {
        BacktestDatasetService datasets;
        QVERIFY(datasets.start(databasePath));
        QSignalSpy prepared(&datasets, &BacktestDatasetService::prepared);
        QSignalSpy failed(&datasets, &BacktestDatasetService::failed);
        QVERIFY(datasets.request(71, fixtureInstrument().symbol, 86400, 5,
                                 {fixtureInstrument()}));
        QTRY_VERIFY_WITH_TIMEOUT(prepared.count() + failed.count() > 0, 5000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(prepared.count(), 1);
        QCOMPARE(prepared.first().at(0).toULongLong(), quint64(71));
        identity = qvariant_cast<BacktestDatasetIdentity>(prepared.first().at(1));
        candles = qvariant_cast<QVector<Candle>>(prepared.first().at(2));
        datasets.stop();
    }
    QVERIFY(!identity.isEmpty());
    QCOMPARE(identity.rawTickTotal(), 10);
    QCOMPARE(identity.derivedBarTotal(), 5);
    QCOMPARE(identity.days.size(), 5);
    QCOMPARE(identity.gapDayCount, 0);
    QCOMPARE(identity.staleDayCount, 0);
    QVERIFY(identity.datasetVersion.startsWith(QStringLiteral("sha256:")));
    QCOMPARE(candles.size(), 5);
    for (int index = 0; index < candles.size(); ++index) {
        QCOMPARE(candles.at(index).source, QStringLiteral("raw_tick_cache_v1"));
        QCOMPARE(candles.at(index).startTime,
                 QDateTime(firstDate.addDays(index), QTime(1, 25), Qt::UTC));
        QCOMPARE(candles.at(index).close, prices.at(index));
        QCOMPARE(candles.at(index).volume, 2000.0);
    }

    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY(contract.open(QIODevice::ReadOnly | QIODevice::Text));
    QJSEngine scriptEngine;
    const auto evaluated = scriptEngine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluated.isError(), qPrintable(evaluated.toString()));
    const QJSValue builder = scriptEngine.globalObject().property(QStringLiteral("buildDefinition"));
    QVERIFY(builder.isCallable());
    const auto wizardDocument = [&scriptEngine, &builder](int version, double threshold,
                                                         bool reverseWmaEntry) {
        const QVariantMap metadata {
            {QStringLiteral("strategyId"), QStringLiteral("custom_synthetic_bias_wma")},
            {QStringLiteral("version"), version},
            {QStringLiteral("name"), QStringLiteral("合成数据验收·BIAS+WMA")},
            {QStringLiteral("description"), QStringLiteral("固定合成样本验证成交与账户隔离，不用于收益择优")},
            {QStringLiteral("author"), QStringLiteral("Qt Test")},
            {QStringLiteral("selectedSymbol"), QStringLiteral("600519")},
            {QStringLiteral("selectedFrequency"), QStringLiteral("day")}
        };
        const QVariantList series {
            QVariantMap{{QStringLiteral("alias"), QStringLiteral("bias")},
                        {QStringLiteral("indicator"), QStringLiteral("BIAS")},
                        {QStringLiteral("period"), 2}},
            QVariantMap{{QStringLiteral("alias"), QStringLiteral("wma")},
                        {QStringLiteral("indicator"), QStringLiteral("WMA")},
                        {QStringLiteral("period"), 2}}
        };
        const QVariantMap bias {{QStringLiteral("series"), QStringLiteral("bias")}};
        const QVariantMap wma {{QStringLiteral("series"), QStringLiteral("wma")}};
        const QVariantMap close {{QStringLiteral("field"), QStringLiteral("close")}};
        const QVariantList entry {
            QVariantMap{{QStringLiteral("op"), QStringLiteral("gt")},
                        {QStringLiteral("left"), bias},
                        {QStringLiteral("right"), QVariantMap{{QStringLiteral("value"), threshold}}}},
            QVariantMap{{QStringLiteral("op"), reverseWmaEntry ? QStringLiteral("lt") : QStringLiteral("gt")},
                        {QStringLiteral("left"), close}, {QStringLiteral("right"), wma}}
        };
        const QVariantList exit {
            QVariantMap{{QStringLiteral("op"), QStringLiteral("lt")},
                        {QStringLiteral("left"), bias},
                        {QStringLiteral("right"), QVariantMap{{QStringLiteral("value"), 0.0}}}},
            QVariantMap{{QStringLiteral("op"), QStringLiteral("lt")},
                        {QStringLiteral("left"), close}, {QStringLiteral("right"), wma}}
        };
        const QVariantMap rules {
            {QStringLiteral("position"), QVariantMap{{QStringLiteral("type"), QStringLiteral("fixedPercent")},
                                                     {QStringLiteral("percent"), 1.0}}},
            {QStringLiteral("risk"), QVariantMap{{QStringLiteral("tPlusOne"), true}}},
            {QStringLiteral("signal"), QVariantMap{{QStringLiteral("priceMode"), QStringLiteral("triggerClose")}}}
        };
        return builder.call({scriptEngine.toScriptValue(metadata), scriptEngine.toScriptValue(series),
                             scriptEngine.toScriptValue(entry), scriptEngine.toScriptValue(exit),
                             scriptEngine.toScriptValue(rules),
                             QJSValue(QStringLiteral("2026-09-07T00:00:00.000Z"))});
    };

    const QVector<double> expectedEquity {100000.0, 100000.0, 103600.0, 98200.0, 98200.0};
    const QVector<double> expectedCash {100000.0, 6400.0, 6400.0, 98200.0, 98200.0};
    CustomStrategyDefinition originalVersion;
    BacktestResult firstResult;
    BacktestEngine engine; // 模拟 Controller 复用引擎，但每次运行须重新创建账户。
    // v2 仅抬高 BIAS 阈值；v3 恢复 BIAS > 1，仅反转 WMA 入场比较。
    // 两个单变量对照均须阻止成交，最后重跑 v1 验证账户及不可变版本隔离。
    for (int run = 0; run < 4; ++run) {
        const int version = run < 3 ? run + 1 : 1;
        const bool activeVersion = version == 1;
        CustomStrategyDefinition expectedDefinition;
        if (run < 3) {
            const QJSValue document = wizardDocument(version, version == 2 ? 50.0 : 1.0,
                                                      version == 3);
            QVERIFY2(!document.isError(), qPrintable(document.toString()));
            const QString json = QString::fromUtf8(QJsonDocument::fromVariant(document.toVariant())
                                                       .toJson(QJsonDocument::Compact));
            QString validationError;
            QVERIFY2(CustomStrategyValidator().parseAndValidate(json, &expectedDefinition, &validationError),
                     qPrintable(validationError));
            AsyncCustomStrategyWorkspaceService workspace;
            QVERIFY(workspace.start(databasePath));
            QSignalSpy saved(&workspace, &AsyncCustomStrategyWorkspaceService::saveFinished);
            QVERIFY(workspace.requestSave(81 + run, json));
            QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 5000);
            QCOMPARE(saved.first().at(0).toULongLong(), quint64(81 + run));
            const auto completion = saved.first().at(1).toMap();
            QVERIFY2(completion.value(QStringLiteral("success")).toBool(),
                     qPrintable(completion.value(QStringLiteral("error")).toString()));
            QCOMPARE(completion.value(QStringLiteral("version")).toInt(), version);
            workspace.stop();
        }

        CustomStrategyDefinition restored;
        {
            SqliteLocalDataStore reopened;
            QVERIFY(reopened.initialize(databasePath));
            const auto definitions = reopened.loadCustomStrategies();
            QCOMPARE(definitions.size(), run < 3 ? run + 1 : 3);
            for (const auto &definition : definitions) {
                if (definition.version == version) restored = definition;
            }
            const auto reloadedIdentity = reopened.loadBacktestDatasetIdentity(fixtureInstrument(), 86400);
            QVERIFY(reloadedIdentity.has_value());
            QCOMPARE(reloadedIdentity->datasetVersion, identity.datasetVersion);
            candles = reopened.loadRecentCandlesFromSource(
                fixtureInstrument(), 86400, 5, QStringLiteral("raw_tick_cache_v1"));
            bool selectable = false;
            for (const auto &dataset : reopened.localHistoryCatalog()) {
                if (dataset.instrument.symbol == fixtureInstrument().symbol
                    && dataset.periodSeconds == 86400 && dataset.candleCount == 5
                    && dataset.isBacktestEligible()) selectable = true;
            }
            QVERIFY(selectable);
        }
        QCOMPARE(restored.strategyId, QStringLiteral("custom_synthetic_bias_wma"));
        QCOMPARE(restored.version, version);
        if (run < 3) QCOMPARE(restored.canonicalJson(), expectedDefinition.canonicalJson());
        if (run == 0) originalVersion = restored;
        if (run == 3) QCOMPARE(restored.canonicalJson(), originalVersion.canonicalJson());
        QString error;
        const auto plan = CustomStrategyCompiler().compile(restored, {}, &error);
        QVERIFY2(plan.has_value(), qPrintable(error));
        QCOMPARE(plan->indicators.size(), 2);
        BacktestConfig config = configFor(*plan, QStringLiteral("synthetic-bias-wma-%1").arg(run + 1));
        config.datasetIdentity = identity;
        QSignalSpy finished(&engine, &BacktestEngine::finished);
        QSignalSpy errors(&engine, &BacktestEngine::errorOccurred);
        QVERIFY(engine.start(config, candles, std::make_unique<CustomBacktestStrategy>(*plan)));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(errors.count(), 0);
        const BacktestResult result = engine.result();
        QCOMPARE(result.config.strategyVersion, version);
        QCOMPARE(result.config.datasetIdentity.datasetVersion, identity.datasetVersion);
        QCOMPARE(result.summary.initialCapital, 100000.0);
        QCOMPARE(result.summary.totalCommission, 0.0);
        QCOMPARE(result.finalPosition.quantity, 0.0);
        QCOMPARE(result.equityCurve.size(), 5);
        QCOMPARE(result.trades.size(), activeVersion ? 1 : 0);
        for (int index = 0; index < 5; ++index) {
            QCOMPARE(result.equityCurve.at(index).equity, activeVersion ? expectedEquity.at(index) : 100000.0);
            QCOMPARE(result.equityCurve.at(index).cash, activeVersion ? expectedCash.at(index) : 100000.0);
        }
        if (activeVersion) {
            const auto &trade = result.trades.first();
            QCOMPARE(trade.entryTime, QDateTime(QDate(2026, 8, 18), QTime(1, 25), Qt::UTC));
            QCOMPARE(trade.exitTime, QDateTime(QDate(2026, 8, 20), QTime(1, 25), Qt::UTC));
            QCOMPARE(trade.entryPrice, 104.0);
            QCOMPARE(trade.exitPrice, 102.0);
            QCOMPARE(trade.quantity, 900.0);
            QCOMPARE(trade.netPnl, -1800.0);
            QVERIFY(trade.exitReason.contains(QStringLiteral("退出条件成立")));
            QVERIFY(!trade.exitReason.contains(QStringLiteral("回测结束平仓")));
        }
        QCOMPARE(result.summary.finalEquity, activeVersion ? 98200.0 : 100000.0);
        if (run == 0) firstResult = result;
        else QCOMPARE(result.config.dataVersion, firstResult.config.dataVersion);

        BacktestArchiveRecord archive;
        archive.resultId = QStringLiteral("synthetic-bias-wma-archive-%1").arg(run + 1);
        archive.completedAt = QDateTime(QDate(2026, 9, 7), QTime(8, 0), Qt::UTC).addSecs(run);
        archive.result = result;
        {
            SqliteLocalDataStore store;
            QVERIFY(store.initialize(databasePath));
            QVERIFY(store.saveBacktestArchive(archive));
        }
        {
            SqliteLocalDataStore reopened;
            QVERIFY(reopened.initialize(databasePath));
            const auto loaded = reopened.loadBacktestArchive(archive.resultId);
            QVERIFY(loaded.has_value());
            QCOMPARE(loaded->result.config.strategyVersion, version);
            QCOMPARE(loaded->result.config.dataVersion, result.config.dataVersion);
            QCOMPARE(loaded->result.config.datasetIdentity.datasetVersion, identity.datasetVersion);
            QCOMPARE(loaded->result.summary.finalEquity, activeVersion ? 98200.0 : 100000.0);
            QCOMPARE(loaded->result.trades.size(), activeVersion ? 1 : 0);
            QCOMPARE(reopened.loadBacktestArchiveSummaries().size(), run + 1);
            if (run == 3) {
                for (const int inactiveVersion : {2, 3}) {
                    const auto inactive = reopened.loadBacktestArchive(
                        QStringLiteral("synthetic-bias-wma-archive-%1").arg(inactiveVersion));
                    QVERIFY(inactive.has_value());
                    QCOMPARE(inactive->result.config.strategyVersion, inactiveVersion);
                    QCOMPARE(inactive->result.summary.finalEquity, 100000.0);
                    QVERIFY(inactive->result.trades.isEmpty());
                }
            }
        }
    }
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::CustomStrategyLifecycleTest)
#include "tst_custom_strategy_lifecycle.moc"
