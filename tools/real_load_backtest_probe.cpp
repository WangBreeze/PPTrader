#include "domain/BatchBacktestTypes.h"
#include "domain/LocalHistoryTypes.h"
#include "services/BatchBacktestService.h"
#include "services/SqliteLocalDataStore.h"
#include "services/TickDataCsvImporter.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUuid>

#include <memory>

namespace {

pp::Instrument instrumentFor(const QString &symbol)
{
    pp::Instrument instrument;
    instrument.symbol = symbol;
    instrument.name = symbol;
    instrument.market = pp::Market::AShare;
    instrument.venue = symbol.startsWith(QLatin1Char('6'))
        ? QStringLiteral("XSHG") : QStringLiteral("XSHE");
    return instrument;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("s"), QStringLiteral("staging")},
         QStringLiteral("暂存根目录（YYYYMMDD/代码.csv）"), QStringLiteral("path")},
        {{QStringLiteral("m"), QStringLiteral("symbols")},
         QStringLiteral("逗号分隔代码"), QStringLiteral("codes")},
        {{QStringLiteral("b"), QStringLiteral("bars")},
         QStringLiteral("回测根数"), QStringLiteral("count")},
        {{QStringLiteral("o"), QStringLiteral("output")},
         QStringLiteral("结果 JSON 路径"), QStringLiteral("path")},
        {{QStringLiteral("k"), QStringLiteral("keep-db")},
         QStringLiteral("可选：保留临时数据库路径"), QStringLiteral("path")},
    });
    parser.process(application);

    const QString staging = parser.value(QStringLiteral("staging"));
    const QString outputPath = parser.value(QStringLiteral("output"));
    const QString keepDb = parser.value(QStringLiteral("keep-db"));
    QStringList symbols;
    for (const QString &value : parser.value(QStringLiteral("symbols")).split(',')) {
        const QString symbol = value.trimmed();
        if (symbol.size() == 6 && !symbols.contains(symbol)) symbols.append(symbol);
    }
    bool barsOk = false;
    const int bars = parser.value(QStringLiteral("bars")).toInt(&barsOk);
    if (staging.isEmpty() || outputPath.isEmpty() || symbols.isEmpty()
        || !barsOk || bars < 2 || !QDir(staging).exists()) {
        QTextStream(stderr) << QStringLiteral(
            "参数无效：需要 --staging/--symbols/--bars/--output") << Qt::endl;
        return 1;
    }

    std::unique_ptr<QTemporaryDir> temporaryRoot;
    QString databasePath = keepDb;
    if (databasePath.isEmpty()) {
        temporaryRoot = std::make_unique<QTemporaryDir>();
        if (!temporaryRoot->isValid()) return 2;
        databasePath = QDir(temporaryRoot->path()).filePath(
            QStringLiteral("real-load.db"));
    }

    QJsonArray fileResults;
    qint64 ticksWritten = 0;
    qint64 barsWritten = 0;
    int failedFiles = 0;
    qint64 schemaInitMilliseconds = 0;
    qint64 loadElapsedMilliseconds = 0;
    {
        QElapsedTimer schemaTimer;
        schemaTimer.start();
        pp::SqliteLocalDataStore store;
        if (!store.initialize(databasePath)) {
            QTextStream(stderr) << QStringLiteral("无法初始化临时 SQLite") << Qt::endl;
            return 3;
        }
        schemaInitMilliseconds = schemaTimer.elapsed();

        QElapsedTimer loadTimer;
        loadTimer.start();
        const QDir stagingRoot(staging);
        const QStringList dateDirs = stagingRoot.entryList(
            {QStringLiteral("????????")}, QDir::Dirs, QDir::Name);
        for (const QString &symbol : symbols) {
            for (const QString &dateDir : dateDirs) {
                const QDate tradingDate = QDate::fromString(dateDir, QStringLiteral("yyyyMMdd"));
                if (!tradingDate.isValid()) continue;
                const QString filePath = stagingRoot.filePath(
                    dateDir + QLatin1Char('/') + symbol + QStringLiteral(".csv"));
                if (!QFileInfo::exists(filePath)) continue;

                pp::TickDataImportRequest request;
                request.rootDirectory = stagingRoot.absolutePath();
                request.instrument = instrumentFor(symbol);
                request.tradingDate = tradingDate;
                request.format = pp::TickDataCsvFormat::Tick;
                request.periodSeconds = 60;

                qint64 parseMilliseconds = 0;
                qint64 rawDatabaseMilliseconds = 0;
                qint64 candleDatabaseMilliseconds = 0;
                QElapsedTimer timer;
                timer.start();
                const bool transactionReady = store.beginRawTradeTickImport(
                    request.instrument, tradingDate, QStringLiteral("tickdata_csv_tick"));
                const auto sink =
                    [&store, transactionReady, &rawDatabaseMilliseconds](
                        const QVector<pp::RawTradeTick> &ticks) {
                        if (!transactionReady) return false;
                        QElapsedTimer databaseTimer;
                        databaseTimer.start();
                        const bool succeeded = store.appendRawTradeTicks(ticks);
                        rawDatabaseMilliseconds += databaseTimer.elapsed();
                        return succeeded;
                    };
                pp::TickDataImportResult imported =
                    pp::TickDataCsvImporter::parse(request, {}, sink);
                parseMilliseconds = timer.elapsed();
                bool committed = false;
                if (transactionReady && imported.succeeded) {
                    timer.restart();
                    committed = store.commitRawTradeTickImport();
                    rawDatabaseMilliseconds += timer.elapsed();
                } else if (transactionReady) {
                    store.rollbackRawTradeTickImport();
                }
                if (!committed) {
                    ++failedFiles;
                    continue;
                }
                const pp::RawTradeFingerprint fingerprint =
                    store.rawTradeFingerprint(request.instrument, tradingDate);
                timer.restart();
                const bool candlesStored = store.replaceRawTickCandleCache(
                    request.instrument, tradingDate, 60, fingerprint,
                    imported.candles);
                candleDatabaseMilliseconds = timer.elapsed();
                if (!candlesStored) {
                    ++failedFiles;
                    continue;
                }
                ticksWritten += imported.validRecords;
                barsWritten += imported.candles.size();
                QJsonObject fileResult;
                fileResult.insert(QStringLiteral("symbol"), symbol);
                fileResult.insert(QStringLiteral("tradingDate"),
                                  tradingDate.toString(Qt::ISODate));
                fileResult.insert(QStringLiteral("linesRead"),
                                  static_cast<double>(imported.linesRead));
                fileResult.insert(QStringLiteral("validRecords"),
                                  static_cast<double>(imported.validRecords));
                fileResult.insert(QStringLiteral("skippedRecords"),
                                  static_cast<double>(imported.skippedRecords));
                fileResult.insert(QStringLiteral("bars"),
                                  static_cast<double>(imported.candles.size()));
                fileResult.insert(QStringLiteral("parseMilliseconds"),
                                  static_cast<double>(parseMilliseconds));
                fileResult.insert(QStringLiteral("rawDatabaseMilliseconds"),
                                  static_cast<double>(rawDatabaseMilliseconds));
                fileResult.insert(QStringLiteral("candleDatabaseMilliseconds"),
                                  static_cast<double>(candleDatabaseMilliseconds));
                fileResults.append(fileResult);
            }
        }
        loadElapsedMilliseconds = loadTimer.elapsed();
    }

    QJsonObject identityResult;
    {
        pp::SqliteLocalDataStore store;
        if (store.initialize(databasePath)) {
            const auto identity = store.loadBacktestDatasetIdentity(
                instrumentFor(symbols.first()), 60);
            if (identity) {
                identityResult.insert(QStringLiteral("datasetVersion"),
                                      identity->datasetVersion);
                identityResult.insert(QStringLiteral("dayCount"),
                                      identity->days.size());
                identityResult.insert(QStringLiteral("rawTickCount"),
                                      identity->rawTickTotal());
                identityResult.insert(QStringLiteral("derivedBarCount"),
                                      identity->derivedBarTotal());
                identityResult.insert(QStringLiteral("staleDayCount"),
                                      identity->staleDayCount);
            }
        }
    }

    QJsonObject backtestResult;
    qint64 backtestElapsedMilliseconds = 0;
    QString archiveResultId;
    qint64 archiveElapsedMilliseconds = -1;
    {
        pp::BatchBacktestService service;
        service.setDatabasePath(databasePath);
        QVector<pp::LocalHistoryDataset> datasets;
        datasets.reserve(symbols.size());
        for (const QString &symbol : symbols) {
            pp::LocalHistoryDataset dataset;
            dataset.instrument = instrumentFor(symbol);
            dataset.periodSeconds = 60;
            dataset.source = QStringLiteral("raw_tick_cache_v1");
            dataset.adjustment = QStringLiteral("none");
            datasets.append(dataset);
        }
        pp::BatchBacktestRequest request;
        request.requestId = 1;
        request.datasets = std::move(datasets);
        request.strategyId = QStringLiteral("sma_crossover");
        request.strategyParameters = {};
        request.barCount = bars;
        request.initialCapital = 1000000.0;
        request.commissionRate = 0.0003;
        request.slippageRate = 0.0002;
        request.lotSize = 100;
        request.maximumActiveEngines = 20;

        int completed = 0;
        int succeeded = 0;
        QStringList errors;
        QString dataVersion;
        QString datasetVersion;
        pp::BacktestResult retained;
        QEventLoop loop;
        QObject::connect(&service, &pp::BatchBacktestService::itemReady,
                         &loop, [&](quint64, const pp::BatchBacktestItemResult &item) {
            ++completed;
            if (item.succeeded) {
                ++succeeded;
                dataVersion = item.dataVersion;
                datasetVersion = item.datasetVersion;
            } else {
                errors.append(item.instrument.symbol + QStringLiteral(":") + item.error);
            }
        });
        QObject::connect(&service, &pp::BatchBacktestService::itemCompleted,
                         &loop, [&](quint64, const QString &,
                                    const pp::BacktestResult &result) {
            retained = result;
        });
        QObject::connect(&service, &pp::BatchBacktestService::finished,
                         &loop, [&]() { loop.quit(); });
        QElapsedTimer backtestTimer;
        if (!service.start(request)) {
            QTextStream(stderr) << QStringLiteral("批量回测服务未能启动") << Qt::endl;
            return 4;
        }
        backtestTimer.start();
        loop.exec();
        backtestElapsedMilliseconds = backtestTimer.elapsed();

        backtestResult.insert(QStringLiteral("requested"),
                              static_cast<double>(request.requestId));
        backtestResult.insert(QStringLiteral("completed"), completed);
        backtestResult.insert(QStringLiteral("succeeded"), succeeded);
        backtestResult.insert(QStringLiteral("failed"), errors.size());
        backtestResult.insert(QStringLiteral("dataVersion"), dataVersion);
        backtestResult.insert(QStringLiteral("datasetVersion"), datasetVersion);
        QJsonArray errorArray;
        for (const QString &error : errors) errorArray.append(error);
        backtestResult.insert(QStringLiteral("errors"), errorArray);

        if (!retained.config.datasetIdentity.isEmpty()) {
            pp::SqliteLocalDataStore store;
            if (store.initialize(databasePath)) {
                pp::BacktestArchiveRecord archive;
                archive.resultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                archive.completedAt = QDateTime::currentDateTimeUtc();
                archive.result = retained;
                QElapsedTimer archiveTimer;
                archiveTimer.start();
                if (store.saveBacktestArchive(archive)) {
                    archiveResultId = archive.resultId;
                    archiveElapsedMilliseconds = archiveTimer.elapsed();
                }
            }
        }
    }

    const qint64 databaseBytes = QFileInfo(databasePath).size()
        + QFileInfo(databasePath + QStringLiteral("-wal")).size()
        + QFileInfo(databasePath + QStringLiteral("-shm")).size();
    QJsonObject report;
    report.insert(QStringLiteral("date"),
                  QDate::currentDate().toString(Qt::ISODate));
    report.insert(QStringLiteral("stagingRoot"), staging);
    QJsonArray symbolArray;
    for (const QString &symbol : symbols) symbolArray.append(symbol);
    report.insert(QStringLiteral("symbols"), symbolArray);
    report.insert(QStringLiteral("bars"), bars);
    report.insert(QStringLiteral("schemaInitMilliseconds"),
                  static_cast<double>(schemaInitMilliseconds));
    report.insert(QStringLiteral("loadElapsedMilliseconds"),
                  static_cast<double>(loadElapsedMilliseconds));
    report.insert(QStringLiteral("ticksWritten"), static_cast<double>(ticksWritten));
    report.insert(QStringLiteral("barsWritten"), static_cast<double>(barsWritten));
    report.insert(QStringLiteral("failedFiles"), failedFiles);
    report.insert(QStringLiteral("perFile"), fileResults);
    report.insert(QStringLiteral("identity"), identityResult);
    QJsonObject backtest;
    backtest.insert(QStringLiteral("elapsedMilliseconds"),
                    static_cast<double>(backtestElapsedMilliseconds));
    for (auto it = backtestResult.constBegin(); it != backtestResult.constEnd(); ++it) {
        backtest.insert(it.key(), it.value());
    }
    report.insert(QStringLiteral("backtest"), backtest);
    QJsonObject archive;
    archive.insert(QStringLiteral("resultId"), archiveResultId);
    archive.insert(QStringLiteral("elapsedMilliseconds"),
                   static_cast<double>(archiveElapsedMilliseconds));
    report.insert(QStringLiteral("archive"), archive);
    report.insert(QStringLiteral("databaseBytes"), static_cast<double>(databaseBytes));
    report.insert(QStringLiteral("databasePath"), databasePath);

    const QByteArray payload = QJsonDocument(report).toJson(QJsonDocument::Indented);
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || output.write(payload) < 0) {
        return 5;
    }
    QTextStream(stdout) << QString::fromUtf8(payload) << Qt::endl;
    return 0;
}
