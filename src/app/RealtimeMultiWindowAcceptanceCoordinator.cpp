#include "app/RealtimeMultiWindowAcceptanceCoordinator.h"

#include "app/RealtimeChartSession.h"
#include "app/TradingController.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QSaveFile>
#include <QGuiApplication>
#include <QTimeZone>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace pp {

namespace {
QStringList symbolsFromEnvironment(const char *name,
                                   const QStringList &fallback)
{
    const QString value = qEnvironmentVariable(name).trimmed();
    if (value.isEmpty()) return fallback;
    QStringList result;
    for (const QString &part : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString symbol = part.trimmed();
        if (!symbol.isEmpty()) result.append(symbol);
    }
    return result.isEmpty() ? fallback : result;
}

RealtimeChartSession *sessionAt(QAbstractItemModel *model, int row)
{
    if (!model || row < 0 || row >= model->rowCount()) return nullptr;
    const int role = model->roleNames().key(QByteArrayLiteral("sessionObject"), -1);
    if (role < 0) return nullptr;
    QObject *object = model->data(model->index(row, 0), role).value<QObject *>();
    return qobject_cast<RealtimeChartSession *>(object);
}

QJsonObject sessionJson(const RealtimeChartSession *session)
{
    if (!session) return {{QStringLiteral("available"), false}};
    QJsonArray chartPanes;
    QAbstractItemModel *paneModel = const_cast<RealtimeChartSession *>(session)
                                         ->chartPaneModel();
    if (paneModel) {
        const QHash<int, QByteArray> roles = paneModel->roleNames();
        const int idRole = roles.key(QByteArrayLiteral("chartId"), -1);
        const int periodRole = roles.key(QByteArrayLiteral("periodSeconds"), -1);
        const int countRole = roles.key(QByteArrayLiteral("pointCount"), -1);
        for (int row = 0; row < paneModel->rowCount(); ++row) {
            const QModelIndex index = paneModel->index(row, 0);
            chartPanes.append(QJsonObject {
                {QStringLiteral("chartId"), paneModel->data(index, idRole).toString()},
                {QStringLiteral("periodSeconds"), paneModel->data(index, periodRole).toInt()},
                {QStringLiteral("pointCount"), paneModel->data(index, countRole).toInt()}
            });
        }
    }
    return {
        {QStringLiteral("available"), true},
        {QStringLiteral("windowId"), session->windowId()},
        {QStringLiteral("symbol"), session->symbol()},
        {QStringLiteral("loading"), session->loading()},
        {QStringLiteral("source"), session->source()},
        {QStringLiteral("lastBarUtc"),
         session->lastBarTime().toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("expectedMinuteUtc"),
         session->expectedMinute().toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("providerLastBarUtc"),
         session->providerLastBarTime().toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("modelCommittedAtUtc"),
         session->modelCommittedAt().toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("minuteRefreshDiagnostics"),
         QJsonObject::fromVariantMap(session->minuteRefreshDiagnostics())},
        {QStringLiteral("stale"), session->stale()},
        {QStringLiteral("requestId"), qint64(session->requestId())},
        {QStringLiteral("resolvedRequestId"),
         qint64(session->resolvedRequestId())},
        {QStringLiteral("minuteCount"), session->minuteCount()},
        {QStringLiteral("fiveMinuteCount"), session->fiveMinuteCount()},
        {QStringLiteral("fifteenMinuteCount"), session->fifteenMinuteCount()},
        {QStringLiteral("sixtyMinuteCount"), session->sixtyMinuteCount()},
        {QStringLiteral("chartPaneCount"), session->chartPaneCount()},
        {QStringLiteral("chartPanes"), chartPanes},
        {QStringLiteral("successfulRefreshCount"),
         session->successfulRefreshCount()},
        {QStringLiteral("failedRefreshCount"), session->failedRefreshCount()},
        {QStringLiteral("instrumentSwitchCount"),
         session->instrumentSwitchCount()},
        {QStringLiteral("error"), session->errorText()}
    };
}
} // namespace

bool RealtimeMultiWindowAcceptanceCoordinator::optionsFromEnvironment(
    Options *options, QString *error)
{
    if (!options) return false;
    options->outputDirectory = qEnvironmentVariable(
        "PPTRADER_REALTIME_MULTIWINDOW_OUTPUT").trimmed();
    if (options->outputDirectory.isEmpty()) {
        if (error) *error = QStringLiteral("未设置验收输出目录");
        return false;
    }
    options->firstWindowSymbols = symbolsFromEnvironment(
        "PPTRADER_REALTIME_WINDOW_A_SYMBOLS",
        {QStringLiteral("600519"), QStringLiteral("000333"),
         QStringLiteral("601318")});
    options->secondWindowSymbols = symbolsFromEnvironment(
        "PPTRADER_REALTIME_WINDOW_B_SYMBOLS",
        {QStringLiteral("000001"), QStringLiteral("000858"),
         QStringLiteral("600036")});
    options->strategyStudyPlanPath = qEnvironmentVariable(
        "PPTRADER_LIMIT_UP_STRATEGY_PLAN").trimmed();
    bool durationOk = false;
    const int duration = qEnvironmentVariableIntValue(
        "PPTRADER_REALTIME_MULTIWINDOW_SECONDS", &durationOk);
    options->durationSeconds = std::clamp(durationOk ? duration : 1800, 30, 23000);
    bool intervalOk = false;
    const int interval = qEnvironmentVariableIntValue(
        "PPTRADER_REALTIME_MULTIWINDOW_SAMPLE_SECONDS", &intervalOk);
    options->sampleIntervalSeconds = std::clamp(intervalOk ? interval : 15, 5, 60);
    if (options->firstWindowSymbols.isEmpty()
        || options->secondWindowSymbols.isEmpty()) {
        if (error) *error = QStringLiteral("两个实时窗口都必须提供至少一个品种");
        return false;
    }
    return true;
}

RealtimeMultiWindowAcceptanceCoordinator::
RealtimeMultiWindowAcceptanceCoordinator(
    TradingController *controller, QCoreApplication *application,
    Options options, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_application(application)
    , m_options(std::move(options))
{
    m_sampleTimer.setInterval(m_options.sampleIntervalSeconds * 1000);
    m_sampleTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_sampleTimer, &QTimer::timeout,
            this, &RealtimeMultiWindowAcceptanceCoordinator::sample);
    m_finishTimer.setSingleShot(true);
    connect(&m_finishTimer, &QTimer::timeout, this,
            [this]() { finish(true); });
    connect(m_controller, &TradingController::crossSectionalFrameUpdated,
            this, &RealtimeMultiWindowAcceptanceCoordinator::captureStrategyFrame);
}

RealtimeMultiWindowAcceptanceCoordinator::
~RealtimeMultiWindowAcceptanceCoordinator()
{
    if (!m_finished && m_started) writeSummary(false, QStringLiteral("进程提前结束"));
}

void RealtimeMultiWindowAcceptanceCoordinator::start()
{
    if (m_started || !m_controller || !m_application) return;
    m_started = true;
    m_startedAt = QDateTime::currentDateTimeUtc();
    QDir directory;
    if (!directory.mkpath(m_options.outputDirectory)) {
        finish(false, QStringLiteral("无法创建验收输出目录"));
        return;
    }
    m_roundsFile.setFileName(QDir(m_options.outputDirectory).filePath(
        QStringLiteral("rounds.jsonl")));
    if (!m_roundsFile.open(QIODevice::WriteOnly | QIODevice::Truncate
                           | QIODevice::Text)) {
        finish(false, QStringLiteral("无法创建 rounds.jsonl"));
        return;
    }
    m_minuteResponsesFile.setFileName(QDir(m_options.outputDirectory).filePath(
        QStringLiteral("minute-responses.jsonl")));
    if (!m_minuteResponsesFile.open(QIODevice::WriteOnly | QIODevice::Truncate
                                   | QIODevice::Text)) {
        finish(false, QStringLiteral("无法创建 minute-responses.jsonl"));
        return;
    }
    if (!m_options.strategyStudyPlanPath.isEmpty()) {
        QString planError;
        if (!loadStrategyStudyPlan(&planError)) {
            finish(false, planError);
            return;
        }
        m_strategyFramesFile.setFileName(QDir(m_options.outputDirectory).filePath(
            QStringLiteral("limit-up-strategy-frames.jsonl")));
        if (!m_strategyFramesFile.open(QIODevice::WriteOnly | QIODevice::Truncate
                                       | QIODevice::Text)) {
            finish(false, QStringLiteral("无法创建涨停候选逐帧证据"));
            return;
        }
    }
    m_controller->startLive();
    m_windowIds = {
        m_controller->openRealtimeChartWindow(m_options.firstWindowSymbols.first()),
        m_controller->openRealtimeChartWindow(m_options.secondWindowSymbols.first())
    };
    if (m_windowIds.size() != 2 || m_windowIds.at(0).isEmpty()
        || m_windowIds.at(1).isEmpty()) {
        finish(false, QStringLiteral("双实时窗口创建失败：%1")
                          .arg(m_controller->realtimeChartWindowError()));
        return;
    }
    for (const QString &windowId : std::as_const(m_windowIds)) {
        // 每个子窗口按 Figma 契约默认已有 1m；验收再添加 5m、10m。
        if (m_controller->addRealtimeChart(windowId, 300).isEmpty()
            || m_controller->addRealtimeChart(windowId, 600).isEmpty()) {
            finish(false, QStringLiteral("实时子窗口无法建立 1m/5m/10m 图表列表"));
            return;
        }
    }
    for (int row = 0; row < 2; ++row) {
        if (auto *session = sessionAt(m_controller->realtimeChartWindowModel(), row)) {
            connect(session, &RealtimeChartSession::refreshCompleted, this,
                    [this](const QVariantMap &diagnostics) {
                if (m_finished) return;
                // 逐响应记录，750ms 重试不能依赖15秒抽样捕获。
                const QByteArray line = QJsonDocument(
                    QJsonObject::fromVariantMap(diagnostics)).toJson(QJsonDocument::Compact) + '\n';
                if (m_minuteResponsesFile.write(line) != line.size()
                    || !m_minuteResponsesFile.flush())
                    finish(false, QStringLiteral("分钟响应证据写入失败"));
            });
        }
    }

    // 按运行时长比例安排不等间隔切换，两个窗口交替变化。短预检仍至少切换一次。
    const QVector<double> ratios {0.08, 0.19, 0.34, 0.52, 0.73, 0.89};
    for (int index = 0; index < ratios.size(); ++index) {
        const int delayMilliseconds = std::max(
            5000, int(m_options.durationSeconds * ratios.at(index) * 1000.0));
        QTimer::singleShot(delayMilliseconds, this,
                           [this, index]() { switchWindow(index % 2); });
    }
    sample();
    m_sampleTimer.start();
    m_finishTimer.start(m_options.durationSeconds * 1000);
}

bool RealtimeMultiWindowAcceptanceCoordinator::loadStrategyStudyPlan(QString *error)
{
    QFile file(m_options.strategyStudyPlanPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法读取涨停候选冻结计划：%1")
            .arg(QFileInfo(m_options.strategyStudyPlanPath).absoluteFilePath());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("涨停候选冻结计划不是有效 JSON：%1")
            .arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    m_strategyPlanVersion = root.value(QStringLiteral("planVersion")).toString();
    m_strategyHistoryThrough = root.value(QStringLiteral("historyThrough")).toString();
    m_strategyFrozenAt = root.value(QStringLiteral("frozenAt")).toString();
    m_strategyTargetTradingDay = root.value(
        QStringLiteral("targetTradingDay")).toString();
    const QDate historyThrough = QDate::fromString(
        m_strategyHistoryThrough, Qt::ISODate);
    const QDate targetTradingDay = QDate::fromString(
        m_strategyTargetTradingDay, Qt::ISODate);
    const QDateTime frozenAt = QDateTime::fromString(m_strategyFrozenAt, Qt::ISODate);
    if (m_strategyPlanVersion.isEmpty() || !historyThrough.isValid()
        || !targetTradingDay.isValid() || historyThrough >= targetTradingDay
        || targetTradingDay < QDate::currentDate() || !frozenAt.isValid()) {
        if (error) *error = QStringLiteral(
            "涨停候选计划身份无效、目标已过期或历史未早于目标交易日，拒绝前视数据");
        return false;
    }
    const QJsonObject groups = root.value(QStringLiteral("groups")).toObject();
    const auto appendGroup = [this](const QJsonArray &symbols,
                                    const QString &category) {
        for (const QJsonValue &value : symbols) {
            const QString symbol = value.toString().trimmed();
            if (!symbol.isEmpty() && !m_strategyCategoryBySymbol.contains(symbol))
                m_strategyCategoryBySymbol.insert(symbol, category);
        }
    };
    appendGroup(groups.value(QStringLiteral("thirdBoard")).toArray(),
                QStringLiteral("三板候选"));
    appendGroup(groups.value(QStringLiteral("secondBoard")).toArray(),
                QStringLiteral("二板候选"));
    appendGroup(groups.value(QStringLiteral("reversalBoard")).toArray(),
                QStringLiteral("反转板候选"));
    if (m_strategyCategoryBySymbol.isEmpty()) {
        if (error) *error = QStringLiteral("涨停候选计划没有任何股票");
        return false;
    }
    return true;
}

void RealtimeMultiWindowAcceptanceCoordinator::captureStrategyFrame(
    const FactorFrame &frame)
{
    if (m_finished || !m_strategyFramesFile.isOpen()
        || frame.indicatorId != QStringLiteral("ashare_relative_strength")) return;
    if (m_lastStrategyFrameTime.isValid() && frame.timestamp < m_lastStrategyFrameTime)
        ++m_strategyMonotonicViolationCount;
    m_lastStrategyFrameTime = frame.timestamp;
    const QDate targetTradingDay = QDate::fromString(
        m_strategyTargetTradingDay, Qt::ISODate);
    const QDate frameTradingDay = frame.timestamp.toTimeZone(
        QTimeZone("Asia/Shanghai")).date();
    const bool targetDayFrame = targetTradingDay.isValid()
        && frameTradingDay == targetTradingDay;
    if (!targetDayFrame) ++m_strategyOutOfTargetDayFrameCount;

    struct CandidateRow {
        QString category;
        FactorValue value;
    };
    QVector<CandidateRow> candidates;
    for (const FactorValue &value : frame.values) {
        const auto category = m_strategyCategoryBySymbol.constFind(
            value.instrument.symbol);
        if (category == m_strategyCategoryBySymbol.cend()) continue;
        candidates.append({category.value(), value});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const CandidateRow &left, const CandidateRow &right) {
        if (left.category != right.category)
            return left.category > right.category;
        if (left.value.rawValue != right.value.rawValue)
            return left.value.rawValue > right.value.rawValue;
        return left.value.instrument.symbol < right.value.instrument.symbol;
    });
    QJsonArray rows;
    if (targetDayFrame) m_strategyLatestExactLimitSymbols.clear();
    for (const CandidateRow &candidate : candidates) {
        const FactorValue &value = candidate.value;
        const double dailyLimitPrice = value.sourcePreviousClose > 0.0
            ? std::floor(value.sourcePreviousClose * 1.10 * 100.0 + 0.5 + 1.0e-9)
                / 100.0
            : 0.0;
        const bool atExactLimitPrice = targetDayFrame && value.valid
            && dailyLimitPrice > 0.0
            && std::llround(value.sourcePrice * 100.0)
                == std::llround(dailyLimitPrice * 100.0);
        QString stage = QStringLiteral("观察");
        if (!targetDayFrame) {
            stage = QStringLiteral("非目标交易日（仅连通性预检）");
        } else if (!value.valid) {
            stage = QStringLiteral("无效");
        } else if (atExactLimitPrice) {
            stage = QStringLiteral("精确涨停价");
            m_strategyExactLimitTouchSymbols.insert(value.instrument.symbol);
            m_strategyLatestExactLimitSymbols.insert(value.instrument.symbol);
            m_strategyLimitHitSymbols.insert(value.instrument.symbol);
            m_strategyApproachSymbols.insert(value.instrument.symbol);
        } else if (value.rawValue >= 0.095) {
            stage = QStringLiteral("触及涨停区");
            m_strategyLimitHitSymbols.insert(value.instrument.symbol);
            m_strategyApproachSymbols.insert(value.instrument.symbol);
        } else if (value.rawValue >= 0.07) {
            stage = QStringLiteral("接近涨停");
            m_strategyApproachSymbols.insert(value.instrument.symbol);
        } else if (value.rawValue <= 0.0) {
            stage = QStringLiteral("未启动或走弱");
        }
        rows.append(QJsonObject {
            {QStringLiteral("category"), candidate.category},
            {QStringLiteral("symbol"), value.instrument.symbol},
            {QStringLiteral("name"), value.instrument.name},
            {QStringLiteral("valid"), value.valid},
            {QStringLiteral("invalidReason"), value.invalidReason},
            {QStringLiteral("price"), value.sourcePrice},
            {QStringLiteral("previousClose"), value.sourcePreviousClose},
            {QStringLiteral("dailyLimitPrice"), dailyLimitPrice},
            {QStringLiteral("atExactLimitPrice"), atExactLimitPrice},
            {QStringLiteral("changePercent"), value.rawValue * 100.0},
            {QStringLiteral("rank"), value.rank},
            {QStringLiteral("percentile"), value.percentile},
            {QStringLiteral("stage"), stage}
        });
    }
    const QJsonObject evidence {
        {QStringLiteral("frame"), ++m_strategyFrameCount},
        {QStringLiteral("planVersion"), m_strategyPlanVersion},
        {QStringLiteral("batchId"), frame.batchId},
        {QStringLiteral("snapshotUtc"), frame.timestamp.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("targetTradingDay"), m_strategyTargetTradingDay},
        {QStringLiteral("targetDayFrame"), targetDayFrame},
        {QStringLiteral("capturedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("snapshotExpectedCount"), m_controller->snapshotExpectedCount()},
        {QStringLiteral("snapshotReceivedCount"), m_controller->snapshotReceivedCount()},
        {QStringLiteral("snapshotCompleteness"), m_controller->snapshotCompleteness()},
        {QStringLiteral("snapshotLatencyMilliseconds"),
         qint64(m_controller->snapshotLatencyMilliseconds())},
        {QStringLiteral("candidates"), rows}
    };
    m_strategyFramesFile.write(QJsonDocument(evidence).toJson(QJsonDocument::Compact));
    m_strategyFramesFile.write("\n");
    m_strategyFramesFile.flush();
}

void RealtimeMultiWindowAcceptanceCoordinator::finishInterrupted()
{
    if (m_started && !m_finished)
        finish(false, QStringLiteral("应用在计划结束前退出"));
}

void RealtimeMultiWindowAcceptanceCoordinator::sample()
{
    if (m_finished) return;
    QAbstractItemModel *model = m_controller->realtimeChartWindowModel();
    const int windowCount = model ? model->rowCount() : 0;
    m_maxWindowCount = std::max(m_maxWindowCount, windowCount);
    QJsonArray sessions;
    int availableCount = 0;
    for (int row = 0; row < 2; ++row) {
        RealtimeChartSession *session = sessionAt(model, row);
        sessions.append(sessionJson(session));
        if (!session) continue;
        ++availableCount;
        if (session->lastBarTime().isValid()) {
            const QString key = session->windowId() + QLatin1Char(':')
                + session->symbol();
            const QDateTime previous = m_lastBarsByWindowAndSymbol.value(key);
            if (previous.isValid() && session->lastBarTime() < previous)
                ++m_monotonicViolationCount;
            m_lastBarsByWindowAndSymbol.insert(key, session->lastBarTime());
            if (session->lastBarTime().toLocalTime().date()
                == QDate::currentDate()) ++m_todayBarSampleCount;
        }
    }
    if (availableCount == 2) ++m_bothWindowSampleCount;
    QJsonArray visibleWindows;
    for (QWindow *window : QGuiApplication::topLevelWindows()) {
        if (!window || !window->isVisible()) continue;
        visibleWindows.append(QJsonObject {
            {QStringLiteral("title"), window->title()},
            {QStringLiteral("width"), window->width()},
            {QStringLiteral("height"), window->height()},
            {QStringLiteral("x"), window->x()},
            {QStringLiteral("y"), window->y()}
        });
    }
    const QJsonObject round {
        {QStringLiteral("sample"), ++m_sampleCount},
        {QStringLiteral("sampledAtUtc"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("windowCount"), windowCount},
        {QStringLiteral("visibleTopLevelWindows"), visibleWindows},
        {QStringLiteral("sessions"), sessions}
    };
    m_roundsFile.write(QJsonDocument(round).toJson(QJsonDocument::Compact));
    m_roundsFile.write("\n");
    m_roundsFile.flush();
}

void RealtimeMultiWindowAcceptanceCoordinator::switchWindow(int windowIndex)
{
    if (m_finished || windowIndex < 0 || windowIndex >= m_windowIds.size()) return;
    const QStringList &pool = windowIndex == 0
        ? m_options.firstWindowSymbols : m_options.secondWindowSymbols;
    if (pool.size() < 2) return;
    ++m_switchAttemptCount;
    m_symbolIndexes[windowIndex] = (m_symbolIndexes.at(windowIndex) + 1) % pool.size();
    if (m_controller->setRealtimeChartWindowInstrument(
            m_windowIds.at(windowIndex), pool.at(m_symbolIndexes.at(windowIndex)))) {
        ++m_switchSuccessCount;
    }
    sample();
}

void RealtimeMultiWindowAcceptanceCoordinator::finish(
    bool completed, const QString &error)
{
    if (m_finished) return;
    m_sampleTimer.stop();
    m_finishTimer.stop();
    sample();
    m_finished = true;
    writeSummary(completed, error);
    m_roundsFile.close();
    m_minuteResponsesFile.close();
    m_strategyFramesFile.close();
    if (m_controller) m_controller->stopLive();
    QMetaObject::invokeMethod(m_application, &QCoreApplication::quit,
                              Qt::QueuedConnection);
}

void RealtimeMultiWindowAcceptanceCoordinator::writeSummary(
    bool completed, const QString &error)
{
    QJsonArray finalSessions;
    QAbstractItemModel *model = m_controller
        ? m_controller->realtimeChartWindowModel() : nullptr;
    for (int row = 0; row < 2; ++row)
        finalSessions.append(sessionJson(sessionAt(model, row)));
    QStringList approached = m_strategyApproachSymbols.values();
    QStringList hit = m_strategyLimitHitSymbols.values();
    QStringList exactTouches = m_strategyExactLimitTouchSymbols.values();
    QStringList latestExact = m_strategyLatestExactLimitSymbols.values();
    std::sort(approached.begin(), approached.end());
    std::sort(hit.begin(), hit.end());
    std::sort(exactTouches.begin(), exactTouches.end());
    std::sort(latestExact.begin(), latestExact.end());
    const QJsonObject strategyStudy {
        {QStringLiteral("enabled"), !m_options.strategyStudyPlanPath.isEmpty()},
        {QStringLiteral("planPath"), m_options.strategyStudyPlanPath},
        {QStringLiteral("planVersion"), m_strategyPlanVersion},
        {QStringLiteral("frozenAt"), m_strategyFrozenAt},
        {QStringLiteral("historyThrough"), m_strategyHistoryThrough},
        {QStringLiteral("targetTradingDay"), m_strategyTargetTradingDay},
        {QStringLiteral("candidateCount"), m_strategyCategoryBySymbol.size()},
        {QStringLiteral("frameCount"), m_strategyFrameCount},
        {QStringLiteral("outOfTargetDayFrameCount"),
         m_strategyOutOfTargetDayFrameCount},
        {QStringLiteral("monotonicViolationCount"),
         m_strategyMonotonicViolationCount},
        {QStringLiteral("approachSymbols"), QJsonArray::fromStringList(approached)},
        {QStringLiteral("limitHitSymbols"), QJsonArray::fromStringList(hit)},
        {QStringLiteral("limitZoneSymbols"), QJsonArray::fromStringList(hit)},
        {QStringLiteral("exactLimitTouchSymbols"),
         QJsonArray::fromStringList(exactTouches)},
        {QStringLiteral("latestExactLimitSymbols"),
         QJsonArray::fromStringList(latestExact)}
    };
    const QJsonObject summary {
        {QStringLiteral("acceptanceType"),
         QStringLiteral("visible_realtime_dual_window_dynamic_charts")},
        {QStringLiteral("completed"), completed},
        {QStringLiteral("error"), error},
        {QStringLiteral("startedAtUtc"), m_startedAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("finishedAtUtc"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("durationSeconds"), m_options.durationSeconds},
        {QStringLiteral("sampleCount"), m_sampleCount},
        {QStringLiteral("bothWindowSampleCount"), m_bothWindowSampleCount},
        {QStringLiteral("maxWindowCount"), m_maxWindowCount},
        {QStringLiteral("switchAttemptCount"), m_switchAttemptCount},
        {QStringLiteral("switchSuccessCount"), m_switchSuccessCount},
        {QStringLiteral("monotonicViolationCount"), m_monotonicViolationCount},
        {QStringLiteral("todayBarSampleCount"), m_todayBarSampleCount},
        {QStringLiteral("strategyStudy"), strategyStudy},
        {QStringLiteral("finalSessions"), finalSessions}
    };
    QSaveFile file(QDir(m_options.outputDirectory).filePath(
        QStringLiteral("summary.json")));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(summary).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

} // namespace pp
