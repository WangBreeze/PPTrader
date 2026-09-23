#include "app/TradingController.h"
#include "services/CompositeMarketDataProvider.h"
#include "services/TickDataCsvImporter.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSettings>
#include <QThread>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
constexpr int kMaximumPortfolioReplayDates = 31;
constexpr qsizetype kMaximumPortfolioReplayTicks = 2'000'000;

bool saveJson(const QString &path, const QJsonObject &value)
{
    QSaveFile file(path);
    const auto bytes = QJsonDocument(value).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool waitFor(const std::function<bool()> &ready, int limit = 10000)
{
    QElapsedTimer time; time.start();
    while (!ready() && time.elapsed() < limit) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    return ready();
}
double percentile(QVector<double> values, double p)
{
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    return values.at(std::clamp(int(std::ceil(p * values.size())) - 1, 0, int(values.size()) - 1));
}
}

// 流程验收入口：真实 Controller/Main、独立目录；绝不连接真实券商。
int runPaperFlowAcceptance()
{
    using namespace pp;
    const QString mode = qEnvironmentVariable("PPTRADER_PAPER_FLOW_MODE");
    const QString output = qEnvironmentVariable("PPTRADER_PAPER_FLOW_OUTPUT");
    const bool portfolioReplay = mode == QStringLiteral("portfolio-replay");
    const bool replay = mode == QStringLiteral("replay") || portfolioReplay;
    const QString paceText = qEnvironmentVariable("PPTRADER_PAPER_FLOW_PACE_SECONDS");
    bool validPace = true;
    const qint64 paceSeconds = paceText.isEmpty() ? 0 : paceText.toLongLong(&validPace);
    if (!validPace || paceSeconds < 0 || paceSeconds > 14400 || (paceSeconds > 0 && !portfolioReplay)) return 2;
    const qint64 paceDurationMs = paceSeconds * 1000;
    const bool preflight = mode == QStringLiteral("preflight");
    if ((!replay && !preflight && mode != QStringLiteral("live")) || !QDir::isAbsolutePath(output)
        || QFileInfo::exists(QDir(output).filePath("report.json"))) return 2;
    QDir().mkpath(output);
    const auto path = [&](const QString &name) { return QDir(output).filePath(name); };
    const Instrument stock{QStringLiteral("000001"), QStringLiteral("平安银行"), Market::AShare};
    QCoreApplication::setOrganizationName(QStringLiteral("PPTraderAcceptance"));
    QCoreApplication::setApplicationName(QStringLiteral("PaperFlow"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, output);
    qputenv("PPTRADER_DATABASE_PATH", path("isolated.db").toLocal8Bit());
    qputenv("PPTRADER_LOG_PATH", path("pptrader.log").toLocal8Bit());
    qputenv("PPTRADER_AUTOSTART_LIVE", "0");
    if (replay) QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 9));
    const QDateTime started = QDateTime::currentDateTimeUtc();
    if (preflight) {
        // 同二进制、同 Provider；一个代码、一次分钟响应及一批快照，串行回收。
        TencentMinuteBarService minute;
        bool received = false, good = false;
        QString error;
        QJsonObject document{{"mode", mode}, {"symbol", stock.symbol}, {"startedAtUtc", started.toString(Qt::ISODateWithMs)}};
        QObject::connect(&minute, &TencentMinuteBarService::resultReady, &minute,
            [&](quint64, const RealtimeMinuteSeriesResult &result) {
                received = true; good = result.succeeded && !result.candles.isEmpty(); error = result.error;
                document.insert("minuteRows", int(result.candles.size()));
                document.insert("lastBarUtc", result.lastBarTime.toString(Qt::ISODateWithMs));
            });
        minute.request(1, stock);
        waitFor([&] { return received; }, 25000);
        minute.cancel();
        good = good && received && error.isEmpty();
        if (good) {
            CompositeMarketDataProvider provider;
            received = false; good = false;
            QObject::connect(&provider, &MarketDataProvider::snapshotReceived, &provider,
                [&](const MarketSnapshot &snapshot) {
                    if (received) return;
                    received = true;
                    good = snapshot.expectedCount == 1 && snapshot.quotes.size() == 1 && snapshot.quotes.first().price > 0;
                    document.insert("snapshotRows", int(snapshot.quotes.size()));
                    document.insert("providerMs", snapshot.durationMilliseconds());
                    provider.stop();
                });
            QObject::connect(&provider, &MarketDataProvider::providerError, &provider,
                [&](const QString &message) { error = message; received = true; good = false; provider.stop(); });
            provider.start({stock});
            waitFor([&] { return received; }, 45000);
            provider.shutdown();
            good = good && received && error.isEmpty();
        }
        document.insert("passed", good); document.insert("error", error);
        return saveJson(path("report.json"), document) && good ? 0 : 1;
    }
    const QTimeZone zone("Asia/Shanghai");
    QDateTime deadline = QDateTime::fromString(qEnvironmentVariable("PPTRADER_PAPER_FLOW_END"), Qt::ISODate);
    if (!deadline.isValid() || deadline <= started) return 2;
    const auto localNow = started.toTimeZone(zone);
    if (replay && !portfolioReplay) deadline = std::min(deadline, QDateTime(localNow.date(), QTime(8, 59), zone));
    else if (!replay && localNow.time() < QTime(9, 30)) return 2;
    if (deadline <= started) return 2;
    const int candidates = 1; // 一个工作台只绑定一份资金仓，不再启动独立候选账户。
    const QStringList symbols = portfolioReplay
        ? qEnvironmentVariable("PPTRADER_PAPER_FLOW_SYMBOLS").split(',', Qt::SkipEmptyParts)
        : QStringList{stock.symbol};
    const QStringList dates = portfolioReplay
        ? qEnvironmentVariable("PPTRADER_PAPER_FLOW_DATES").split(',', Qt::SkipEmptyParts)
        : QStringList{QStringLiteral("20260903"), QStringLiteral("20260904"), QStringLiteral("20260907")};
    if (symbols.isEmpty() || symbols.size() > 20 || dates.isEmpty()
        || dates.size() > kMaximumPortfolioReplayDates) return 2;
    QVector<RawTradeTick> ticks;
    QVector<Candle> bars;
    QJsonArray sources;
    QJsonArray qualityWarnings;
    QString failure;
    QHash<QString, QString> expectedHashes;
    if (portfolioReplay) {
        QFile manifest(qEnvironmentVariable("PPTRADER_PAPER_FLOW_MANIFEST"));
        if (!manifest.open(QIODevice::ReadOnly)) return 3;
        const auto provenance = QJsonDocument::fromJson(manifest.readAll()).object();
        if (!provenance.value("allHashesMatch").toBool()) return 3;
        for (const auto &value : provenance.value("remote").toArray()) {
            const auto item = value.toObject();
            expectedHashes.insert(QDir::cleanPath(item.value("localPath").toString()), item.value("sha256").toString().toLower());
        }
    }
    if (replay) {
        // 本次流程压力窗口明确排除开/收盘集合竞价和稀疏延长段；不补造无成交分钟。
        const auto inReplayWindow = [&](const QDateTime &time) {
            const auto clock = time.toTimeZone(zone).time();
            return (clock >= QTime(9, 30) && clock < QTime(11, 30))
                || (clock >= QTime(13, 0) && clock < QTime(14, 57));
        };
        const QString sourceRoot = qEnvironmentVariable("PPTRADER_PAPER_FLOW_SOURCE");
        for (const QString &date : dates) for (const QString &symbol : symbols) {
            TickDataImportRequest request;
            request.rootDirectory = sourceRoot; request.instrument = Instrument{symbol, symbol, Market::AShare};
            request.tradingDate = QDate::fromString(date, "yyyyMMdd");
            QFile original(TickDataCsvImporter::expectedFilePath(request));
            if (!original.open(QIODevice::ReadOnly)) { failure = QStringLiteral("源文件不可读"); continue; }
            const QString beforeHash = QString::fromLatin1(QCryptographicHash::hash(original.readAll(), QCryptographicHash::Sha256).toHex());
            original.close();
            if (portfolioReplay && expectedHashes.value(QDir::cleanPath(original.fileName())) != beforeHash) {
                failure = QStringLiteral("源文件与 NAS 来源清单摘要不一致"); continue;
            }
            const qsizetype before = ticks.size();
            const auto result = TickDataCsvImporter::parse(request, [&] { return QDateTime::currentDateTimeUtc() >= deadline; },
                [&](const QVector<RawTradeTick> &batch) {
                    for (const auto &tick : batch) if (inReplayWindow(tick.timestamp)) ticks.append(tick);
                    return ticks.size() <= kMaximumPortfolioReplayTicks;
                });
            QVector<Candle> eligible;
            for (const auto &bar : result.candles) if (inReplayWindow(bar.startTime)) eligible.append(bar);
            QFile input(result.filePath);
            if (!input.open(QIODevice::ReadOnly)) return 3;
            const QString afterHash = QString::fromLatin1(QCryptographicHash::hash(input.readAll(), QCryptographicHash::Sha256).toHex());
            sources.append(QJsonObject{{"date", date}, {"symbol", symbol}, {"records", result.validRecords},
                {"missingMinutes", result.missingContinuousBars}, {"skipped", result.skippedRecords},
                {"replayRecords", qint64(ticks.size() - before)}, {"excludedOutsideReplayWindow", result.validRecords - (ticks.size() - before)},
                {"replayMinuteBars", int(eligible.size())}, {"replayWindow", "09:30-11:30;13:00-14:57 (half-open)"},
                {"outOfOrder", result.outOfOrderRecords},
                {"sha256", afterHash}, {"unchangedDuringRead", beforeHash == afterHash}});
            if (eligible.size() < 237) qualityWarnings.append(QJsonObject{
                {"date", date}, {"symbol", symbol}, {"missingMinuteCount", 237 - int(eligible.size())},
                {"message", "无记录分钟原因未知；按真实 Bar 继续，不补造 Tick 或 K 线"}});
            if (!result.succeeded || result.cancelled || result.outOfOrderRecords || result.skippedRecords
                || eligible.isEmpty() || beforeHash != afterHash) {
                failure = QStringLiteral("历史文件解析失败、乱序、无有效输入、非法记录或摘要变化；停止而非掩盖输入错误");
            }
            bars += eligible;
        }
        std::stable_sort(ticks.begin(), ticks.end(), [](const auto &a, const auto &b) {
            return a.timestamp != b.timestamp ? a.timestamp < b.timestamp : a.instrument.symbol < b.instrument.symbol;
        });
        std::stable_sort(bars.begin(), bars.end(), [](const auto &a, const auto &b) {
            return a.startTime != b.startTime ? a.startTime < b.startTime : a.instrument.symbol < b.instrument.symbol;
        });
        if (failure.isEmpty() && (ticks.isEmpty() || bars.isEmpty())) failure = QStringLiteral("历史数据为空");
    }
    TradingController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("tradingController", &controller);
    engine.load(QUrl("qrc:/PPTrader/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 4;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window) return 4;
    window->setTitle(replay ? QStringLiteral("PPTrader · NAS 历史回放 → 模拟账户") : QStringLiteral("PPTrader · 真实行情 → 策略 → 模拟账户"));
    QMetaObject::invokeMethod(window, "activateWorkspace", Q_ARG(QVariant, QVariant(4)));
    const QString strategyId = QStringLiteral("custom_paper_flow_sma_wma");
    const QString definition = QString::fromUtf8(R"({"schemaVersion":2,"strategyId":"custom_paper_flow_sma_wma","version":1,
        "name":"SMA5 / WMA10 流程候选","description":"固定规则；只验收流程，不按收益筛选","author":"PPTrader",
        "market":"A股","executionScope":"mock_execution","position":{"type":"fixedPercent","percent":0.1},"createdAt":"2026-09-08T00:00:00Z","updatedAt":"2026-09-08T00:00:00Z","parameters":[],
        "entry":{"op":"gt","left":{"indicator":"SMA","period":5},"right":{"indicator":"WMA","period":10}},
        "exit":{"op":"lt","left":{"indicator":"SMA","period":5},"right":{"indicator":"WMA","period":10}}})");
    if (!controller.requestSaveCustomStrategyJson(definition)
        || !waitFor([&] { return !controller.customStrategySaveLoading(); })
        || !controller.customStrategySaveError().isEmpty()) failure = controller.customStrategySaveError();
    if (failure.isEmpty()) {
        if (!replay) controller.startLive();
        PaperStrategyConfig config;
        config.maximumTotalPositionFraction = .6;
        config.maximumHoldingSymbols = 6;
        for (int i = 0; i < candidates; ++i) {
            const QString id = portfolioReplay
                ? controller.startPaperReplayPortfolio(strategyId, 1, symbols, ticks.first().timestamp, config)
                : replay
                ? controller.startPaperReplayRun(strategyId, 1, stock.symbol, ticks.first().timestamp)
                : controller.startPaperStrategyRun(strategyId, 1, stock.symbol, 100000, .0003, .0002);
            if (id.isEmpty()) { failure = controller.paperStrategyError(); break; }
        }
    }
    QFile samples(path("samples.jsonl"));
    if (!samples.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return 5;
    QElapsedTimer elapsed; elapsed.start();
    QElapsedTimer sinceHeartbeat; sinceHeartbeat.start();
    QVector<double> slices, heartbeatGaps;
    qsizetype tickIndex = 0, barIndex = 0;
    int deliveredBars = 0;
    QHash<QString, int> deliveredPerStock;
    bool done = !failure.isEmpty();
    QString finishReason = done ? QStringLiteral("setup-failed") : QString{};
    const auto sample = [&] {
        const auto rows = controller.paperStrategyRuns();
        if (!done) {
            QString error = controller.providerError();
            for (const auto &value : rows) {
                const auto run = value.toMap();
                const QString feedError = run.value("feedError").toString();
                if (run.value("stopped").toBool() || !feedError.isEmpty()) {
                    failure = feedError.isEmpty() ? QStringLiteral("模拟实例提前停止") : feedError;
                    done = true; finishReason = QStringLiteral("flow-stopped");
                }
                error += feedError;
            }
            if (error.contains(QStringLiteral("Permission denied"), Qt::CaseInsensitive)
                || error.contains(QStringLiteral("SocketAccessError"), Qt::CaseInsensitive)) {
                failure = error; done = true; finishReason = QStringLiteral("network-permission-failed");
            }
        }
        const QJsonObject row{{"atUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"elapsedMs", elapsed.elapsed()}, {"ticksConsumed", qint64(tickIndex)}, {"barsDelivered", deliveredBars},
            {"runs", QJsonArray::fromVariantList(rows)}, {"provider", controller.activeRealtimeProvider()},
            {"providerCompleteness", controller.providerCompleteness()}, {"providerError", controller.providerError()}};
        samples.write(QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n'); samples.flush();
    };
    QTimer work, heartbeat, sampler;
    QObject::connect(&heartbeat, &QTimer::timeout, &controller, [&] {
        heartbeatGaps.append(sinceHeartbeat.restart());
        if (QDateTime::currentDateTimeUtc() >= deadline) { done = true; finishReason = QStringLiteral("deadline"); }
        if (!window->isVisible()) { done = true; finishReason = QStringLiteral("window-closed"); }
    });
    QObject::connect(&sampler, &QTimer::timeout, &controller, sample);
    QObject::connect(&work, &QTimer::timeout, &controller, [&] {
        if (done || !replay) return;
        QElapsedTimer slice; slice.start();
        int count = 0;
        // 只控制真实输入的投递节奏：不重复回放、不重置账户、不改写源时刻。
        // 输入最多200万条、最长4小时，乘积在qint64范围内。
        const qsizetype available = paceDurationMs > 0
            ? qsizetype(qint64(ticks.size()) * std::min(elapsed.elapsed(), paceDurationMs) / paceDurationMs)
            : ticks.size();
        while (tickIndex < available && slice.elapsed() < 12 && count++ < 50) {
            const auto &raw = ticks.at(tickIndex++);
            while (barIndex < bars.size() && bars.at(barIndex).startTime.addSecs(180) <= raw.timestamp) {
                const auto &bar = bars.at(barIndex++);
                const bool warmup = deliveredPerStock.value(bar.instrument.symbol) < 10 || bar.startTime.secsTo(raw.timestamp) > 300;
                if (controller.acceptPaperReplayBar(bar, raw.timestamp, warmup)) {
                    ++deliveredBars; ++deliveredPerStock[bar.instrument.symbol];
                }
            }
            Tick quote; quote.instrument = raw.instrument; quote.timestamp = raw.timestamp;
            quote.price = raw.price; // 原始成交价作为研究报价代理，无真实买卖盘，不改历史时刻。
            controller.acceptPaperReplayQuote(quote);
        }
        if (count > 0) slices.append(double(slice.nsecsElapsed()) / 1e6);
        if (tickIndex == ticks.size()) { done = true; finishReason = QStringLiteral("input-complete"); }
    });
    heartbeat.start(100); sampler.start(paceDurationMs >= 60000 ? 5000 : 1000); work.start(5);
    sample();
    while (!done) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
    work.stop(); sampler.stop(); heartbeat.stop();
    controller.stopAllPaperStrategies();
    controller.stopLive(); sample();
    const auto rows = controller.paperStrategyRuns();
    const int expectedBars = replay && !ticks.isEmpty() ? int(std::count_if(bars.cbegin(), bars.cend(),
        [&](const Candle &bar) { return bar.startTime.addSecs(180) <= ticks.last().timestamp; })) : 0;
    bool allComputed = rows.size() == candidates, allFilled = allComputed;
    bool directionalCountsConsistent = allComputed;
    bool rejectionCountsConsistent = allComputed;
    bool executionRecordsConsistent = allComputed;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        allComputed = allComputed && row.value("barCount").toInt() > 10 && row.value("feedError").toString().isEmpty();
        if (replay) allComputed = allComputed && row.value("barCount").toInt() == expectedBars;
        allFilled = allFilled && row.value("fillCount").toInt() > 0;
        directionalCountsConsistent = directionalCountsConsistent
            && row.value("buyFillCount").toInt() + row.value("sellFillCount").toInt()
                == row.value("fillCount").toInt();
        rejectionCountsConsistent = rejectionCountsConsistent
            && row.value("rejectedBuyCashCount").toInt()
                + row.value("rejectedBuyPositionLimitCount").toInt()
                + row.value("rejectedSellTPlusOneCount").toInt()
                + row.value("rejectedSellNoPositionCount").toInt()
                + row.value("rejectedSubmitCount").toInt()
                == row.value("rejectedCount").toInt();
        const auto executionRecords = row.value("executionRecords").toList();
        executionRecordsConsistent = executionRecordsConsistent
            && row.value("executionRecordCount").toInt() == row.value("fillCount").toInt()
            && row.value("displayedExecutionRecordCount").toInt() == executionRecords.size()
            && executionRecords.size() == std::min(row.value("fillCount").toInt(), 100);
        int expectedRecordNumber = row.value("fillCount").toInt() - executionRecords.size() + 1;
        for (const auto &recordValue : executionRecords) {
            const auto record = recordValue.toMap();
            const QString sideCode = record.value("sideCode").toString();
            executionRecordsConsistent = executionRecordsConsistent
                && record.value("recordNumber").toInt() == expectedRecordNumber++
                && !record.value("executionId").toString().isEmpty()
                && !record.value("orderId").toString().isEmpty()
                && symbols.contains(record.value("symbol").toString())
                && (sideCode == QStringLiteral("buy") || sideCode == QStringLiteral("sell"))
                && record.value("side").toString()
                    == (sideCode == QStringLiteral("buy") ? QStringLiteral("买入") : QStringLiteral("卖出"))
                && record.value("quantity").toDouble() > 0
                && record.value("price").toDouble() > 0
                && !record.value("timestampLocal").toString().isEmpty();
        }
    }
    const bool complete = replay ? tickIndex == ticks.size() && finishReason == QStringLiteral("input-complete")
                                 : finishReason == QStringLiteral("deadline");
    const QJsonObject report{{"mode", mode}, {"startedAtUtc", started.toString(Qt::ISODateWithMs)},
        {"finishedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"deadlineUtc", deadline.toUTC().toString(Qt::ISODateWithMs)}, {"reason", finishReason}, {"failure", failure},
        {"candidates", candidates}, {"sourceFiles", sources}, {"sourceTicks", int(ticks.size())},
        {"dataQualityWarnings", qualityWarnings},
        {"indicatorWindowBasis", "N real observed bars; not N consecutive clock minutes; no synthesized bars"},
        {"symbols", QJsonArray::fromStringList(symbols)}, {"accountCount", int(rows.size())},
        {"inputKind", "NAS raw trades aggregated to 1m indicators; trade prices as quote proxy"},
        {"fullMarketCapacityAccepted", false}, {"longStabilityAccepted", false},
        {"ticksConsumed", qint64(tickIndex)}, {"barsDelivered", deliveredBars}, {"expectedBarsPerRun", expectedBars}, {"inputComplete", complete},
        {"elapsedMs", elapsed.elapsed()}, {"sliceP50Ms", percentile(slices, .5)}, {"sliceP95Ms", percentile(slices, .95)},
        {"paceDurationMs", paceDurationMs},
        {"pacingBasis", "equal input-count pacing; original event timestamps preserved; not exchange-time speed or max throughput"},
        {"sliceMaxMs", percentile(slices, 1)}, {"heartbeatP95Ms", percentile(heartbeatGaps, .95)},
        {"heartbeatMaxMs", percentile(heartbeatGaps, 1)}, {"allComputed", allComputed}, {"allFilled", allFilled},
        {"directionalCountsConsistent", directionalCountsConsistent},
        {"rejectionCountsConsistent", rejectionCountsConsistent},
        {"executionRecordsConsistent", executionRecordsConsistent},
        {"flowPassed", failure.isEmpty() && complete && allComputed && allFilled
            && directionalCountsConsistent && rejectionCountsConsistent && executionRecordsConsistent},
        {"profitabilityAccepted", false}, {"realOrdersEnabled", false},
        {"runs", QJsonArray::fromVariantList(rows)}};
    window->grabWindow().save(path("final-window.png"));
    const bool saved = saveJson(path("report.json"), report);
    window->close(); controller.shutdown();
    return saved && failure.isEmpty() && complete && allComputed && allFilled
        && directionalCountsConsistent && rejectionCountsConsistent && executionRecordsConsistent ? 0 : 1;
}
