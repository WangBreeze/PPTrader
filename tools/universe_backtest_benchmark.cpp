#include "services/BacktestEngine.h"
#include "services/SqliteLocalDataStore.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QTimeZone>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>

#ifdef Q_OS_LINUX
#include <sys/resource.h>
#endif

namespace {

struct StockBatch {
    QString symbol;
    QVector<pp::BacktestResult> results;
    QString error;
};

const QStringList defaultStrategyIds {
    QStringLiteral("sma_crossover"), QStringLiteral("rsi_mean_reversion"),
    QStringLiteral("macd_trend"), QStringLiteral("bollinger_mean_reversion"),
    QStringLiteral("stoch_mean_reversion")
};

qint64 residentPeakBytes()
{
#ifdef Q_OS_LINUX
    rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) == 0) return qint64(usage.ru_maxrss) * 1024;
#endif
    return 0;
}

qint64 databaseBytes(const QString &path)
{
    return QFileInfo(path).size() + QFileInfo(path + QStringLiteral("-wal")).size()
        + QFileInfo(path + QStringLiteral("-shm")).size();
}

QStringList selectedDates(const QString &root, int dateCount, const QDate &endDate)
{
    QStringList dates;
    const QStringList folders = QDir(root).entryList(
        {QStringLiteral("????????+daily")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &folder : folders) {
        const QString date = folder.left(8);
        const QDate folderDate = QDate::fromString(date, QStringLiteral("yyyyMMdd"));
        if (folderDate.isValid() && (!endDate.isValid() || folderDate <= endDate))
            dates.append(folder);
    }
    return dates.mid(std::max<qsizetype>(0, dates.size() - dateCount));
}

QStringList commonSymbols(const QString &root, const QStringList &dates)
{
    QSet<QString> common;
    bool first = true;
    for (const QString &folder : dates) {
        QSet<QString> symbols;
        const QStringList files = QDir(root + QLatin1Char('/') + folder).entryList(
            {QStringLiteral("*.csv")}, QDir::Files, QDir::Name);
        for (const QString &file : files) symbols.insert(file.chopped(4));
        if (first) { common = symbols; first = false; }
        else common.intersect(symbols);
    }
    QStringList sorted = common.values();
    sorted.sort();
    return sorted;
}

bool readStock(const QString &root, const QStringList &dates, const QString &symbol,
               QVector<pp::Candle> *candles, QString *error)
{
    const QTimeZone zone("Asia/Shanghai");
    pp::Instrument instrument;
    instrument.symbol = symbol;
    instrument.name = symbol;
    instrument.market = pp::Market::AShare;
    instrument.venue = symbol.startsWith(QLatin1Char('6')) ? QStringLiteral("SSE")
        : (symbol.startsWith(QLatin1Char('8')) || symbol.startsWith(QLatin1Char('4')))
            ? QStringLiteral("BSE") : QStringLiteral("SZSE");
    candles->reserve(dates.size());
    for (const QString &folder : dates) {
        QFile file(root + QLatin1Char('/') + folder + QLatin1Char('/') + symbol
                   + QStringLiteral(".csv"));
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("无法读取 %1").arg(file.fileName());
            return false;
        }
        const QByteArray header = file.readLine().trimmed();
        const QList<QByteArray> row = file.readLine().trimmed().split(',');
        const QDate date = QDate::fromString(folder.left(8), QStringLiteral("yyyyMMdd"));
        bool ok[5] {};
        if (header != "time,open,close,high,low,volume" || row.size() != 6
            || !row.at(0).startsWith(date.toString(Qt::ISODate).toUtf8())
            || !file.readAll().trimmed().isEmpty()) {
            *error = QStringLiteral("日线格式异常：%1").arg(file.fileName());
            return false;
        }
        pp::Candle candle;
        candle.instrument = instrument;
        candle.startTime = QDateTime(date, QTime(15, 0), zone);
        candle.periodSeconds = 86400;
        candle.open = row.at(1).toDouble(&ok[0]);
        candle.close = row.at(2).toDouble(&ok[1]);
        candle.high = row.at(3).toDouble(&ok[2]);
        candle.low = row.at(4).toDouble(&ok[3]);
        candle.volume = row.at(5).toDouble(&ok[4]);
        candle.source = QStringLiteral("local_daily_csv");
        if (!std::all_of(std::begin(ok), std::end(ok), [](bool valid) { return valid; })
            || !std::isfinite(candle.open) || !std::isfinite(candle.close)
            || !std::isfinite(candle.high) || !std::isfinite(candle.low)
            || !std::isfinite(candle.volume) || candle.low <= 0.0
            || candle.high < std::max(candle.open, candle.close)
            || candle.low > std::min(candle.open, candle.close)
            || candle.volume < 0.0) {
            *error = QStringLiteral("日线数值异常：%1").arg(file.fileName());
            return false;
        }
        candles->append(candle);
    }
    return true;
}

StockBatch runStock(const QString &root, const QStringList &dates, const QString &symbol,
                    const QStringList &strategyIds)
{
    StockBatch batch;
    batch.symbol = symbol;
    QVector<pp::Candle> candles;
    if (!readStock(root, dates, symbol, &candles, &batch.error)) return batch;
    pp::BacktestConfig base;
    base.instrument = candles.first().instrument;
    base.requestedBars = dates.size();
    base.sampleFrom = candles.first().startTime;
    base.sampleTo = candles.last().startTime;
    const auto prepared = pp::BacktestEngine::prepareCandles(base, std::move(candles));
    for (const QString &id : strategyIds) {
        pp::BacktestConfig config = base;
        config.strategyId = id;
        config.taskId = QStringLiteral("universe:%1:%2").arg(symbol, id);
        pp::BacktestEngine engine;
        engine.setCandleObservationsPerTurn(512);
        engine.setObservationSignalsEnabled(false);
        QEventLoop loop;
        QString engineError;
        QObject::connect(&engine, &pp::BacktestEngine::finished,
                         &loop, &QEventLoop::quit);
        QObject::connect(&engine, &pp::BacktestEngine::errorOccurred,
                         &loop, [&](const QString &message) { engineError = message; loop.quit(); });
        if (!engine.startPreparedCandles(config, prepared)) {
            batch.error = QStringLiteral("%1: %2").arg(id, engineError);
            return batch;
        }
        loop.exec();
        if (!engineError.isEmpty()) {
            batch.error = QStringLiteral("%1: %2").arg(id, engineError);
            return batch;
        }
        batch.results.append(engine.result());
    }
    return batch;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("本地日线 5 策略全市场并行回测和完整归档压力测试"));
    parser.addHelpOption();
    QCommandLineOption dataOption(QStringLiteral("data-root"), QStringLiteral("只读日线目录"),
                                  QStringLiteral("path"));
    QCommandLineOption dbOption(QStringLiteral("database"), QStringLiteral("新建隔离归档库"),
                                QStringLiteral("path"));
    QCommandLineOption reportOption(QStringLiteral("report"), QStringLiteral("JSON 报告"),
                                    QStringLiteral("path"));
    QCommandLineOption threadsOption(QStringLiteral("threads"), QStringLiteral("计算线程数"),
                                     QStringLiteral("count"), QStringLiteral("1"));
    QCommandLineOption limitOption(QStringLiteral("limit"), QStringLiteral("股票数量上限，0 为全部"),
                                   QStringLiteral("count"), QStringLiteral("0"));
    QCommandLineOption daysOption(QStringLiteral("days"), QStringLiteral("末尾交易日数量"),
                                  QStringLiteral("count"), QStringLiteral("45"));
    QCommandLineOption endOption(QStringLiteral("end-date"), QStringLiteral("固定样本截止日期 YYYY-MM-DD"),
                                 QStringLiteral("date"));
    QCommandLineOption strategyOption(QStringLiteral("strategies"),
        QStringLiteral("五个以逗号分隔的内置日 K 策略 ID"), QStringLiteral("ids"),
        defaultStrategyIds.join(QLatin1Char(',')));
    QCommandLineOption computeOnlyOption(QStringLiteral("compute-only"),
        QStringLiteral("只测策略计算，不写入回测归档"));
    parser.addOptions({dataOption, dbOption, reportOption, threadsOption, limitOption,
                       daysOption, endOption, strategyOption, computeOnlyOption});
    parser.process(application);
    bool threadsOk = false, limitOk = false, daysOk = false;
    const int threadCount = parser.value(threadsOption).toInt(&threadsOk);
    const int limit = parser.value(limitOption).toInt(&limitOk);
    const int days = parser.value(daysOption).toInt(&daysOk);
    const QDate endDate = QDate::fromString(parser.value(endOption), Qt::ISODate);
    const QString root = parser.value(dataOption);
    const QString dbPath = parser.value(dbOption);
    const QString reportPath = parser.value(reportOption);
    const bool computeOnly = parser.isSet(computeOnlyOption);
    const QStringList strategyIds = parser.value(strategyOption).split(
        QLatin1Char(','), Qt::SkipEmptyParts);
    pp::BacktestStrategyRegistry registry;
    QSet<QString> distinctStrategies;
    for (const QString &id : strategyIds) {
        distinctStrategies.insert(id);
        if (!registry.supportsDataMode(id, QStringLiteral("candles"))) {
            QTextStream(stderr) << "不支持日 K 策略：" << id << '\n';
            return 2;
        }
    }
    if (!threadsOk || threadCount < 1 || threadCount > 64 || !limitOk || limit < 0
        || !daysOk || days < 35 || root.isEmpty()
        || (!computeOnly && (dbPath.isEmpty() || QFileInfo::exists(dbPath)))
        || reportPath.isEmpty()
        || (parser.isSet(endOption) && !endDate.isValid())
        || strategyIds.size() != 5 || distinctStrategies.size() != 5) {
        QTextStream(stderr) << "参数无效或目标库已存在\n";
        return 2;
    }
    const QStringList dates = selectedDates(root, days, endDate);
    if (dates.size() != days) {
        QTextStream(stderr) << "交易日目录不足\n";
        return 2;
    }
    const QStringList common = commonSymbols(root, dates);
    const int eligibleCount = common.size();
    const QString symbolsSha256 = QString::fromLatin1(QCryptographicHash::hash(
        common.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex());
    QElapsedTimer preflightTimer;
    preflightTimer.start();
    QStringList symbols;
    QJsonArray excludedInputs;
    for (const QString &symbol : common) {
        QVector<pp::Candle> candles;
        QString error;
        if (readStock(root, dates, symbol, &candles, &error)) symbols.append(symbol);
        else excludedInputs.append(QJsonObject{{QStringLiteral("symbol"), symbol},
                                                {QStringLiteral("reason"), error}});
    }
    const qint64 preflightMs = preflightTimer.elapsed();
    const int validCount = symbols.size();
    if (limit > 0) symbols = symbols.mid(0, limit);
    if (symbols.isEmpty()) return 2;
    std::unique_ptr<pp::SqliteLocalDataStore> store;
    if (!computeOnly) store = std::make_unique<pp::SqliteLocalDataStore>();
    if (store && !store->initialize(dbPath)) {
        QTextStream(stderr) << "无法初始化独立回测库\n";
        return 2;
    }
    const qint64 initialBytes = store ? databaseBytes(dbPath) : 0;
    std::atomic<int> next {0};
    std::atomic<int> active {0};
    std::atomic<int> maximumActive {0};
    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable space;
    std::deque<StockBatch> queue;
    int finishedWorkers = 0;
    QVector<QString> failures;
    int saved = 0;
    int completedTasks = 0;
    int completedStocks = 0;
    QElapsedTimer timer;
    timer.start();
    std::vector<std::thread> workers;
    for (int worker = 0; worker < threadCount; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const int index = next.fetch_add(1);
                if (index >= symbols.size()) break;
                const int now = ++active;
                int observed = maximumActive.load();
                while (now > observed && !maximumActive.compare_exchange_weak(observed, now)) {}
                StockBatch batch = runStock(root, dates, symbols.at(index), strategyIds);
                --active;
                std::unique_lock lock(mutex);
                space.wait(lock, [&] { return queue.size() < size_t(threadCount * 2); });
                queue.push_back(std::move(batch));
                lock.unlock();
                ready.notify_one();
            }
            std::lock_guard lock(mutex);
            ++finishedWorkers;
            ready.notify_one();
        });
    }
    while (true) {
        std::unique_lock lock(mutex);
        ready.wait(lock, [&] { return !queue.empty() || finishedWorkers == threadCount; });
        if (queue.empty()) break;
        StockBatch batch = std::move(queue.front());
        queue.pop_front();
        lock.unlock();
        space.notify_one();
        if (!batch.error.isEmpty()) {
            failures.append(batch.symbol + QStringLiteral(": ") + batch.error);
            continue;
        }
        if (computeOnly) {
            completedTasks += batch.results.size();
            ++completedStocks;
            continue;
        }
        QString owner;
        bool stockSaved = true;
        for (const auto &result : batch.results) {
            pp::BacktestArchiveRecord record;
            record.resultId = result.config.taskId;
            record.completedAt = QDateTime::currentDateTimeUtc();
            record.result = result;
            record.sharedCandleResultId = owner;
            if (!store->saveBacktestArchive(record)) {
                failures.append(batch.symbol + QStringLiteral(": 归档失败 ")
                                + result.config.strategyId);
                stockSaved = false;
                break;
            }
            if (owner.isEmpty()) owner = record.resultId;
            ++saved;
            ++completedTasks;
        }
        if (stockSaved) ++completedStocks;
        if ((completedStocks + failures.size()) % 500 == 0)
            QTextStream(stderr) << "已处理 " << completedStocks + failures.size()
                                << '/' << symbols.size() << "\n";
    }
    for (auto &worker : workers) worker.join();
    const qint64 elapsedMs = timer.elapsed();
    if (store) store->close();
    const qint64 finalBytes = store ? databaseBytes(dbPath) : 0;
    QJsonArray errorArray;
    for (const QString &failure : failures) errorArray.append(failure);
    const QJsonObject report {
        {QStringLiteral("dataRoot"), root}, {QStringLiteral("database"), dbPath},
        {QStringLiteral("archiveMode"), computeOnly
            ? QStringLiteral("none") : QStringLiteral("full")},
        {QStringLiteral("firstDate"), dates.first().left(8)},
        {QStringLiteral("lastDate"), dates.last().left(8)},
        {QStringLiteral("dateFolders"), QJsonArray::fromStringList(dates)},
        {QStringLiteral("eligibleSymbolsSha256"), symbolsSha256},
        {QStringLiteral("sessions"), days},
        {QStringLiteral("eligibleStocks"), eligibleCount},
        {QStringLiteral("validStocks"), validCount},
        {QStringLiteral("excludedInputs"), excludedInputs},
        {QStringLiteral("preflightMs"), preflightMs},
        {QStringLiteral("selectedStocks"), symbols.size()},
        {QStringLiteral("strategies"), QJsonArray::fromStringList(strategyIds)},
        {QStringLiteral("requestedTasks"), symbols.size() * strategyIds.size()},
        {QStringLiteral("completedTasks"), completedTasks},
        {QStringLiteral("savedTasks"), saved},
        {QStringLiteral("completedStocks"), completedStocks},
        {QStringLiteral("threads"), threadCount},
        {QStringLiteral("maximumActiveCompute"), maximumActive.load()},
        {QStringLiteral("elapsedMs"), elapsedMs},
        {QStringLiteral("tasksPerSecond"), elapsedMs > 0
            ? completedTasks * 1000.0 / elapsedMs : 0.0},
        {QStringLiteral("initialDatabaseBytes"), initialBytes},
        {QStringLiteral("finalDatabaseBytes"), finalBytes},
        {QStringLiteral("databaseGrowthBytes"), finalBytes - initialBytes},
        {QStringLiteral("peakResidentBytes"), residentPeakBytes()},
        {QStringLiteral("failures"), errorArray}
    };
    QSaveFile output(reportPath);
    if (!QDir().mkpath(QFileInfo(reportPath).absolutePath())
        || !output.open(QIODevice::WriteOnly)) return 3;
    output.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    if (!output.commit()) return 3;
    QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact) << '\n';
    return failures.isEmpty() && completedTasks == symbols.size() * strategyIds.size()
        && (computeOnly || saved == completedTasks) ? 0 : 1;
}
