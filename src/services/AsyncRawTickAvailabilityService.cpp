#include "services/AsyncRawTickAvailabilityService.h"

#include "services/BacktestDataAvailabilityPlanner.h"
#include "services/BacktestDataQualityGate.h"
#include "services/SqliteLocalDataStore.h"

#include <QMetaObject>
#include <QPointer>

#include <algorithm>

namespace {

constexpr int kMaximumAssessmentDetails = 200;

struct PendingJobGuard {
    std::shared_ptr<std::atomic<int>> count;
    ~PendingJobGuard()
    {
        if (count) count->fetch_sub(1, std::memory_order_acq_rel);
    }
};

} // namespace

namespace pp {

AsyncRawTickAvailabilityService::AsyncRawTickAvailabilityService(QObject *parent)
    : QObject(parent), m_worker(new QObject),
      m_latestRequestId(std::make_shared<std::atomic<quint64>>(0)),
      m_pendingJobs(std::make_shared<std::atomic<int>>(0))
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderRawTickAvailability"));
    m_thread.start();
}

AsyncRawTickAvailabilityService::~AsyncRawTickAvailabilityService()
{
    cancel();
    m_thread.quit();
    if (!m_thread.wait(5000)) m_thread.wait();
    m_worker = nullptr;
}

void AsyncRawTickAvailabilityService::cancel()
{
    m_latestRequestId->store(0, std::memory_order_release);
}

bool AsyncRawTickAvailabilityService::isBusy() const
{
    return m_pendingJobs->load(std::memory_order_acquire) > 0;
}

bool AsyncRawTickAvailabilityService::request(
    const QString &databasePath, quint64 requestId,
    const QDate &startDate, const QDate &endDate,
    int maximumSymbols)
{
    if (!m_worker || databasePath.trimmed().isEmpty() || requestId == 0
        || !startDate.isValid() || !endDate.isValid()
        || startDate > endDate || maximumSymbols <= 0) {
        return false;
    }
    m_latestRequestId->store(requestId, std::memory_order_release);
    m_pendingJobs->fetch_add(1, std::memory_order_acq_rel);
    QPointer<AsyncRawTickAvailabilityService> guard(this);
    const auto latestRequestId = m_latestRequestId;
    const auto pendingJobs = m_pendingJobs;
    const bool invoked = QMetaObject::invokeMethod(
        m_worker,
        [guard, latestRequestId, pendingJobs, databasePath, requestId, startDate,
         endDate, maximumSymbols]() {
            const PendingJobGuard pendingGuard{pendingJobs};
            if (latestRequestId->load(std::memory_order_acquire) != requestId) {
                return;
            }
            QVariantMap result;
            result.insert(QStringLiteral("requestedStartDate"),
                          startDate.toString(Qt::ISODate));
            result.insert(QStringLiteral("requestedEndDate"),
                          endDate.toString(Qt::ISODate));
            result.insert(QStringLiteral("requestedMaximumSymbols"),
                          maximumSymbols);

            SqliteLocalDataStore store;
            QString storeError;
            QObject::connect(
                &store, &SqliteLocalDataStore::writeFailed, &store,
                [&storeError](const QString &message) {
                    if (storeError.isEmpty()) storeError = message;
                });
            if (!store.initializeReadOnly(databasePath)
                || !store.beginReadSnapshot()) {
                result.insert(QStringLiteral("error"),
                              storeError.isEmpty()
                                  ? QStringLiteral("本地 SQLite 数据库不可用")
                                  : storeError);
            } else {
                QVector<Instrument> instruments = store.rawBacktestInstruments(
                    maximumSymbols, [latestRequestId, requestId]() {
                        return latestRequestId->load(std::memory_order_acquire)
                            != requestId;
                    });
                if (latestRequestId->load(std::memory_order_acquire)
                    != requestId) {
                    return;
                }
                std::sort(instruments.begin(), instruments.end(),
                          [](const Instrument &left, const Instrument &right) {
                              return left.symbol < right.symbol;
                          });
                const int limit = std::min(
                    maximumSymbols, static_cast<int>(instruments.size()));
                QVector<LocalHistoryDataset> datasets;
                datasets.reserve(limit);
                for (int index = 0; index < limit; ++index) {
                    LocalHistoryDataset dataset;
                    dataset.instrument = instruments.at(index);
                    dataset.periodSeconds = 0;
                    dataset.source = QStringLiteral("raw_trade_ticks_v1");
                    datasets.append(dataset);
                }
                result.insert(QStringLiteral("requestedSymbolCount"),
                              datasets.size());
                if (datasets.isEmpty() && storeError.isEmpty()) {
                    storeError = QStringLiteral(
                        "本地目录中没有可分析的原始 Tick");
                }
                QVector<BacktestDataQualityAssessment> assessments;
                QVariantList symbolAssessments;
                int assessmentDetailCount = 0;
                int eligibleSymbols = 0;
                assessments.reserve(datasets.size());
                symbolAssessments.reserve(std::min(
                    kMaximumAssessmentDetails,
                    static_cast<int>(datasets.size())));
                for (const LocalHistoryDataset &dataset : datasets) {
                    if (latestRequestId->load(std::memory_order_acquire)
                        != requestId) {
                        return;
                    }
                    BacktestDatasetIdentity identity;
                    if (const auto stored = store.loadRawBacktestDatasetIdentity(
                            dataset.instrument)) {
                        identity = BacktestDataAvailabilityPlanner::trimIdentity(
                            *stored, startDate, endDate);
                    } else {
                        identity.instrument = dataset.instrument;
                        identity.periodSeconds = 0;
                        identity.source = QStringLiteral("raw_trade_ticks_v1");
                    }
                    const QVector<AShareInstrumentDayRecord> instrumentDays =
                        store.loadAShareInstrumentDayRecords(
                            {dataset.instrument.symbol}, startDate, endDate);
                    const QVector<RawTickImportDayAudit> audits =
                        store.loadRawTickImportDayAudits(
                            dataset.instrument, startDate, endDate);
                    if (!storeError.isEmpty()) break;
                    BacktestDataQualityAssessment assessment =
                        BacktestDataQualityGate::assess(
                            identity, startDate, endDate,
                            instrumentDays, audits);
                    assessments.append(assessment);
                    if (assessment.eligible) ++eligibleSymbols;
                    const bool needsDetail = !assessment.eligible
                        || assessment.status
                            != BacktestDataQualityStatus::Pass
                        || !BacktestDataAvailabilityPlanner::blockingDates(
                                assessment).isEmpty();
                    if (needsDetail) {
                        ++assessmentDetailCount;
                        if (symbolAssessments.size()
                            < kMaximumAssessmentDetails) {
                            QVariantMap symbolMap = assessment.toVariantMap();
                            symbolMap.insert(QStringLiteral("symbol"),
                                             dataset.instrument.symbol);
                            symbolMap.insert(QStringLiteral("name"),
                                             dataset.instrument.name);
                            symbolAssessments.append(symbolMap);
                        }
                    }
                }

                if (!store.endReadSnapshot() && storeError.isEmpty()) {
                    storeError = QStringLiteral("SQLite 一致读快照结束失败");
                }
                if (!storeError.isEmpty()) {
                    result.insert(QStringLiteral("error"), storeError);
                    assessments.clear();
                    symbolAssessments.clear();
                    assessmentDetailCount = 0;
                    eligibleSymbols = 0;
                }

                const RawTickAvailabilityPlan plan =
                    BacktestDataAvailabilityPlanner::plan(
                        startDate, endDate, assessments);
                const QVariantMap planMap = plan.toVariantMap();
                for (auto it = planMap.cbegin(); it != planMap.cend(); ++it) {
                    result.insert(it.key(), it.value());
                }
                result.insert(QStringLiteral("symbolsAnalyzed"), assessments.size());
                result.insert(QStringLiteral("eligibleSymbolCount"), eligibleSymbols);
                result.insert(QStringLiteral("rejectedSymbolCount"),
                              assessments.size() - eligibleSymbols);
                result.insert(QStringLiteral("fullRangeEligible"),
                                  storeError.isEmpty()
                                  && plan.calendarCoverageKnown
                                  && !assessments.isEmpty()
                                  && plan.blockedDates.isEmpty()
                                  && assessments.size() == datasets.size()
                                  && eligibleSymbols == datasets.size());
                result.insert(QStringLiteral("symbolAssessments"),
                              symbolAssessments);
                result.insert(QStringLiteral("assessmentDetailCount"),
                              assessmentDetailCount);
                result.insert(QStringLiteral("assessmentDetailsTruncated"),
                              assessmentDetailCount
                                  > kMaximumAssessmentDetails);
                if (!storeError.isEmpty()) {
                    // 保留上方精确 SQL 错误；不能把读取失败伪装为数据缺口。
                } else if (!plan.calendarCoverageKnown) {
                    result.insert(QStringLiteral("error"),
                                  QStringLiteral("请求范围缺少完整权威交易日历覆盖"));
                } else if (!plan.recommendedSegment()) {
                    result.insert(QStringLiteral("error"),
                                  QStringLiteral("所选标的没有共同的完整 Tick 交易日区间"));
                }
            }
            if (!guard) return;
            QMetaObject::invokeMethod(
                guard,
                [guard, requestId, result]() {
                    if (guard) emit guard->resultReady(requestId, result);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    if (!invoked) {
        m_pendingJobs->fetch_sub(1, std::memory_order_acq_rel);
    }
    return invoked;
}

} // namespace pp
