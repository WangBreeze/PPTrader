#include "services/BacktestEngine.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace {

double percentile(QVector<qint64> values, double fraction)
{
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const int valueCount = static_cast<int>(values.size());
    const int index = std::clamp(
        static_cast<int>(std::ceil(fraction * valueCount)) - 1,
        0, valueCount - 1);
    return static_cast<double>(values.at(index));
}

QVector<pp::Candle> loadTemplate(const QString &databasePath,
                                 const QString &symbol, int requestedBars,
                                 QString *error)
{
    QVector<pp::Candle> candles;
    const QString connectionName = QStringLiteral("capacity-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (!database.open()) {
            if (error) *error = QStringLiteral("无法打开容量基准 SQLite");
        } else {
            QSqlQuery query(database);
            query.prepare(QStringLiteral(
                "select name, market, venue, start_time_utc, period_seconds, "
                "open, high, low, close, volume, buy_volume, sell_volume, "
                "neutral_volume, directional_volume_known "
                "from candles where symbol = ? and source = 'raw_tick_cache_v1' "
                "and close > 0 order by start_time_utc desc limit ?"));
            query.addBindValue(symbol);
            query.addBindValue(requestedBars);
            if (!query.exec()) {
                if (error) *error = QStringLiteral("读取 Tick 派生 K 线失败");
            } else {
                while (query.next()) {
                    pp::Candle candle;
                    candle.instrument.symbol = symbol;
                    candle.instrument.name = query.value(0).toString();
                    candle.instrument.market = pp::Market::AShare;
                    candle.instrument.venue = query.value(2).toString();
                    candle.startTime = QDateTime::fromString(
                        query.value(3).toString(), Qt::ISODateWithMs);
                    candle.periodSeconds = query.value(4).toInt();
                    candle.open = query.value(5).toDouble();
                    candle.high = query.value(6).toDouble();
                    candle.low = query.value(7).toDouble();
                    candle.close = query.value(8).toDouble();
                    candle.volume = query.value(9).toDouble();
                    candle.buyVolume = query.value(10).toDouble();
                    candle.sellVolume = query.value(11).toDouble();
                    candle.neutralVolume = query.value(12).toDouble();
                    candle.directionalVolumeKnown = query.value(13).toBool();
                    candle.source = QStringLiteral("raw_tick_cache_v1");
                    candles.prepend(candle);
                }
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    if (candles.isEmpty() && error && error->isEmpty()) {
        *error = QStringLiteral("没有找到 Tick 派生 K 线模板");
    }
    return candles;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("pptrader_backtest_capacity"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("d"), QStringLiteral("database")},
         QStringLiteral("SQLite database path"), QStringLiteral("path")},
        {{QStringLiteral("s"), QStringLiteral("symbol")},
         QStringLiteral("Template symbol"), QStringLiteral("symbol"),
         QStringLiteral("600519")},
        {{QStringLiteral("n"), QStringLiteral("symbols")},
         QStringLiteral("Number of independent symbols"), QStringLiteral("count"),
         QStringLiteral("100")},
        {{QStringLiteral("b"), QStringLiteral("bars")},
         QStringLiteral("Bars per symbol"), QStringLiteral("count"),
         QStringLiteral("239")},
        {{QStringLiteral("c"), QStringLiteral("concurrency")},
         QStringLiteral("Maximum active engines"), QStringLiteral("count"),
         QStringLiteral("1")}
    });
    parser.process(application);

    const QString databasePath = parser.value(QStringLiteral("database"));
    const QString templateSymbol = parser.value(QStringLiteral("symbol")).trimmed();
    const int symbolCount = std::max(1, parser.value(QStringLiteral("symbols")).toInt());
    const int requestedBars = std::max(20, parser.value(QStringLiteral("bars")).toInt());
    const int concurrency = std::clamp(
        parser.value(QStringLiteral("concurrency")).toInt(), 1, 128);
    if (databasePath.isEmpty()) {
        QTextStream(stderr) << "--database is required\n";
        return 2;
    }

    QString loadError;
    const QVector<pp::Candle> templateCandles = loadTemplate(
        databasePath, templateSymbol, requestedBars, &loadError);
    if (!loadError.isEmpty()) {
        QTextStream(stderr) << loadError << '\n';
        return 3;
    }

    QElapsedTimer totalTimer;
    totalTimer.start();
    QElapsedTimer heartbeatClock;
    heartbeatClock.start();
    QVector<qint64> heartbeatGaps;
    QVector<qint64> taskDurations;
    qint64 lastHeartbeat = 0;
    QTimer heartbeat;
    heartbeat.setInterval(16);
    QObject::connect(&heartbeat, &QTimer::timeout, &application, [&] {
        const qint64 now = heartbeatClock.elapsed();
        if (lastHeartbeat > 0) heartbeatGaps.append(now - lastHeartbeat);
        lastHeartbeat = now;
    });
    heartbeat.start();

    int nextTask = 0;
    int active = 0;
    int peakActive = 0;
    int completed = 0;
    int failed = 0;
    double finalEquityChecksum = 0.0;
    std::function<void()> fillSlots;
    fillSlots = [&] {
        while (active < concurrency && nextTask < symbolCount) {
            const int taskIndex = nextTask++;
            auto *engine = new pp::BacktestEngine(&application);
            QVector<pp::Candle> candles = templateCandles;
            pp::Instrument instrument = candles.first().instrument;
            instrument.symbol = QStringLiteral("%1").arg(
                taskIndex + 1, 6, 10, QLatin1Char('0'));
            instrument.name = QStringLiteral("容量标的 %1").arg(taskIndex + 1);
            for (pp::Candle &candle : candles) candle.instrument = instrument;

            pp::BacktestConfig config;
            config.taskId = QString::number(taskIndex + 1);
            config.strategyId = QStringLiteral("sma_crossover");
            config.instrument = instrument;
            config.frequency = QStringLiteral("1m");
            config.periodSeconds = candles.first().periodSeconds;
            config.requestedBars = candles.size();
            config.initialCapital = 100000.0;
            config.commissionRate = 0.0003;
            config.slippageRate = 0.0002;
            config.lotSize = 100;

            const qint64 startedAt = totalTimer.elapsed();
            ++active;
            peakActive = std::max(peakActive, active);
            QObject::connect(engine, &pp::BacktestEngine::finished,
                             &application, [&, engine, startedAt](const pp::PerformanceSummary &) {
                taskDurations.append(totalTimer.elapsed() - startedAt);
                finalEquityChecksum += engine->result().summary.finalEquity;
                --active;
                ++completed;
                engine->deleteLater();
                if (completed + failed >= symbolCount) application.quit();
                else QTimer::singleShot(0, &application, fillSlots);
            });
            QObject::connect(engine, &pp::BacktestEngine::errorOccurred,
                             &application, [&, engine, startedAt](const QString &) {
                taskDurations.append(totalTimer.elapsed() - startedAt);
                --active;
                ++failed;
                engine->deleteLater();
                if (completed + failed >= symbolCount) application.quit();
                else QTimer::singleShot(0, &application, fillSlots);
            });
            engine->start(config, std::move(candles));
        }
    };

    QTimer::singleShot(0, &application, fillSlots);
    application.exec();
    heartbeat.stop();
    const qint64 elapsed = totalTimer.elapsed();

    QJsonObject report;
    report.insert(QStringLiteral("symbols"), symbolCount);
    report.insert(QStringLiteral("barsPerSymbol"), templateCandles.size());
    report.insert(QStringLiteral("totalBars"),
                  static_cast<double>(symbolCount) * templateCandles.size());
    report.insert(QStringLiteral("concurrency"), concurrency);
    report.insert(QStringLiteral("peakActiveEngines"), peakActive);
    report.insert(QStringLiteral("completed"), completed);
    report.insert(QStringLiteral("failed"), failed);
    report.insert(QStringLiteral("elapsedMs"), static_cast<double>(elapsed));
    report.insert(QStringLiteral("symbolsPerSecond"), elapsed > 0
        ? symbolCount * 1000.0 / elapsed : 0.0);
    report.insert(QStringLiteral("barsPerSecond"), elapsed > 0
        ? static_cast<double>(symbolCount) * templateCandles.size() * 1000.0 / elapsed
        : 0.0);
    report.insert(QStringLiteral("taskP50Ms"), percentile(taskDurations, 0.50));
    report.insert(QStringLiteral("taskP95Ms"), percentile(taskDurations, 0.95));
    report.insert(QStringLiteral("heartbeatP95Ms"), percentile(heartbeatGaps, 0.95));
    report.insert(QStringLiteral("heartbeatMaxMs"),
                  heartbeatGaps.isEmpty() ? 0.0
                                          : static_cast<double>(*std::max_element(
                                                heartbeatGaps.cbegin(), heartbeatGaps.cend())));
    report.insert(QStringLiteral("logicalProcessors"), QThread::idealThreadCount());
    report.insert(QStringLiteral("resultChecksum"), finalEquityChecksum);
    report.insert(QStringLiteral("dataSource"), QStringLiteral("raw_tick_cache_v1 template"));
    QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << '\n';
    return failed == 0 ? 0 : 4;
}
