#include "services/BacktestDataAvailabilityPlanner.h"
#include "services/BacktestDataQualityGate.h"
#include "services/BacktestEngine.h"
#include "services/SqliteLocalDataStore.h"
#include "strategy/CustomBacktestStrategy.h"
#include "strategy/CustomStrategyCompiler.h"
#include "strategy/CustomStrategyValidator.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QTimeZone>

#include <cmath>
#include <limits>
#include <memory>

namespace {

using namespace pp;

void fail(const QString &message)
{
    QTextStream(stderr) << message << Qt::endl;
}

QJsonObject tradeJson(const BacktestTrade &trade)
{
    return {{QStringLiteral("entryTime"), trade.entryTime.toString(Qt::ISODateWithMs)},
            {QStringLiteral("exitTime"), trade.exitTime.toString(Qt::ISODateWithMs)},
            {QStringLiteral("entryPrice"), trade.entryPrice},
            {QStringLiteral("exitPrice"), trade.exitPrice},
            {QStringLiteral("quantity"), trade.quantity},
            {QStringLiteral("grossPnl"), trade.grossPnl},
            {QStringLiteral("netPnl"), trade.netPnl},
            {QStringLiteral("commission"), trade.commission},
            {QStringLiteral("entryReason"), trade.entryReason},
            {QStringLiteral("exitReason"), trade.exitReason}};
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {QStringLiteral("db"), QStringLiteral("只读本地 Tick SQLite 路径"),
         QStringLiteral("path")},
        {QStringLiteral("definition"), QStringLiteral("已冻结的自定义策略 JSON 路径"),
         QStringLiteral("path")},
        {QStringLiteral("symbol"), QStringLiteral("六位 A 股代码"),
         QStringLiteral("code")},
        {QStringLiteral("from"), QStringLiteral("起始交易日 yyyy-MM-dd"),
         QStringLiteral("date")},
        {QStringLiteral("to"), QStringLiteral("结束交易日 yyyy-MM-dd"),
         QStringLiteral("date")},
        {QStringLiteral("output"), QStringLiteral("轻量结果 JSON 路径"),
         QStringLiteral("path")},
        {QStringLiteral("capital"), QStringLiteral("初始资金（元）"),
         QStringLiteral("amount"), QStringLiteral("1000000")},
        {QStringLiteral("commission"), QStringLiteral("双边佣金率"),
         QStringLiteral("rate"), QStringLiteral("0.0003")},
        {QStringLiteral("slippage"), QStringLiteral("双边滑点率"),
         QStringLiteral("rate"), QStringLiteral("0.0002")}
    });
    parser.process(application);

    const QString dbPath = parser.value(QStringLiteral("db"));
    const QString definitionPath = parser.value(QStringLiteral("definition"));
    const QString symbol = parser.value(QStringLiteral("symbol")).trimmed();
    const QString outputPath = parser.value(QStringLiteral("output"));
    const QDate firstDate = QDate::fromString(parser.value(QStringLiteral("from")), Qt::ISODate);
    const QDate lastDate = QDate::fromString(parser.value(QStringLiteral("to")), Qt::ISODate);
    bool capitalOk = false;
    bool commissionOk = false;
    bool slippageOk = false;
    const double capital = parser.value(QStringLiteral("capital")).toDouble(&capitalOk);
    const double commission = parser.value(QStringLiteral("commission")).toDouble(&commissionOk);
    const double slippage = parser.value(QStringLiteral("slippage")).toDouble(&slippageOk);
    const QFileInfo dbInfo(dbPath);
    const QFileInfo definitionInfo(definitionPath);
    const QFileInfo outputInfo(outputPath);
    if (!dbInfo.isFile() || !definitionInfo.isFile()
        || symbol.size() != 6 || !firstDate.isValid() || !lastDate.isValid()
        || firstDate > lastDate || firstDate.daysTo(lastDate) > 365
        || outputPath.isEmpty()
        || outputInfo.absoluteFilePath() == dbInfo.absoluteFilePath()
        || outputInfo.absoluteFilePath() == definitionInfo.absoluteFilePath()
        || (!outputInfo.canonicalFilePath().isEmpty()
            && (outputInfo.canonicalFilePath() == dbInfo.canonicalFilePath()
                || outputInfo.canonicalFilePath()
                    == definitionInfo.canonicalFilePath()))
        || !capitalOk || !(capital > 0.0) || capital > 1.0e12
        || !commissionOk || !std::isfinite(commission)
        || commission < 0.0 || commission > 0.1
        || !slippageOk || !std::isfinite(slippage)
        || slippage < 0.0 || slippage > 0.1) {
        fail(QStringLiteral("参数无效：需要 --db、--definition、--symbol、--from、--to、--output"));
        return 1;
    }

    QFile definitionFile(definitionPath);
    if (!definitionFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        fail(QStringLiteral("无法读取自定义策略定义"));
        return 2;
    }
    CustomStrategyDefinition definition;
    QString error;
    if (!CustomStrategyValidator().parseAndValidate(
            QString::fromUtf8(definitionFile.readAll()), &definition, &error)) {
        fail(QStringLiteral("策略定义无效：%1").arg(error));
        return 2;
    }
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    if (!plan) {
        fail(QStringLiteral("策略编译失败：%1").arg(error));
        return 2;
    }
    auto strategy = std::make_unique<CustomBacktestStrategy>(
        *plan, CustomBacktestInputMode::RawTicks);
    if (!strategy->supportsProvidedRawTicks()) {
        fail(QStringLiteral("逐笔模式要求 signal.priceMode=nextOpen，且暂不支持横截面/多股条件"));
        return 2;
    }

    SqliteLocalDataStore store;
    if (!store.initializeReadOnly(dbPath)) {
        fail(QStringLiteral("无法只读打开 Tick 数据库"));
        return 3;
    }
    Instrument instrument {symbol, symbol, Market::AShare,
                           symbol.startsWith(QLatin1Char('6'))
                               ? QStringLiteral("XSHG") : QStringLiteral("XSHE")};
    const auto fullIdentity = store.loadRawBacktestDatasetIdentity(instrument);
    if (!fullIdentity) {
        fail(QStringLiteral("所选股票没有原始 Tick 数据集身份"));
        return 3;
    }
    const BacktestDatasetIdentity identity =
        BacktestDataAvailabilityPlanner::trimIdentity(*fullIdentity, firstDate, lastDate);
    if (identity.days.isEmpty() || identity.firstTradingDate != firstDate
        || identity.lastTradingDate != lastDate || !identity.calendarCoverageKnown
        || identity.gapDayCount > 0 || identity.rawTickTotal() > std::numeric_limits<int>::max()
        || identity.rawTickTotal() < strategy->minimumWarmupBars()) {
        fail(QStringLiteral("数据窗口不连续、日历未覆盖或 Tick 数量超限；请选连续完整交易日"));
        return 3;
    }
    const BacktestDataQualityAssessment quality = BacktestDataQualityGate::assess(
        identity, firstDate, lastDate,
        store.loadAShareInstrumentDayRecords({symbol}, firstDate, lastDate),
        store.loadRawTickImportDayAudits(instrument, firstDate, lastDate));
    if (!quality.eligible) {
        fail(QStringLiteral("历史 Tick 数据质量准入拒绝：%1")
                 .arg(quality.messages.join(QStringLiteral("；"))));
        return 3;
    }

    BacktestConfig config;
    config.taskId = QStringLiteral("custom-raw-%1-%2-%3")
        .arg(symbol, firstDate.toString(Qt::ISODate), lastDate.toString(Qt::ISODate));
    config.strategyId = plan->strategyId;
    config.strategyVersion = plan->version;
    config.strategyParameters = plan->normalizedParameters;
    config.ruleSnapshot = strategy->ruleSnapshot();
    config.datasetIdentity = identity;
    config.dataMode = BacktestDataMode::RawTicks;
    config.instrument = instrument;
    config.frequency = QStringLiteral("tick");
    config.periodSeconds = 0;
    config.sampleFrom = QDateTime(firstDate, QTime(0, 0), QTimeZone("Asia/Shanghai"));
    config.sampleTo = QDateTime(lastDate.addDays(1), QTime(0, 0),
                                QTimeZone("Asia/Shanghai")).addMSecs(-1);
    config.initialCapital = capital;
    config.commissionRate = commission;
    config.slippageRate = slippage;
    config.lotSize = 100;

    BacktestEngine engine;
    engine.setObservationSignalsEnabled(false);
    QEventLoop loop;
    QString runError;
    QObject::connect(&engine, &BacktestEngine::finished, &loop, &QEventLoop::quit);
    QObject::connect(&engine, &BacktestEngine::errorOccurred, &loop,
                     [&runError](const QString &message) { runError = message; });
    QObject::connect(&engine, &BacktestEngine::errorOccurred, &loop, &QEventLoop::quit);
    int dayIndex = 0;
    const bool started = engine.startStreamedRawTicks(
        config, static_cast<int>(identity.rawTickTotal()),
        [&store, &identity, &instrument, &dayIndex](
            QVector<RawTradeTick> *chunk, QString *message) {
            chunk->clear();
            if (dayIndex >= identity.days.size()) return true;
            const RawTickDayFingerprint &day = identity.days.at(dayIndex);
            const RawTradeFingerprint before = store.rawTradeFingerprint(
                instrument, day.tradingDate);
            if (before.count != day.rawCount
                || before.contentSha256 != day.rawContentSha256) {
                if (message) *message = QStringLiteral("执行前 Tick 日指纹变化");
                return false;
            }
            const bool loaded = store.forEachRawTradeTick(
                instrument, day.tradingDate,
                [chunk](const RawTradeTick &tick) {
                    chunk->append(tick);
                    return true;
                });
            const RawTradeFingerprint after = store.rawTradeFingerprint(
                instrument, day.tradingDate);
            if (!loaded || chunk->size() != day.rawCount
                || after.count != before.count
                || after.contentSha256 != before.contentSha256) {
                if (message) *message = QStringLiteral("分段读取时 Tick 日指纹变化");
                return false;
            }
            ++dayIndex;
            return true;
        }, std::move(strategy));
    if (!started) {
        fail(runError.isEmpty() ? QStringLiteral("无法启动逐笔回测") : runError);
        return 4;
    }
    loop.exec();
    if (!runError.isEmpty()) {
        fail(QStringLiteral("逐笔回测失败：%1").arg(runError));
        return 4;
    }

    const BacktestResult &result = engine.result();
    QJsonArray trades;
    for (const BacktestTrade &trade : result.trades) trades.append(tradeJson(trade));
    const QJsonObject output {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("strategyId"), plan->strategyId},
        {QStringLiteral("strategyVersion"), plan->version},
        {QStringLiteral("definitionSha256"), plan->definitionSha256},
        {QStringLiteral("ruleSnapshot"), QJsonObject::fromVariantMap(config.ruleSnapshot)},
        {QStringLiteral("symbol"), symbol},
        {QStringLiteral("firstTradingDate"), firstDate.toString(Qt::ISODate)},
        {QStringLiteral("lastTradingDate"), lastDate.toString(Qt::ISODate)},
        {QStringLiteral("datasetVersion"), identity.datasetVersion},
        {QStringLiteral("datasetIdentity"),
         QJsonObject::fromVariantMap(identity.toVariantMap())},
        {QStringLiteral("dataQuality"),
         QJsonObject::fromVariantMap(quality.toVariantMap())},
        {QStringLiteral("dataVersion"), result.config.dataVersion},
        {QStringLiteral("observations"), result.config.actualBars},
        {QStringLiteral("closedTrades"), result.summary.totalTrades},
        {QStringLiteral("initialCapital"), capital},
        {QStringLiteral("commissionRate"), commission},
        {QStringLiteral("slippageRate"), slippage},
        {QStringLiteral("lotSize"), 100},
        {QStringLiteral("finalEquity"), result.summary.finalEquity},
        {QStringLiteral("totalReturnPercent"), result.summary.totalReturnPercent},
        {QStringLiteral("totalCommission"), result.summary.totalCommission},
        {QStringLiteral("profitableArchiveEligible"),
         isProfitableBacktestForArchive(result)},
        {QStringLiteral("trades"), trades}
    };
    QSaveFile outputFile(outputPath);
    const QByteArray payload = QJsonDocument(output).toJson(QJsonDocument::Indented);
    if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Text)
        || outputFile.write(payload) != payload.size()
        || !outputFile.commit()) {
        fail(QStringLiteral("结果 JSON 写入失败"));
        return 5;
    }
    QTextStream(stdout) << QStringLiteral("已完成 %1 · %2 笔 Tick · %3 笔闭合交易 · %4%")
        .arg(symbol).arg(result.config.actualBars).arg(result.summary.totalTrades)
        .arg(result.summary.totalReturnPercent, 0, 'f', 4) << Qt::endl;
    return 0;
}
