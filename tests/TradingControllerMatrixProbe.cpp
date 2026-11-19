#include "app/StrategyMatrixContractProbe.h"

#include "app/TradingController.h"
#include "services/SqliteLocalDataStore.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantMap>
#include <QDebug>

#include <memory>

namespace {

bool waitForMatrix(pp::TradingController *controller, int timeoutMilliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (controller->strategyMatrixRunning()
           && timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
    return !controller->strategyMatrixRunning();
}

bool waitForCompaction(pp::TradingController *controller, int timeoutMilliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (controller->backtestArchiveCompactionRunning()
           && timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
    return !controller->backtestArchiveCompactionRunning();
}

int runRawMatrixContractProbe(const QString &sourcePath)
{
    const QDateTime sourceModified = QFileInfo(sourcePath).lastModified();
    pp::SqliteLocalDataStore source;
    if (!source.initializeReadOnly(sourcePath)) return 30;
    QTemporaryDir directory;
    if (!directory.isValid()) return 31;
    const QString path = directory.filePath(QStringLiteral("matrix-raw.db"));
    pp::SqliteLocalDataStore target;
    if (!target.initialize(path)) return 32;
    const QDate firstDay(2026, 7, 20);
    const QDate lastDay(2026, 7, 21);
    for (const QString &symbol : {QStringLiteral("600519"),
                                  QStringLiteral("000001")}) {
        const pp::Instrument instrument {symbol, symbol, pp::Market::AShare,
            symbol.startsWith(QLatin1Char('6'))
                ? QStringLiteral("XSHG") : QStringLiteral("XSHE")};
        for (const QDate day : {firstDay, lastDay}) {
            QVector<pp::RawTradeTick> ticks;
            if (!source.forEachRawTradeTick(instrument, day,
                    [&ticks](const pp::RawTradeTick &tick) {
                        ticks.append(tick);
                        return true;
                    }) || ticks.isEmpty()) return 33;
            if (!target.beginRawTradeTickImport(
                    instrument, day, QStringLiteral("tickdata_csv_tick"))
                || !target.appendRawTradeTicks(ticks)) return 34;
            pp::TickDataImportResult audit;
            audit.succeeded = true;
            audit.linesRead = ticks.size();
            audit.validRecords = ticks.size();
            audit.qualityStatus = QStringLiteral("healthy");
            if (!target.commitRawTradeTickImport(audit)) return 35;
        }
    }
    target.close();
    qputenv("PPTRADER_DATABASE_PATH", path.toLocal8Bit());
    auto controller = std::make_unique<pp::TradingController>();
    static constexpr auto definition = R"({
      "schemaVersion":2,"strategyId":"custom_matrix_probe_raw","version":1,
      "name":"原始逐笔矩阵控制器探针","description":"验证批次恢复","author":"Qt Test",
      "market":"A股","executionScope":"backtest_only",
      "createdAt":"2026-11-13T00:00:00.000Z",
      "updatedAt":"2026-11-13T00:00:00.000Z",
      "parameters":[{"key":"period","displayName":"成交笔数窗口",
        "type":"integer","defaultValue":5,"minimumValue":2,
        "maximumValue":12,"step":1}],
      "entry":{"op":"gt","left":{"indicator":"SMA",
        "period":{"parameter":"period"}},"right":{"value":0}},
      "exit":{"op":"gt","left":{"field":"close"},"right":{"value":0}},
      "signal":{"priceMode":"nextOpen"}
    })";
    if (!controller->saveCustomStrategyJson(QString::fromUtf8(definition)))
        return 36;
    const QStringList symbols {QStringLiteral("600519"), QStringLiteral("000001")};
    const QString first = firstDay.toString(Qt::ISODate);
    const QString last = lastDay.toString(Qt::ISODate);
    const QVariantList preflight = controller->preflightStrategyMatrix(
        symbols, QStringLiteral("tick"), first, last);
    if (preflight.size() != 2) return 37;
    for (const QVariant &value : preflight) {
        if (!value.toMap().value(QStringLiteral("eligible")).toBool()) {
            qWarning().noquote() << value.toMap();
            return 38;
        }
    }
    const QVariantList strategies {
        QVariantMap {{QStringLiteral("strategyId"),
                      QStringLiteral("custom_matrix_probe_raw")},
                     {QStringLiteral("version"), 1},
                     {QStringLiteral("profileLabel"), QStringLiteral("SMA5")},
                     {QStringLiteral("parameters"), QVariantMap {
                         {QStringLiteral("period"), 5}}}},
        QVariantMap {{QStringLiteral("strategyId"),
                      QStringLiteral("custom_matrix_probe_raw")},
                     {QStringLiteral("version"), 1},
                     {QStringLiteral("profileLabel"), QStringLiteral("SMA8")},
                     {QStringLiteral("parameters"), QVariantMap {
                         {QStringLiteral("period"), 8}}}}
    };
    const QString runId = controller->runStrategyMatrix(
        strategies, symbols, QStringLiteral("tick"), first, last);
    if (runId.isEmpty()) {
        qWarning().noquote() << controller->strategyMatrixError();
        return 39;
    }
    if (!waitForMatrix(controller.get(), 30000)) return 40;
    const QVariantList results = controller->strategyMatrixResults();
    if (results.size() != 4) return 41;
    for (const QVariant &value : results) {
        const QVariantMap item = value.toMap();
        if (!item.value(QStringLiteral("succeeded")).toBool()
            || item.value(QStringLiteral("resultId")).toString().isEmpty()) {
            qWarning().noquote() << item;
            return 42;
        }
    }
    controller->shutdown();
    controller.reset();
    pp::SqliteLocalDataStore check;
    if (!check.initialize(path)) return 43;
    const auto saved = check.loadStrategyMatrixRun(runId);
    if (!saved || saved->status != QStringLiteral("completed")) return 44;
    auto tasks = check.loadStrategyMatrixTasks(runId);
    if (tasks.size() != 4) return 45;
    tasks.first().status = QStringLiteral("failed");
    tasks.first().resultId.clear();
    if (!check.upsertStrategyMatrixTask(tasks.first())) return 46;
    check.close();
    auto reopened = std::make_unique<pp::TradingController>();
    if (!reopened->loadStrategyMatrixRun(runId)) return 47;
    if (reopened->resumeStrategyMatrixRun(runId) != runId) {
        qWarning().noquote() << reopened->strategyMatrixError();
        return 48;
    }
    if (reopened->strategyMatrixRunning()
        || reopened->strategyMatrixCompleted() != 4) return 49;
    reopened->shutdown();
    reopened.reset();
    if (QFileInfo(sourcePath).lastModified() != sourceModified) return 50;
    qInfo().noquote() << QStringLiteral(
        "raw_matrix_controller_probe run=%1 tasks=4 restored=4 source_read_only=1")
                             .arg(runId);
    return 0;
}

} // namespace

int runStrategyMatrixContractProbe()
{
    const QString rawSource = qEnvironmentVariable("PPTRADER_RAW_MATRIX_SOURCE_DB");
    if (!rawSource.isEmpty()) return runRawMatrixContractProbe(rawSource);
    const QString path = qEnvironmentVariable("PPTRADER_MATRIX_PROBE_DB");
    if (!QFileInfo::exists(path)) {
        qWarning() << "matrix probe requires an existing isolated database";
        return 2;
    }
    qputenv("PPTRADER_DATABASE_PATH", path.toLocal8Bit());
    auto controller = std::make_unique<pp::TradingController>();
    const QStringList symbols {QStringLiteral("000001"), QStringLiteral("600519")};
    const QString first = QStringLiteral("2026-07-20");
    const QString last = QStringLiteral("2026-11-09");
    const QVariantList preflight = controller->preflightStrategyMatrix(
        symbols, QStringLiteral("day"), first, last);
    if (preflight.size() != symbols.size()) return 3;
    for (const QVariant &row : preflight) {
        if (!row.toMap().value(QStringLiteral("eligible")).toBool()) {
            qWarning().noquote() << row.toMap();
            return 4;
        }
    }
    const QVariantList strategies {
        QVariantMap {{QStringLiteral("strategyId"), QStringLiteral("sma_crossover")}},
        QVariantMap {{QStringLiteral("strategyId"), QStringLiteral("macd_trend")}}};
    const QString runId = controller->runStrategyMatrix(
        strategies, symbols, QStringLiteral("day"), first, last);
    if (runId.isEmpty()) {
        qWarning().noquote() << controller->strategyMatrixError();
        return 5;
    }
    if (!waitForMatrix(controller.get(), 120000)) return 6;
    const QVariantList results = controller->strategyMatrixResults();
    if (results.size() != 4) return 7;
    for (const QVariant &value : results) {
        const QVariantMap item = value.toMap();
        if (!item.value(QStringLiteral("succeeded")).toBool()
            || item.value(QStringLiteral("resultId")).toString().isEmpty()) {
            qWarning().noquote() << item;
            return 8;
        }
    }
    controller->shutdown();
    controller.reset();

    pp::SqliteLocalDataStore store;
    if (!store.initialize(path)) return 9;
    const auto persisted = store.loadStrategyMatrixRun(runId);
    if (!persisted || persisted->status != QStringLiteral("completed")) return 10;
    auto tasks = store.loadStrategyMatrixTasks(runId);
    if (tasks.size() != 4) return 11;
    pp::StrategyMatrixTaskRecord interrupted = tasks.first();
    interrupted.status = QStringLiteral("failed");
    interrupted.error = QStringLiteral("模拟：归档后状态写入中断");
    interrupted.resultId.clear();
    if (!store.upsertStrategyMatrixTask(interrupted)) return 12;
    store.close();

    auto reopened = std::make_unique<pp::TradingController>();
    if (!reopened->loadStrategyMatrixRun(runId)) return 13;
    if (reopened->strategyMatrixResults().size() != 4) return 14;
    if (reopened->resumeStrategyMatrixRun(runId) != runId) {
        qWarning().noquote() << reopened->strategyMatrixError();
        return 15;
    }
    if (reopened->strategyMatrixRunning()) return 16;
    if (reopened->strategyMatrixCompleted() != 4) return 17;
    reopened->shutdown();
    reopened.reset();

    pp::SqliteLocalDataStore verification;
    if (!verification.initialize(path)) return 18;
    tasks = verification.loadStrategyMatrixTasks(runId);
    if (tasks.size() != 4) return 19;
    for (const auto &task : tasks) {
        if (task.status != QStringLiteral("succeeded")
            || task.resultId.isEmpty()) return 20;
    }
    verification.close();
    if (qEnvironmentVariable("PPTRADER_MATRIX_PROBE_COMPACT")
        == QStringLiteral("1")) {
        auto maintenance = std::make_unique<pp::TradingController>();
        if (!maintenance->deleteStrategyMatrixRun(runId)) {
            qWarning().noquote() << maintenance->strategyMatrixError();
            return 21;
        }
        if (!maintenance->compactBacktestArchiveStorage()) {
            qWarning().noquote() << maintenance->backtestArchiveCompactionError();
            return 22;
        }
        if (!waitForCompaction(maintenance.get(), 120000)) return 23;
        if (!maintenance->backtestArchiveCompactionError().isEmpty()) {
            qWarning().noquote() << maintenance->backtestArchiveCompactionError();
            return 24;
        }
        if (maintenance->backtestArchiveCount() != 4) return 25;
        maintenance->shutdown();
    }
    qInfo().noquote() << QStringLiteral(
        "matrix_controller_probe run=%1 tasks=4 restored=4 orphan_reconciled=1 compact=%2")
                             .arg(runId, qEnvironmentVariable("PPTRADER_MATRIX_PROBE_COMPACT"));
    return 0;
}
