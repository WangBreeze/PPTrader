#include "app/VisibleBacktestLongrunCoordinator.h"

#include "app/BatchBacktestEvidence.h"
#include "app/TradingController.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSaveFile>
#include <QWindow>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace pp {

namespace {

int boundedEnvironmentInteger(const char *name, int fallback, int minimum,
                              int maximum, bool allowZero = false)
{
    bool valid = false;
    const int value = qEnvironmentVariableIntValue(name, &valid);
    if (!valid || (allowZero && value == 0)) return valid ? value : fallback;
    return std::clamp(value, minimum, maximum);
}

QString utcNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

} // namespace

bool VisibleBacktestLongrunCoordinator::optionsFromEnvironment(
    Options *options, QString *error)
{
    if (!options) return false;
    Options parsed;
    parsed.outputDirectory = qEnvironmentVariable(
        "PPTRADER_BACKTEST_LONGRUN_OUTPUT_DIRECTORY").trimmed();
    parsed.databasePath = qEnvironmentVariable("PPTRADER_DATABASE_PATH").trimmed();
    parsed.startDate = QDate::fromString(
        qEnvironmentVariable("PPTRADER_BACKTEST_LONGRUN_START_DATE").trimmed(),
        Qt::ISODate);
    parsed.endDate = QDate::fromString(
        qEnvironmentVariable("PPTRADER_BACKTEST_LONGRUN_END_DATE").trimmed(),
        Qt::ISODate);
    parsed.durationSeconds = boundedEnvironmentInteger(
        "PPTRADER_BACKTEST_LONGRUN_SECONDS", 14'400, 1, 28'800);
    parsed.maximumSymbols = boundedEnvironmentInteger(
        "PPTRADER_BACKTEST_LONGRUN_MAX_SYMBOLS", 10, 1, 10'000);
    parsed.maximumRounds = boundedEnvironmentInteger(
        "PPTRADER_BACKTEST_LONGRUN_MAX_ROUNDS", 0, 1, 1'000'000, true);
    parsed.watchdogSeconds = boundedEnvironmentInteger(
        "PPTRADER_BACKTEST_LONGRUN_WATCHDOG_SECONDS", 120, 1, 600);

    QString validationError;
    if (parsed.outputDirectory.isEmpty()) {
        validationError = QStringLiteral("未配置回测长稳证据目录");
    } else if (parsed.databasePath.isEmpty()) {
        validationError = QStringLiteral("未配置本地隔离 SQLite 路径");
    } else if (!QFileInfo(parsed.databasePath).isAbsolute()
               || !QFileInfo::exists(parsed.databasePath)) {
        validationError = QStringLiteral("本地隔离 SQLite 路径无效");
    } else if (parsed.databasePath.startsWith(QStringLiteral("\\\\"))
               || parsed.databasePath.startsWith(QStringLiteral("//"))) {
        validationError = QStringLiteral("回测长稳禁止直接打开网络共享数据库");
    } else if (!parsed.startDate.isValid() || !parsed.endDate.isValid()
               || parsed.startDate > parsed.endDate) {
        validationError = QStringLiteral("回测长稳日期范围无效");
    }
    if (!validationError.isEmpty()) {
        if (error) *error = validationError;
        return false;
    }
    parsed.outputDirectory = QDir(parsed.outputDirectory).absolutePath();
    parsed.databasePath = QFileInfo(parsed.databasePath).absoluteFilePath();
    *options = parsed;
    if (error) error->clear();
    return true;
}

VisibleBacktestLongrunCoordinator::VisibleBacktestLongrunCoordinator(
    TradingController *controller, QObject *rootObject, QWindow *window,
    Options options, QObject *parent)
    : QObject(parent),
      m_controller(controller),
      m_rootObject(rootObject),
      m_window(window),
      m_options(std::move(options))
{
    m_strategyParameters = {
        {QStringLiteral("lookbackTicks"), 20},
        {QStringLiteral("thresholdPercent"), 0.2},
        {QStringLiteral("cooldownSeconds"), 30},
        {QStringLiteral("minimumHoldSeconds"), 60}
    };
    m_pollTimer.setInterval(100);
    m_pollTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_pollTimer, &QTimer::timeout, this,
            &VisibleBacktestLongrunCoordinator::poll);
    m_watchdogTimer.setSingleShot(true);
    connect(&m_watchdogTimer, &QTimer::timeout, this, [this]() {
        ++m_watchdogTimeouts;
        fail(QStringLiteral("单轮回测超过 %1 秒 watchdog")
                 .arg(m_options.watchdogSeconds));
    });
    m_cancellationGraceTimer.setSingleShot(true);
    connect(&m_cancellationGraceTimer, &QTimer::timeout, this, [this]() {
        finalize(QStringLiteral("failed"),
                 m_pendingFailure
                     + QStringLiteral("；取消后 30 秒仍未排空后台任务"));
    });
}

void VisibleBacktestLongrunCoordinator::start()
{
    if (m_finalized || m_totalElapsed.isValid()) return;
    if (!m_controller || !m_rootObject || !m_window) {
        fail(QStringLiteral("回测长稳启动时应用对象不可用"));
        return;
    }
    QDir outputDirectory(m_options.outputDirectory);
    if (!outputDirectory.exists() && !outputDirectory.mkpath(QStringLiteral("."))) {
        fail(QStringLiteral("无法创建回测长稳证据目录"));
        return;
    }
    m_reportPath = outputDirectory.filePath(
        QStringLiteral("raw-tick-backtest-app-longrun.json"));
    m_roundsPath = outputDirectory.filePath(QStringLiteral("rounds.jsonl"));
    if (QFileInfo::exists(m_reportPath) || QFileInfo::exists(m_roundsPath)) {
        m_finalized = true;
        qCritical().noquote()
            << QStringLiteral("证据目录已包含本次运行文件，拒绝覆盖");
        m_window->setTitle(QStringLiteral("PPTrader · 回测长稳拒绝覆盖既有证据"));
        QTimer::singleShot(250, QCoreApplication::instance(),
                           &QCoreApplication::quit);
        return;
    }

    m_startedAt = utcNow();
    m_initialCapital = m_controller->backtestConfiguredInitialCapital();
    m_commissionRate = m_controller->backtestCommissionRate();
    m_slippageRate = m_controller->backtestSlippageRate();
    m_totalElapsed.start();
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
    QVariant workspaceIndex = 3;
    if (!QMetaObject::invokeMethod(m_rootObject, "showBatchBacktestPanel",
                                   Qt::DirectConnection,
                                   Q_ARG(QVariant, workspaceIndex))) {
        fail(QStringLiteral("无法切换到回测任务页面"));
        return;
    }
    if (!writeReport(QStringLiteral("running"))) {
        fail(QStringLiteral("无法写入回测长稳初始证据"));
        return;
    }
    m_pollTimer.start();
    launchNextRound();
}

void VisibleBacktestLongrunCoordinator::finishInterrupted()
{
    if (!m_finalized)
        finalize(QStringLiteral("interrupted"),
                 QStringLiteral("应用在计划结束前退出"));
}

void VisibleBacktestLongrunCoordinator::launchNextRound()
{
    if (m_finalized || !m_controller || m_active) return;
    if (m_totalElapsed.elapsed() >= m_options.durationSeconds * 1000LL
        || (m_options.maximumRounds > 0
            && m_roundsCompleted >= m_options.maximumRounds)) {
        finalize(QStringLiteral("completed"));
        return;
    }
    if (!qFuzzyCompare(m_controller->backtestConfiguredInitialCapital() + 1.0,
                       m_initialCapital + 1.0)
        || !qFuzzyCompare(m_controller->backtestCommissionRate() + 1.0,
                          m_commissionRate + 1.0)
        || !qFuzzyCompare(m_controller->backtestSlippageRate() + 1.0,
                          m_slippageRate + 1.0)) {
        fail(QStringLiteral("长稳期间回测资金、费率或滑点配置发生变化"));
        return;
    }

    m_active = true;
    m_roundElapsed.restart();
    m_activeRequestId = m_controller->runLocalBatchBacktestRange(
        QStringLiteral("tick_momentum"), m_strategyParameters,
        QStringLiteral("raw_ticks"), QString{},
        m_options.startDate.toString(Qt::ISODate),
        m_options.endDate.toString(Qt::ISODate), m_options.maximumSymbols);
    if (m_activeRequestId == 0) {
        m_active = false;
        fail(QStringLiteral("批量回测未能启动：%1")
                 .arg(m_controller->batchBacktestError()));
        return;
    }
    m_watchdogTimer.start(m_options.watchdogSeconds * 1000);
    updateWindowTitle();
}

void VisibleBacktestLongrunCoordinator::poll()
{
    if (m_finalized || !m_controller) return;
    updateWindowTitle();
    if (m_cancelling) {
        if (!m_controller->batchBacktestRunning()) {
            m_cancellationGraceTimer.stop();
            finalize(QStringLiteral("failed"), m_pendingFailure);
        }
        return;
    }
    if (!m_active) return;
    if (m_controller->batchBacktestRequestId() != m_activeRequestId) {
        fail(QStringLiteral("批量回测请求代次被意外替换"));
        return;
    }
    if (m_controller->batchBacktestRunning()) return;
    if (m_controller->batchBacktestRequestedCount() <= 0
        || m_controller->batchBacktestCompletedCount()
            < m_controller->batchBacktestRequestedCount()) {
        fail(QStringLiteral("批量回测提前结束：完成 %1/%2；%3")
                 .arg(m_controller->batchBacktestCompletedCount())
                 .arg(m_controller->batchBacktestRequestedCount())
                 .arg(m_controller->batchBacktestError()));
        return;
    }
    QAbstractItemModel *model = m_controller->batchBacktestModel();
    if (m_controller->batchBacktestCompletedCount()
            != m_controller->batchBacktestRequestedCount()
        || m_controller->batchBacktestSucceededCount()
                + m_controller->batchBacktestFailedCount()
            != m_controller->batchBacktestRequestedCount()
        || !model
        || model->rowCount() != m_controller->batchBacktestRequestedCount()) {
        fail(QStringLiteral("批量回测完成计数或结果模型不一致"));
        return;
    }
    completeRound();
}

void VisibleBacktestLongrunCoordinator::completeRound()
{
    if (!m_controller || !m_active) return;
    m_watchdogTimer.stop();
    m_active = false;
    ++m_roundsCompleted;
    const qint64 duration = m_roundElapsed.elapsed();
    m_roundDurations.append(duration);
    const int requested = m_controller->batchBacktestRequestedCount();
    const int completed = m_controller->batchBacktestCompletedCount();
    const int succeeded = m_controller->batchBacktestSucceededCount();
    const int failed = m_controller->batchBacktestFailedCount();
    m_totalTasksCompleted += completed;
    m_totalTasksFailed += failed;

    QAbstractItemModel *model = m_controller->batchBacktestModel();
    const QJsonArray rows = batchBacktestEvidence::canonicalRows(model);
    const QString digest = batchBacktestEvidence::canonicalDigest(model);
    const qint64 rawTickCount = batchBacktestEvidence::totalRawTickCount(model);
    if (m_firstDigest.isEmpty()) {
        m_firstDigest = digest;
        m_firstRows = rows;
    } else if (digest != m_firstDigest) {
        m_deterministic = false;
    }
    m_lastDigest = digest;
    m_lastRows = rows;
    m_minRawTickCount = m_minRawTickCount < 0
        ? rawTickCount : std::min(m_minRawTickCount, rawTickCount);
    m_maxRawTickCount = std::max(m_maxRawTickCount, rawTickCount);

    QJsonObject round{
        {QStringLiteral("round"), m_roundsCompleted},
        {QStringLiteral("requestId"), QString::number(m_activeRequestId)},
        {QStringLiteral("completedAt"), utcNow()},
        {QStringLiteral("durationMilliseconds"), duration},
        {QStringLiteral("requested"), requested},
        {QStringLiteral("completed"), completed},
        {QStringLiteral("succeeded"), succeeded},
        {QStringLiteral("failed"), failed},
        {QStringLiteral("totalRawTickCount"), rawTickCount},
        {QStringLiteral("resultDigest"), digest},
        {QStringLiteral("controllerError"), m_controller->batchBacktestError()}
    };
    if (!appendRound(round) || !writeReport(QStringLiteral("running"))) {
        fail(QStringLiteral("回测长稳证据写入失败"));
        return;
    }
    if (failed > 0 || succeeded != requested || completed != requested) {
        fail(QStringLiteral("第 %1 轮未达到 %2/%2 成功：成功 %3，失败 %4")
                 .arg(m_roundsCompleted).arg(requested).arg(succeeded).arg(failed));
        return;
    }
    QTimer::singleShot(50, this,
                       &VisibleBacktestLongrunCoordinator::launchNextRound);
}

void VisibleBacktestLongrunCoordinator::fail(const QString &message)
{
    if (!m_finalized && m_controller && m_controller->batchBacktestRunning()) {
        m_pendingFailure = message;
        m_cancelling = true;
        m_watchdogTimer.stop();
        m_controller->cancelLocalBatchBacktest();
        m_cancellationGraceTimer.start(30'000);
        return;
    }
    finalize(QStringLiteral("failed"), message);
}

void VisibleBacktestLongrunCoordinator::finalize(const QString &status,
                                                  const QString &error)
{
    if (m_finalized) return;
    m_finalized = true;
    m_completedAt = utcNow();
    m_pollTimer.stop();
    m_watchdogTimer.stop();
    m_cancellationGraceTimer.stop();
    writeReport(status, error);
    if (m_window) {
        m_window->setTitle(status == QStringLiteral("completed")
            ? QStringLiteral("PPTrader · 原始 Tick 回测长稳完成")
            : QStringLiteral("PPTrader · 原始 Tick 回测长稳异常"));
    }
    if (QCoreApplication::closingDown()) return;
    QTimer::singleShot(250, QCoreApplication::instance(),
                       &QCoreApplication::quit);
}

bool VisibleBacktestLongrunCoordinator::appendRound(const QJsonObject &round)
{
    QFile file(m_roundsPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;
    const QByteArray line = QJsonDocument(round).toJson(QJsonDocument::Compact)
        + '\n';
    return file.write(line) == line.size() && file.flush();
}

bool VisibleBacktestLongrunCoordinator::writeReport(const QString &status,
                                                     const QString &error)
{
    if (m_reportPath.isEmpty()) return false;
    QJsonObject report{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("acceptanceType"),
         QStringLiteral("visible_pptrader_raw_tick_batch_longrun")},
        {QStringLiteral("status"), status},
        {QStringLiteral("error"), error},
        {QStringLiteral("startedAt"), m_startedAt},
        {QStringLiteral("completedAt"), m_completedAt},
        {QStringLiteral("requestedDurationSeconds"), m_options.durationSeconds},
        {QStringLiteral("elapsedMilliseconds"),
         m_totalElapsed.isValid() ? m_totalElapsed.elapsed() : 0},
        {QStringLiteral("strategyId"), QStringLiteral("tick_momentum")},
        {QStringLiteral("strategyParameters"),
         QJsonObject::fromVariantMap(m_strategyParameters)},
        {QStringLiteral("initialCapital"), m_initialCapital},
        {QStringLiteral("commissionRate"), m_commissionRate},
        {QStringLiteral("slippageRate"), m_slippageRate},
        {QStringLiteral("lotSize"), 100},
        {QStringLiteral("dataMode"), QStringLiteral("raw_ticks")},
        {QStringLiteral("frequency"), QStringLiteral("tick")},
        {QStringLiteral("startDate"), m_options.startDate.toString(Qt::ISODate)},
        {QStringLiteral("endDate"), m_options.endDate.toString(Qt::ISODate)},
        {QStringLiteral("maximumSymbols"), m_options.maximumSymbols},
        {QStringLiteral("maximumRounds"), m_options.maximumRounds},
        {QStringLiteral("watchdogSeconds"), m_options.watchdogSeconds},
        {QStringLiteral("databaseMode"), QStringLiteral("local_isolated_sqlite")},
        {QStringLiteral("sourceProvenance"), QStringLiteral("nas_synchronized")},
        {QStringLiteral("networkUsedDuringLongrun"), false},
        {QStringLiteral("databasePath"), m_options.databasePath},
        {QStringLiteral("roundsFile"), QFileInfo(m_roundsPath).fileName()},
        {QStringLiteral("activeRequestId"), QString::number(m_activeRequestId)},
        {QStringLiteral("roundsCompleted"), m_roundsCompleted},
        {QStringLiteral("tasksCompleted"), m_totalTasksCompleted},
        {QStringLiteral("tasksFailed"), m_totalTasksFailed},
        {QStringLiteral("watchdogTimeouts"), m_watchdogTimeouts},
        {QStringLiteral("deterministic"), m_deterministic},
        {QStringLiteral("firstResultDigest"), m_firstDigest},
        {QStringLiteral("lastResultDigest"), m_lastDigest},
        {QStringLiteral("minimumRawTickCountPerRound"),
         m_minRawTickCount < 0 ? 0 : m_minRawTickCount},
        {QStringLiteral("maximumRawTickCountPerRound"), m_maxRawTickCount},
        {QStringLiteral("roundDurationMillisecondsP50"),
         percentile(m_roundDurations, 0.50)},
        {QStringLiteral("roundDurationMillisecondsP95"),
         percentile(m_roundDurations, 0.95)},
        {QStringLiteral("roundDurationMillisecondsMax"),
         m_roundDurations.isEmpty()
             ? 0 : *std::max_element(m_roundDurations.cbegin(),
                                     m_roundDurations.cend())},
        {QStringLiteral("firstResults"), m_firstRows},
        {QStringLiteral("lastResults"), m_lastRows},
        {QStringLiteral("tPlusOneViolationsAvailable"), false}
    };
    QSaveFile file(m_reportPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    return file.write(bytes) == bytes.size() && file.commit();
}

void VisibleBacktestLongrunCoordinator::updateWindowTitle()
{
    if (!m_window || !m_controller) return;
    m_window->setTitle(
        QStringLiteral("PPTrader · 原始 Tick 回测长稳 · 第 %1 轮 · %2/%3")
            .arg(m_roundsCompleted + (m_active ? 1 : 0))
            .arg(m_controller->batchBacktestCompletedCount())
            .arg(m_controller->batchBacktestRequestedCount()));
}

double VisibleBacktestLongrunCoordinator::percentile(QVector<qint64> values,
                                                       double quantile)
{
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const qsizetype index = std::clamp<qsizetype>(
        static_cast<qsizetype>(std::ceil(quantile * values.size())) - 1,
        0, values.size() - 1);
    return static_cast<double>(values.at(index));
}

} // namespace pp
