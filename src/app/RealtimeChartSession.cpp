#include "app/RealtimeChartSession.h"

#include <algorithm>
#include <limits>
#include <QTimeZone>

namespace pp {

namespace {
// 沪深连续竞价及收尾的一分钟宽限。仅控制补请求，不解释/过滤 Provider 标签。
bool inMinuteCatchupWindow(const QDateTime &utc)
{
    if (!utc.isValid()) return false;
    const QDateTime local = utc.toTimeZone(QTimeZone("Asia/Shanghai"));
    const QTime time = local.time();
    return local.date().dayOfWeek() <= 5
        && ((time >= QTime(9, 31) && time < QTime(11, 32))
            || (time >= QTime(13, 1) && time < QTime(15, 2)));
}
QDateTime naturalMinute(const QDateTime &utc)
{
    return TencentMinuteBarService::expectedClosedMinute(utc).addSecs(60);
}
} // namespace

RealtimeChartSession::RealtimeChartSession(
    const QString &windowId, const Instrument &instrument,
    int refreshOffsetMilliseconds, QObject *parent)
    : QObject(parent)
    , m_windowId(windowId)
    , m_instrument(instrument)
    , m_refreshOffsetMilliseconds(std::max(0, refreshOffsetMilliseconds))
{
    m_submitter = [this](quint64 requestId, const Instrument &requested,
                         int maximumBars) {
        return m_service.request(requestId, requested, maximumBars);
    };
    m_canceller = [this]() { m_service.cancel(); };
    initialize();
}

RealtimeChartSession::RealtimeChartSession(
    const QString &windowId, const Instrument &instrument,
    RequestSubmitter submitter, CancelCallback canceller,
    int refreshOffsetMilliseconds, QObject *parent)
    : QObject(parent)
    , m_windowId(windowId)
    , m_instrument(instrument)
    , m_submitter(std::move(submitter))
    , m_canceller(std::move(canceller))
    , m_refreshOffsetMilliseconds(std::max(0, refreshOffsetMilliseconds))
{
    initialize();
}

RealtimeChartSession::~RealtimeChartSession()
{
    shutdown();
}

void RealtimeChartSession::initialize()
{
    rememberInstrument(m_instrument);
    connect(&m_service, &TencentMinuteBarService::resultReady,
            this, &RealtimeChartSession::acceptResult);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_refreshEnabled || m_shutdownRequested) return;
        if (m_requestGate.loading()) {
            scheduleRefresh();
            return;
        }
        submitRefresh(m_retryPending);
    });
}

QString RealtimeChartSession::windowId() const { return m_windowId; }
QString RealtimeChartSession::symbol() const { return m_instrument.symbol; }
QString RealtimeChartSession::instrumentTitle() const
{
    return m_instrument.name.isEmpty()
        ? m_instrument.symbol
        : QStringLiteral("%1 · %2").arg(m_instrument.symbol, m_instrument.name);
}
bool RealtimeChartSession::loading() const { return m_requestGate.loading(); }
QString RealtimeChartSession::statusText() const { return m_statusText; }
QString RealtimeChartSession::errorText() const { return m_errorText; }
QString RealtimeChartSession::source() const { return m_source; }
QDateTime RealtimeChartSession::lastBarTime() const { return m_lastBarTime; }
QDateTime RealtimeChartSession::expectedMinute() const { return m_expectedMinute; }
QDateTime RealtimeChartSession::providerLastBarTime() const
{
    return m_providerLastBarTime;
}
QDateTime RealtimeChartSession::modelCommittedAt() const
{
    return m_modelCommittedAt;
}
QVariantMap RealtimeChartSession::minuteRefreshDiagnostics() const
{
    return m_minuteRefreshDiagnostics;
}
bool RealtimeChartSession::stale() const { return m_stale; }
bool RealtimeChartSession::refreshEnabled() const { return m_refreshEnabled; }
qulonglong RealtimeChartSession::requestId() const
{
    return m_requestGate.currentRequestId();
}
qulonglong RealtimeChartSession::resolvedRequestId() const
{
    return m_requestGate.resolvedRequestId();
}
int RealtimeChartSession::minuteCount() const
{
    return m_chartPanes.countForPeriod(60);
}
int RealtimeChartSession::fiveMinuteCount() const
{
    return m_chartPanes.countForPeriod(300);
}
int RealtimeChartSession::fifteenMinuteCount() const
{
    return m_chartPanes.countForPeriod(900);
}
int RealtimeChartSession::sixtyMinuteCount() const
{
    return m_chartPanes.countForPeriod(3600);
}
int RealtimeChartSession::successfulRefreshCount() const
{
    return m_successfulRefreshCount;
}
int RealtimeChartSession::failedRefreshCount() const
{
    return m_failedRefreshCount;
}
int RealtimeChartSession::instrumentSwitchCount() const
{
    return m_instrumentSwitchCount;
}

QAbstractItemModel *RealtimeChartSession::chartPaneModel()
{
    return &m_chartPanes;
}
int RealtimeChartSession::chartPaneCount() const { return m_chartPanes.count(); }
QString RealtimeChartSession::chartPaneError() const { return m_chartPaneError; }
QVariantList RealtimeChartSession::recentInstruments() const
{
    QVariantList result;
    result.reserve(m_recentInstruments.size());
    for (const Instrument &instrument : m_recentInstruments) {
        result.append(QVariantMap {
            {QStringLiteral("code"), instrument.symbol},
            {QStringLiteral("name"), instrument.name},
            {QStringLiteral("venue"), instrument.venue}
        });
    }
    return result;
}

QAbstractItemModel *RealtimeChartSession::modelForPeriod(int periodSeconds)
{
    return m_chartPanes.modelForPeriod(periodSeconds);
}

int RealtimeChartSession::countForPeriod(int periodSeconds) const
{
    return m_chartPanes.countForPeriod(periodSeconds);
}

QString RealtimeChartSession::addChart(int periodSeconds)
{
    const QString chartId = m_chartPanes.addChart(periodSeconds);
    m_chartPaneError = chartId.isEmpty()
        ? QStringLiteral("图表最多 10 张，且只支持 1/5/10/15/30/60 分钟")
        : QString();
    emit chartPanesChanged();
    return chartId;
}

bool RealtimeChartSession::removeChart(const QString &chartId)
{
    const bool removed = m_chartPanes.removeChart(chartId);
    m_chartPaneError = removed ? QString()
        : QStringLiteral("至少保留 1 张实时图表");
    emit chartPanesChanged();
    return removed;
}

bool RealtimeChartSession::setChartPeriod(
    const QString &chartId, int periodSeconds)
{
    const bool changed = m_chartPanes.setChartPeriod(chartId, periodSeconds);
    m_chartPaneError = changed ? QString()
        : QStringLiteral("周期只支持 1/5/10/15/30/60 分钟");
    emit chartPanesChanged();
    return changed;
}

qulonglong RealtimeChartSession::requestRefresh()
{
    return submitRefresh(false);
}

qulonglong RealtimeChartSession::submitRefresh(bool retry)
{
    if (m_shutdownRequested || m_instrument.symbol.isEmpty()
        || m_instrument.market != Market::AShare) return 0;
    m_refreshTimer.stop();
    m_retryPending = false;
    if (!retry) {
        m_catchupRetryCount = 0;
        m_roundBaselineCount = m_rawCandles.size();
        m_roundBaselineFetchedAt = m_lastSuccessfulFetchedAt;
    }
    if (m_canceller) m_canceller();
    const quint64 currentRequestId = m_requestGate.begin();
    m_errorText.clear();
    m_statusText = QStringLiteral(
        "正在读取 %1 的腾讯 1 分钟行情；图表派生数据仅驻留内存…")
        .arg(m_instrument.symbol);
    emit stateChanged();
    if (!m_submitter || !m_submitter(currentRequestId, m_instrument, 320)) {
        RealtimeMinuteSeriesResult result;
        result.instrument = m_instrument;
        result.requestedAt = QDateTime::currentDateTimeUtc();
        result.fetchedAt = result.requestedAt;
        result.providerId = QStringLiteral("tencent_public_m1");
        result.error = QStringLiteral("腾讯 1 分钟行情请求提交失败");
        acceptResult(currentRequestId, result);
    }
    return currentRequestId;
}

void RealtimeChartSession::setInstrument(
    const Instrument &instrument, bool requestImmediately)
{
    if (instrument.symbol == m_instrument.symbol) {
        if (requestImmediately) requestRefresh();
        return;
    }
    m_refreshTimer.stop();
    if (m_canceller) m_canceller();
    m_requestGate.cancel();
    m_instrument = instrument;
    rememberInstrument(instrument);
    ++m_instrumentSwitchCount;
    m_rawCandles.clear();
    clearModels();
    m_source.clear();
    m_lastBarTime = {};
    m_expectedMinute = {};
    m_providerLastBarTime = {};
    m_modelCommittedAt = {};
    m_catchupRetryCount = 0;
    m_retryPending = false;
    m_lastSuccessfulFetchedAt = {};
    m_roundBaselineFetchedAt = {};
    m_minuteRefreshDiagnostics.clear();
    m_stale = true;
    m_errorText.clear();
    m_statusText = QStringLiteral("已切换到 %1，等待实时行情")
                       .arg(instrumentTitle());
    emit stateChanged();
    if (requestImmediately) requestRefresh();
}

void RealtimeChartSession::setRefreshEnabled(
    bool enabled, bool requestImmediately)
{
    if (m_refreshEnabled == enabled) {
        if (enabled && requestImmediately) requestRefresh();
        return;
    }
    m_refreshEnabled = enabled;
    if (!enabled) {
        m_retryPending = false;
        m_refreshTimer.stop();
        if (m_canceller) m_canceller();
        m_requestGate.cancel();
        m_statusText = QStringLiteral("自动刷新已停止；保留上次内存行情");
    }
    emit stateChanged();
    if (enabled && requestImmediately) requestRefresh();
}

bool RealtimeChartSession::acceptResult(
    quint64 completedRequestId, const RealtimeMinuteSeriesResult &result)
{
    if (!m_requestGate.finish(completedRequestId)) return false;
    m_refreshTimer.stop();
    m_source = result.providerId;
    m_expectedMinute = TencentMinuteBarService::expectedClosedMinute(
        result.fetchedAt);
    m_providerLastBarTime = result.lastBarTime;
    const int previousCount = m_rawCandles.size();
    const bool providerRegressed = result.succeeded
        && result.instrument.symbol == m_instrument.symbol
        && m_lastBarTime.isValid() && result.lastBarTime.isValid()
        && result.lastBarTime < m_lastBarTime;
    const bool accepted = result.succeeded
        && result.instrument.symbol == m_instrument.symbol
        && !providerRegressed;
    if (accepted) {
        m_rawCandles = result.candles;
        m_lastBarTime = result.lastBarTime;
        const qint64 ageSeconds = m_lastBarTime.isValid()
            ? m_lastBarTime.secsTo(QDateTime::currentDateTimeUtc())
            : std::numeric_limits<qint64>::max();
        m_stale = ageSeconds > 120;
        m_errorText.clear();
        ++m_successfulRefreshCount;
        rebuildModels();
        m_modelCommittedAt = QDateTime::currentDateTimeUtc();
        m_lastSuccessfulFetchedAt = result.fetchedAt;
        m_statusText = QStringLiteral(
            "%1 · 1m %2 根 · %3 张图表 · %4")
            .arg(m_instrument.symbol)
            .arg(minuteCount())
            .arg(chartPaneCount())
            .arg(m_stale ? QStringLiteral("休市或陈旧")
                         : QStringLiteral("行情已更新"));
    } else {
        ++m_failedRefreshCount;
        m_errorText = providerRegressed
            ? QStringLiteral("Provider 分钟末柱回退，已保留上次内存行情")
            : result.error.isEmpty()
                ? QStringLiteral("实时分钟行情不可用") : result.error;
        if (m_lastBarTime.isValid()) {
            m_stale = m_lastBarTime.secsTo(QDateTime::currentDateTimeUtc()) > 120;
            m_statusText = QStringLiteral("刷新失败，保留上次内存行情");
        } else {
            m_stale = true;
            m_statusText = QStringLiteral("实时分钟行情不可用");
        }
    }
    const QTimeZone shanghai("Asia/Shanghai");
    const QDate fetchedDate = result.fetchedAt.toTimeZone(shanghai).date();
    const bool sameDay = result.lastBarTime.isValid()
        && result.lastBarTime.toTimeZone(shanghai).date() == fetchedDate;
    const bool expectedLabelPresent = std::any_of(
        result.candles.cbegin(), result.candles.cend(), [this](const Candle &bar) {
            return bar.startTime == m_expectedMinute;
        });
    const bool flatAcrossMinute = m_roundBaselineFetchedAt.isValid()
        && m_roundBaselineFetchedAt.toTimeZone(shanghai).date() == fetchedDate
        && naturalMinute(result.fetchedAt) > naturalMinute(m_roundBaselineFetchedAt)
        && result.candles.size() <= m_roundBaselineCount;
    const bool needsCatchup = accepted && sameDay
        && inMinuteCatchupWindow(result.fetchedAt)
        && (flatAcrossMinute || !expectedLabelPresent);
    const int responseRetry = m_catchupRetryCount;
    if (needsCatchup && m_catchupRetryCount < 2) {
        ++m_catchupRetryCount;
        scheduleCatchupRetry();
    } else {
        scheduleRefresh();
    }
    m_minuteRefreshDiagnostics = {
        {QStringLiteral("windowId"), m_windowId},
        {QStringLiteral("symbol"), m_instrument.symbol},
        {QStringLiteral("requestId"), QVariant::fromValue(completedRequestId)},
        {QStringLiteral("requestedAtUtc"), result.requestedAt.toUTC()},
        {QStringLiteral("fetchedAtUtc"), result.fetchedAt.toUTC()},
        {QStringLiteral("expectedMinuteUtc"), m_expectedMinute},
        {QStringLiteral("providerLastBarUtc"), result.lastBarTime},
        {QStringLiteral("modelCommittedAtUtc"), accepted ? m_modelCommittedAt : QDateTime{}},
        {QStringLiteral("returnedBarCount"), result.candles.size()},
        {QStringLiteral("acceptedBarCount"), m_rawCandles.size()},
        {QStringLiteral("countDelta"), m_rawCandles.size() - previousCount},
        {QStringLiteral("expectedLabelPresent"), expectedLabelPresent},
        {QStringLiteral("flatAcrossMinute"), flatAcrossMinute},
        {QStringLiteral("retryAttempt"), responseRetry},
        {QStringLiteral("retryScheduled"), m_retryPending},
        {QStringLiteral("retryExhausted"), needsCatchup && responseRetry >= 2},
        {QStringLiteral("accepted"), accepted},
        {QStringLiteral("providerRegressed"), providerRegressed},
        {QStringLiteral("error"), m_errorText},
        {QStringLiteral("labelSemantics"), QStringLiteral("provider_label_unverified")},
        {QStringLiteral("renderCommittedAtUtc"), QVariant{}},
        {QStringLiteral("payloadSha256"), result.payloadSha256},
        {QStringLiteral("providerTailRows"), result.providerTailRows},
        {QStringLiteral("httpDate"), result.httpDate},
        {QStringLiteral("httpAge"), result.httpAge},
        {QStringLiteral("responseRowCount"), result.responseRowCount}
    };
    emit refreshCompleted(m_minuteRefreshDiagnostics);
    emit stateChanged();
    return true;
}

void RealtimeChartSession::shutdown()
{
    if (m_shutdownRequested) return;
    m_shutdownRequested = true;
    m_refreshEnabled = false;
    m_retryPending = false;
    m_refreshTimer.stop();
    if (m_canceller) m_canceller();
    m_requestGate.cancel();
}

void RealtimeChartSession::scheduleRefresh()
{
    m_retryPending = false;
    m_refreshTimer.stop();
    if (m_shutdownRequested || !m_refreshEnabled
        || m_instrument.symbol.isEmpty()) return;
    m_refreshTimer.start(
        TencentMinuteBarService::millisecondsUntilNextMinuteRefresh(
            QDateTime::currentDateTimeUtc()) + m_refreshOffsetMilliseconds);
}

void RealtimeChartSession::scheduleCatchupRetry()
{
    m_refreshTimer.stop();
    if (m_shutdownRequested || !m_refreshEnabled
        || m_instrument.symbol.isEmpty()) return;
    m_retryPending = true;
    // 每个常规请求最多两次补请求；标签领先也不能替代序列实际增长。
    m_refreshTimer.start(750);
}

void RealtimeChartSession::rebuildModels()
{
    m_chartPanes.setRawCandles(m_rawCandles);
    emit chartPanesChanged();
}

void RealtimeChartSession::clearModels()
{
    m_chartPanes.clearSeries();
    emit chartPanesChanged();
}

void RealtimeChartSession::rememberInstrument(const Instrument &instrument)
{
    if (instrument.symbol.isEmpty()) return;
    for (int index = 0; index < m_recentInstruments.size(); ++index) {
        if (m_recentInstruments.at(index).symbol != instrument.symbol) continue;
        m_recentInstruments.removeAt(index);
        break;
    }
    m_recentInstruments.prepend(instrument);
    while (m_recentInstruments.size() > 20) m_recentInstruments.removeLast();
    emit recentInstrumentsChanged();
}

} // namespace pp
