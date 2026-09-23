#include "services/BatchBacktestService.h"

#include "services/AShareTradingCalendar.h"
#include "services/BacktestEngine.h"
#include "services/BacktestDataVersion.h"
#include "services/BacktestDataAvailabilityPlanner.h"
#include "services/BacktestDataQualityGate.h"
#include "services/SqliteLocalDataStore.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <QTimeZone>

#include <algorithm>
#include <limits>
#include <memory>

namespace pp {

namespace {

QDateTime startOfShanghaiDate(const QDate &date)
{
    static const QTimeZone shanghai("Asia/Shanghai");
    return QDateTime(date, QTime(0, 0), shanghai).toUTC();
}

BacktestDatasetIdentity identityForDateRange(
    BacktestDatasetIdentity identity, const QDate &startDate, const QDate &endDate)
{
    return BacktestDataAvailabilityPlanner::trimIdentity(
        std::move(identity), startDate, endDate);
}

QVariantMap summarySnapshot(const PerformanceSummary &summary)
{
    return {{QStringLiteral("initialCapital"), summary.initialCapital},
            {QStringLiteral("finalEquity"), summary.finalEquity},
            {QStringLiteral("totalReturnPercent"), summary.totalReturnPercent},
            {QStringLiteral("maxDrawdownPercent"), summary.maxDrawdownPercent},
            {QStringLiteral("winRatePercent"), summary.winRatePercent},
            {QStringLiteral("sharpeRatio"), summary.sharpeRatio},
            {QStringLiteral("totalCommission"), summary.totalCommission},
            {QStringLiteral("totalTrades"), summary.totalTrades}};
}

void addCanonicalField(QCryptographicHash &hash, const QByteArray &name,
                       const QByteArray &value)
{
    hash.addData(name);
    hash.addData(":");
    hash.addData(QByteArray::number(value.size()));
    hash.addData(":");
    hash.addData(value);
    hash.addData("\n");
}

void addCanonicalInteger(QCryptographicHash &hash, const QByteArray &name,
                         qint64 value)
{
    addCanonicalField(hash, name, QByteArray::number(value));
}

void addCanonicalDates(QCryptographicHash &hash, const QByteArray &name,
                       QVector<QDate> dates)
{
    std::sort(dates.begin(), dates.end());
    dates.erase(std::unique(dates.begin(), dates.end()), dates.end());
    addCanonicalInteger(hash, name + "Count", dates.size());
    for (const QDate &date : dates) {
        addCanonicalField(hash, name,
                          date.toString(Qt::ISODate).toUtf8());
    }
}

QString finishedSha256(QCryptographicHash &hash)
{
    return QStringLiteral("sha256:")
        + QString::fromLatin1(hash.result().toHex());
}

QString rawTickQualityEvidenceVersion(
    const BacktestDatasetIdentity &identity,
    const QDate &startDate, const QDate &endDate,
    const QVector<AShareInstrumentDayRecord> &instrumentDays,
    const QVector<RawTickImportDayAudit> &importAudits,
    const BacktestDataQualityAssessment &assessment)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addCanonicalField(hash, "schema",
                      "pptrader-raw-tick-quality-evidence-v1");
    addCanonicalField(hash, "startDate",
                      startDate.toString(Qt::ISODate).toUtf8());
    addCanonicalField(hash, "endDate",
                      endDate.toString(Qt::ISODate).toUtf8());

    // 数据身份以不可变逐日内容指纹为准，不信任调用方缓存的 datasetVersion。
    addCanonicalField(hash, "symbol",
                      identity.instrument.symbol.trimmed().toUpper().toUtf8());
    addCanonicalInteger(hash, "periodSeconds", identity.periodSeconds);
    addCanonicalField(hash, "source", identity.source.trimmed().toUtf8());
    addCanonicalInteger(hash, "aggregatorVersion", identity.aggregatorVersion);
    addCanonicalInteger(hash, "fingerprintVersion", identity.fingerprintVersion);
    addCanonicalField(hash, "datasetVersion",
                      computeBacktestDatasetVersion(identity).toUtf8());
    QVector<RawTickDayFingerprint> fingerprints = identity.days;
    std::sort(fingerprints.begin(), fingerprints.end(),
              [](const RawTickDayFingerprint &left,
                 const RawTickDayFingerprint &right) {
        return left.tradingDate < right.tradingDate;
    });
    const bool hasContentDigests = !fingerprints.isEmpty()
        && std::all_of(fingerprints.cbegin(), fingerprints.cend(),
                       [](const RawTickDayFingerprint &fingerprint) {
        return fingerprint.rawContentSha256.startsWith(
                   QStringLiteral("sha256:"))
            && fingerprint.rawContentSha256.size() == 71;
    });
    addCanonicalInteger(hash, "fingerprintCount", fingerprints.size());
    for (const RawTickDayFingerprint &fingerprint : fingerprints) {
        addCanonicalField(hash, "fingerprintDate",
                          fingerprint.tradingDate.toString(Qt::ISODate).toUtf8());
        addCanonicalInteger(hash, "fingerprintRawCount", fingerprint.rawCount);
        if (hasContentDigests) {
            addCanonicalField(hash, "fingerprintContentSha256",
                              fingerprint.rawContentSha256.toUtf8());
        } else {
            addCanonicalInteger(hash, "fingerprintRawMaximumId",
                                fingerprint.rawMaximumId);
        }
    }

    // 当前交易日日历没有独立 version 字段；其权威公告编号由 source 承载。
    for (QDate date = startDate; date <= endDate; date = date.addDays(1)) {
        const AShareTradingDayStatus status = AShareTradingCalendar::status(date);
        addCanonicalField(hash, "calendarDate",
                          date.toString(Qt::ISODate).toUtf8());
        addCanonicalInteger(hash, "calendarTradingDay", status.tradingDay);
        addCanonicalInteger(hash, "calendarCoverageKnown", status.coverageKnown);
        addCanonicalInteger(hash, "calendarWeekend", status.weekend);
        addCanonicalInteger(hash, "calendarHoliday", status.holiday);
        addCanonicalField(hash, "calendarSource",
                          status.source.trimmed().toUtf8());
    }

    QVector<AShareInstrumentDayRecord> relevantInstrumentDays;
    for (const AShareInstrumentDayRecord &record : instrumentDays) {
        if (record.instrument.symbol.trimmed()
                == identity.instrument.symbol.trimmed()
            && record.tradingDate >= startDate
            && record.tradingDate <= endDate) {
            relevantInstrumentDays.append(record);
        }
    }
    std::sort(relevantInstrumentDays.begin(), relevantInstrumentDays.end(),
              [](const AShareInstrumentDayRecord &left,
                 const AShareInstrumentDayRecord &right) {
        if (left.tradingDate != right.tradingDate)
            return left.tradingDate < right.tradingDate;
        if (left.state != right.state)
            return static_cast<int>(left.state)
                < static_cast<int>(right.state);
        if (left.source != right.source) return left.source < right.source;
        return left.sourceVersion < right.sourceVersion;
    });
    addCanonicalInteger(hash, "instrumentDayCount",
                        relevantInstrumentDays.size());
    for (const AShareInstrumentDayRecord &record : relevantInstrumentDays) {
        addCanonicalField(hash, "instrumentDayDate",
                          record.tradingDate.toString(Qt::ISODate).toUtf8());
        addCanonicalField(hash, "instrumentDayState",
                          aShareInstrumentDayStateName(record.state).toUtf8());
        addCanonicalField(hash, "instrumentDaySource",
                          record.source.trimmed().toUtf8());
        addCanonicalField(hash, "instrumentDaySourceVersion",
                          record.sourceVersion.trimmed().toUtf8());
    }

    QVector<RawTickImportDayAudit> relevantAudits;
    for (const RawTickImportDayAudit &audit : importAudits) {
        if (audit.tradingDate >= startDate && audit.tradingDate <= endDate)
            relevantAudits.append(audit);
    }
    std::sort(relevantAudits.begin(), relevantAudits.end(),
              [](const RawTickImportDayAudit &left,
                 const RawTickImportDayAudit &right) {
        if (left.tradingDate != right.tradingDate)
            return left.tradingDate < right.tradingDate;
        if (left.auditVersion != right.auditVersion)
            return left.auditVersion < right.auditVersion;
        if (left.rawContentSha256 != right.rawContentSha256)
            return left.rawContentSha256 < right.rawContentSha256;
        return left.source < right.source;
    });
    addCanonicalInteger(hash, "importAuditCount", relevantAudits.size());
    for (const RawTickImportDayAudit &audit : relevantAudits) {
        addCanonicalField(hash, "importAuditDate",
                          audit.tradingDate.toString(Qt::ISODate).toUtf8());
        addCanonicalField(hash, "importAuditContentSha256",
                          audit.rawContentSha256.toUtf8());
        addCanonicalInteger(hash, "importAuditVersion", audit.auditVersion);
        addCanonicalInteger(hash, "importAuditLinesRead", audit.linesRead);
        addCanonicalInteger(hash, "importAuditValidRecords",
                            audit.validRecords);
        addCanonicalInteger(hash, "importAuditSkippedRecords",
                            audit.skippedRecords);
        addCanonicalInteger(hash, "importAuditOutOfOrderRecords",
                            audit.outOfOrderRecords);
        addCanonicalField(hash, "importAuditQualityStatus",
                          audit.qualityStatus.trimmed().toLower().toUtf8());
        addCanonicalField(hash, "importAuditSource",
                          audit.source.trimmed().toUtf8());
    }

    addCanonicalField(hash, "qualityStatus",
                      backtestDataQualityStatusCode(assessment.status).toUtf8());
    addCanonicalInteger(hash, "qualityEligible", assessment.eligible);
    addCanonicalDates(hash, "observedRawTickDate",
                      assessment.observedRawTickDates);
    addCanonicalDates(hash, "missingTradingDate",
                      assessment.missingTradingDates);
    addCanonicalDates(hash, "explainedNoTradeDate",
                      assessment.explainedNoTradeDates);
    addCanonicalDates(hash, "unexpectedNonTradingDate",
                      assessment.unexpectedNonTradingDates);
    addCanonicalDates(hash, "contradictorySuspensionDate",
                      assessment.contradictorySuspensionDates);
    addCanonicalDates(hash, "unknownImportAuditDate",
                      assessment.unknownImportAuditDates);
    addCanonicalDates(hash, "outOfOrderAuditDate",
                      assessment.outOfOrderAuditDates);
    addCanonicalDates(hash, "excessiveSkippedDate",
                      assessment.excessiveSkippedDates);
    addCanonicalDates(hash, "failedAuditDate",
                      assessment.failedAuditDates);
    addCanonicalDates(hash, "auditCountMismatchDate",
                      assessment.auditCountMismatchDates);
    addCanonicalInteger(hash, "auditedDayCount", assessment.auditedDayCount);
    addCanonicalInteger(hash, "validRecords", assessment.validRecords);
    addCanonicalInteger(hash, "skippedRecords", assessment.skippedRecords);
    addCanonicalInteger(hash, "outOfOrderRecords",
                        assessment.outOfOrderRecords);
    addCanonicalInteger(hash, "calendarCoverageKnown",
                        assessment.calendarCoverageKnown);
    addCanonicalInteger(hash, "corporateActionCoverageKnown",
                        assessment.corporateActionCoverageKnown);
    addCanonicalDates(hash, "corporateActionDate",
                      assessment.corporateActionDates);
    // messages 和 auditedAt 均是展示/运行时元数据，刻意不进入证据身份。
    return finishedSha256(hash);
}

QString segmentedResearchPlanVersion(
    const QVector<RawTickResearchSegmentResult> &segments)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addCanonicalField(hash, "schema",
                      "pptrader-independent-segmented-research-plan-v1");
    addCanonicalInteger(hash, "segmentCount", segments.size());
    for (const RawTickResearchSegmentResult &segment : segments) {
        addCanonicalInteger(hash, "segmentNumber", segment.segmentNumber);
        addCanonicalField(hash, "segmentStartDate",
                          segment.startDate.toString(Qt::ISODate).toUtf8());
        addCanonicalField(hash, "segmentEndDate",
                          segment.endDate.toString(Qt::ISODate).toUtf8());
        addCanonicalInteger(hash, "segmentTradingDayCount",
                            segment.tradingDayCount);
        addCanonicalField(hash, "segmentQualityEvidenceVersion",
                          segment.qualityEvidenceVersion.toUtf8());
    }
    return finishedSha256(hash);
}

QString segmentedExecutionVersion(
    const QVector<RawTickResearchSegmentResult> &segments)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addCanonicalField(hash, "schema",
                      "pptrader-independent-segmented-execution-v2");
    addCanonicalInteger(hash, "segmentCount", segments.size());
    for (const RawTickResearchSegmentResult &segment : segments) {
        addCanonicalInteger(hash, "segmentNumber", segment.segmentNumber);
        addCanonicalField(hash, "segmentDataVersion",
                          segment.dataVersion.toUtf8());
    }
    return finishedSha256(hash);
}

bool rawFingerprintMatches(const RawTickDayFingerprint &expected,
                           const RawTradeFingerprint &current)
{
    if (current.count != expected.rawCount) return false;
    if (!expected.rawContentSha256.isEmpty()) {
        return current.contentSha256 == expected.rawContentSha256;
    }
    return current.maximumId == expected.rawMaximumId;
}

struct BatchExecutionJob {
    enum class Phase { Single, Training, Holdout, SegmentedResearch };

    LocalHistoryDataset dataset;
    BacktestEngine *engine = nullptr;
    qint64 startedAt = 0;
    Phase phase = Phase::Single;
    BacktestDatasetIdentity fullIdentity;
    BacktestDatasetIdentity trainingIdentity;
    BacktestDatasetIdentity holdoutIdentity;
    PerformanceSummary trainingSummary;
    QString trainingDataVersion;
    QVector<RawTickWalkForwardFoldResult> walkForwardFolds;
    QVector<BacktestDatasetIdentity> walkForwardTrainingIdentities;
    QVector<BacktestDatasetIdentity> walkForwardValidationIdentities;
    int currentWalkForwardFold = 0;
    QVector<BacktestDatasetIdentity> researchSegmentIdentities;
    QVector<RawTickResearchSegmentResult> researchSegments;
    int currentResearchSegment = 0;
    qint64 residentObservationCount = 0;
    BacktestDataQualityAssessment qualityAssessment;

    bool multiFoldWalkForward() const { return walkForwardFolds.size() > 1; }
    bool segmentedResearch() const
    {
        return phase == Phase::SegmentedResearch
            && !researchSegmentIdentities.isEmpty();
    }
};

} // namespace

class BatchBacktestWorker final : public QObject {
public:
    explicit BatchBacktestWorker(BatchBacktestService *owner)
        : m_owner(owner)
    {
    }

    void start(const QString &databasePath, const BatchBacktestRequest &request)
    {
        if (m_running) {
            postFinished(request.requestId, false, QStringLiteral("已有批量回测正在运行"));
            return;
        }
        m_request = request;
        m_request.maximumActiveEngines = std::clamp(
            m_request.maximumActiveEngines, 1, 20);
        if (m_request.dataMode == BacktestDataMode::RawTicks) {
            // 完整逐笔输入及逐 Tick 权益曲线占用显著高于周期 K 线。
            // 原始模式固定逐标的执行，避免调用方把 20 路 Candle 预算误用于 Tick。
            m_request.maximumActiveEngines = 1;
        }
        if (m_request.holdoutTradingDays > 0
            && m_request.walkForwardFoldCount == 0) {
            m_request.walkForwardFoldCount = 1;
        }
        m_nextDataset = 0;
        m_completed = 0;
        m_succeeded = 0;
        m_failed = 0;
        m_activeRawTickDatasets = 0;
        m_activeRawTickObservations = 0;
        m_running = true;
        m_clock.start();
        if (m_owner && m_owner->cancelRequested()) {
            finish(true, QString{});
            return;
        }

        m_store = std::make_unique<SqliteLocalDataStore>();
        if (!m_store->initialize(databasePath)) {
            finish(false, QStringLiteral("批量回测 SQLite 连接失败"));
            return;
        }
        if (m_request.requestId == 0 || m_request.datasets.isEmpty()
            || m_request.strategyId.trimmed().isEmpty()
            || !m_request.startDate.isValid() || !m_request.endDate.isValid()
            || m_request.startDate > m_request.endDate
            || (m_request.dataMode == BacktestDataMode::Candles
                && m_request.periodSeconds <= 0)
            || (m_request.dataMode == BacktestDataMode::RawTicks
                && m_request.periodSeconds != 0)
            || m_request.holdoutTradingDays < 0
            || m_request.walkForwardFoldCount < 0
            || m_request.walkForwardFoldCount > 20
            || (m_request.holdoutTradingDays == 0
                && m_request.walkForwardFoldCount > 0)
            || (m_request.segmentedResearch
                && (m_request.dataMode != BacktestDataMode::RawTicks
                    || m_request.holdoutTradingDays > 0
                    || m_request.walkForwardFoldCount > 0))
            || (m_request.holdoutTradingDays > 0
                && (m_request.dataMode != BacktestDataMode::RawTicks
                    || m_request.holdoutTradingDays < 2
                    || m_request.walkForwardFoldCount < 1))) {
            finish(false, QStringLiteral("批量回测请求无效"));
            return;
        }
        BacktestStrategyRegistry registry;
        if (!registry.supportsDataMode(
                m_request.strategyId, backtestDataModeCode(m_request.dataMode))) {
            finish(false, QStringLiteral("所选策略不支持当前回测数据模式"));
            return;
        }
        postProgress();
        launchMore();
    }

    void cancel()
    {
        if (!m_running) return;
        const auto engines = m_engines;
        m_engines.clear();
        for (BacktestEngine *engine : engines) {
            disconnect(engine, nullptr, this, nullptr);
            engine->cancel();
            engine->deleteLater();
        }
        m_activeRawTickDatasets = 0;
        m_activeRawTickObservations = 0;
        postRawTickResidency();
        finish(true, QString{});
    }

private:
    QVector<Candle> loadCandles(const LocalHistoryDataset &dataset, QString *error)
    {
        const QDateTime from = startOfShanghaiDate(m_request.startDate);
        const QDateTime to = startOfShanghaiDate(m_request.endDate.addDays(1)).addMSecs(-1);
        QVector<Candle> candles = m_store->loadCandlesFromSource(
            dataset.instrument, m_request.periodSeconds, from, to,
            QStringLiteral("raw_tick_cache_v1"));
        if (error) error->clear();
        return candles;
    }

    BacktestEngine::RawTickChunkLoader makeRawTickChunkLoader(
        const Instrument &instrument, BacktestDatasetIdentity identity)
    {
        std::sort(identity.days.begin(), identity.days.end(),
                  [](const RawTickDayFingerprint &left,
                     const RawTickDayFingerprint &right) {
            return left.tradingDate < right.tradingDate;
        });
        auto dayIndex = std::make_shared<int>(0);
        return [this, instrument, identity = std::move(identity), dayIndex](
                   QVector<RawTradeTick> *ticks, QString *error) mutable {
            if (!ticks) return false;
            ticks->clear();
            if (*dayIndex >= identity.days.size()) {
                if (error) error->clear();
                return true;
            }
            const RawTickDayFingerprint expected = identity.days.at(*dayIndex);
            const RawTradeFingerprint before = m_store->rawTradeFingerprint(
                instrument, expected.tradingDate);
            if (!rawFingerprintMatches(expected, before)) {
                if (error) {
                    *error = QStringLiteral(
                        "原始 Tick 身份在执行前已变化：%1 · %2")
                        .arg(instrument.symbol,
                             expected.tradingDate.toString(Qt::ISODate));
                }
                return false;
            }
            ticks->reserve(static_cast<int>(std::min<qint64>(
                expected.rawCount, std::numeric_limits<int>::max())));
            const bool loaded = m_store->forEachRawTradeTick(
                instrument, expected.tradingDate,
                [ticks](const RawTradeTick &tick) {
                    ticks->append(tick);
                    return true;
                });
            const RawTradeFingerprint after = m_store->rawTradeFingerprint(
                instrument, expected.tradingDate);
            if (!loaded || ticks->size() != expected.rawCount
                || !rawFingerprintMatches(expected, after)) {
                ticks->clear();
                if (error) {
                    *error = QStringLiteral(
                        "原始 Tick 身份在分段读取期间变化或读取不完整：%1 · %2")
                        .arg(instrument.symbol,
                             expected.tradingDate.toString(Qt::ISODate));
                }
                return false;
            }
            ++(*dayIndex);
            if (error) error->clear();
            return true;
        };
    }

    QString requestFrequency() const
    {
        return m_request.dataMode == BacktestDataMode::RawTicks
            ? QStringLiteral("tick") : localHistoryFrequency(m_request.periodSeconds);
    }

    BacktestConfig makeConfig(const LocalHistoryDataset &dataset,
                              const BacktestDatasetIdentity &identity,
                              const QDate &startDate, const QDate &endDate,
                              const QString &taskSuffix = {}) const
    {
        BacktestConfig config;
        config.taskId = QStringLiteral("%1:%2%3")
            .arg(m_request.requestId).arg(dataset.instrument.symbol, taskSuffix);
        config.strategyId = m_request.strategyId;
        config.strategyParameters = m_request.strategyParameters;
        config.instrument = dataset.instrument;
        config.dataMode = m_request.dataMode;
        config.frequency = requestFrequency();
        config.periodSeconds = m_request.periodSeconds;
        config.requestedBars = m_request.barCount;
        config.sampleFrom = startOfShanghaiDate(startDate);
        config.sampleTo = startOfShanghaiDate(endDate.addDays(1)).addMSecs(-1);
        config.initialCapital = m_request.initialCapital;
        config.commissionRate = m_request.commissionRate;
        config.slippageRate = m_request.slippageRate;
        config.lotSize = m_request.lotSize;
        config.datasetIdentity = identity;
        return config;
    }

    BatchBacktestItemResult baseItem(
        const LocalHistoryDataset &dataset,
        const BacktestDatasetIdentity *identity = nullptr,
        const BacktestDataQualityAssessment *quality = nullptr) const
    {
        BatchBacktestItemResult item;
        item.requestId = m_request.requestId;
        item.instrument = dataset.instrument;
        item.dataMode = m_request.dataMode;
        item.startDate = m_request.startDate;
        item.endDate = m_request.endDate;
        item.frequency = requestFrequency();
        item.requestedBars = m_request.barCount;
        item.holdoutEnabled = m_request.holdoutTradingDays > 0;
        item.holdoutTradingDays = m_request.holdoutTradingDays;
        item.walkForwardFoldCount = m_request.holdoutTradingDays > 0
            ? std::max(1, m_request.walkForwardFoldCount) : 0;
        item.walkForwardEnabled = item.walkForwardFoldCount > 1;
        item.segmentedResearch = m_request.segmentedResearch;
        item.researchOnly = item.walkForwardEnabled || item.segmentedResearch;
        if (identity && !identity->isEmpty()) {
            item.datasetVersion = identity->datasetVersion;
            item.datasetDayCount = identity->days.size();
            item.datasetRawTickCount = identity->rawTickTotal();
            item.datasetGapDayCount = identity->gapDayCount;
            item.datasetStaleDayCount = identity->staleDayCount;
        }
        if (quality) {
            item.dataQualityStatus = backtestDataQualityStatusCode(quality->status);
            item.dataQualityEligible = quality->eligible;
            item.dataQualityAssessment = quality->toVariantMap();
        }
        return item;
    }

    void failWithoutEngine(BatchBacktestItemResult item)
    {
        ++m_completed;
        ++m_failed;
        postItem(item);
        postProgress();
    }

    void launchMore()
    {
        if (!m_running) return;
        if (m_owner && m_owner->cancelRequested()) {
            cancel();
            return;
        }
        while (m_engines.size() < m_request.maximumActiveEngines
               && m_nextDataset < m_request.datasets.size()) {
            const LocalHistoryDataset dataset = m_request.datasets.at(m_nextDataset++);
            QString loadError;
            auto identity = m_request.dataMode == BacktestDataMode::RawTicks
                ? m_store->loadRawBacktestDatasetIdentity(dataset.instrument)
                : m_store->loadBacktestDatasetIdentity(
                    dataset.instrument, m_request.periodSeconds);
            if (identity) {
                *identity = identityForDateRange(
                    *identity, m_request.startDate, m_request.endDate);
            }
            QVector<AShareInstrumentDayRecord> instrumentDays;
            QVector<RawTickImportDayAudit> importAudits;
            std::optional<BacktestDataQualityAssessment> qualityAssessment;
            if (identity && !identity->isEmpty()) {
                instrumentDays = m_store->loadAShareInstrumentDayRecords(
                    {dataset.instrument.symbol}, m_request.startDate,
                    m_request.endDate);
                importAudits = m_store->loadRawTickImportDayAudits(
                    dataset.instrument, m_request.startDate,
                    m_request.endDate);
                qualityAssessment = BacktestDataQualityGate::assess(
                    *identity, m_request.startDate, m_request.endDate,
                    instrumentDays, importAudits);
                identity->gapDayCount =
                    qualityAssessment->missingTradingDates.size();
            }
            const bool identityUnavailable = !identity || identity->isEmpty();
            const bool identityStale = m_request.dataMode == BacktestDataMode::Candles
                && !identityUnavailable && identity->staleDayCount > 0;
            const bool qualityRejected = qualityAssessment.has_value()
                && !qualityAssessment->eligible;
            const bool segmentedCandidate = m_request.segmentedResearch
                && m_request.dataMode == BacktestDataMode::RawTicks
                && qualityAssessment.has_value() && !identityUnavailable;
            QVector<Candle> candles;
            const bool streamedRawTicks =
                m_request.dataMode == BacktestDataMode::RawTicks;
            if (!identityUnavailable
                && (!qualityRejected || segmentedCandidate)) {
                if (!streamedRawTicks) {
                    candles = loadCandles(dataset, &loadError);
                }
            }
            const int actualCount = qualityRejected && !identityUnavailable
                ? (m_request.dataMode == BacktestDataMode::RawTicks
                    ? identity->rawTickTotal() : identity->derivedBarTotal())
                : (m_request.dataMode == BacktestDataMode::RawTicks
                    ? (identity ? identity->rawTickTotal() : 0)
                    : candles.size());
            if (!loadError.isEmpty() || actualCount <= 0
                || identityUnavailable || identityStale
                || (qualityRejected && !segmentedCandidate)) {
                BatchBacktestItemResult item;
                item.requestId = m_request.requestId;
                item.instrument = dataset.instrument;
                item.dataMode = m_request.dataMode;
                item.startDate = m_request.startDate;
                item.endDate = m_request.endDate;
                item.frequency = requestFrequency();
                item.requestedBars = m_request.barCount;
                item.actualBars = actualCount;
                if (identity && !identity->isEmpty()) {
                    item.datasetVersion = identity->datasetVersion;
                    item.datasetDayCount = identity->days.size();
                    item.datasetRawTickCount = identity->rawTickTotal();
                    item.datasetGapDayCount = identity->gapDayCount;
                    item.datasetStaleDayCount = identity->staleDayCount;
                }
                if (qualityAssessment) {
                    item.dataQualityStatus = backtestDataQualityStatusCode(
                        qualityAssessment->status);
                    item.dataQualityEligible = qualityAssessment->eligible;
                    item.dataQualityAssessment =
                        qualityAssessment->toVariantMap();
                }
                if (!loadError.isEmpty()) {
                    item.error = loadError;
                } else if (qualityRejected) {
                    item.error = QStringLiteral("历史 Tick 数据质量准入拒绝：%1")
                        .arg(qualityAssessment->messages.join(
                            QStringLiteral("；")));
                } else if (actualCount <= 0 && !identityUnavailable) {
                    item.error = m_request.dataMode == BacktestDataMode::RawTicks
                        ? QStringLiteral("日期范围内没有原始 Tick")
                        : QStringLiteral("日期范围内没有 Tick 派生 K 线");
                } else if (identityUnavailable) {
                    item.error = QStringLiteral("数据集身份不可用：无原始 Tick 事实");
                } else {
                    item.error = QStringLiteral(
                        "派生缓存过期（%1 日），请先在本地分钟序列/导入面板刷新")
                                     .arg(identity->staleDayCount);
                }
                ++m_completed;
                ++m_failed;
                postItem(item);
                postProgress();
                continue;
            }

            auto job = std::make_shared<BatchExecutionJob>();
            job->dataset = dataset;
            job->fullIdentity = *identity;
            if (qualityAssessment) job->qualityAssessment = *qualityAssessment;
            job->startedAt = m_clock.elapsed();

            BacktestConfig config = makeConfig(
                dataset, *identity, m_request.startDate, m_request.endDate);
            if (m_request.segmentedResearch) {
                const RawTickAvailabilityPlan plan =
                    BacktestDataAvailabilityPlanner::plan(
                        m_request.startDate, m_request.endDate,
                        {*qualityAssessment});
                if (!plan.calendarCoverageKnown) {
                    BatchBacktestItemResult item = baseItem(
                        dataset, &*identity, &job->qualityAssessment);
                    item.actualBars = actualCount;
                    item.error = QStringLiteral(
                        "分段研究拒绝：请求范围缺少完整权威交易日历覆盖");
                    failWithoutEngine(item);
                    continue;
                }
                for (const RawTickAvailabilitySegment &segment : plan.segments) {
                    BacktestDatasetIdentity segmentIdentity = identityForDateRange(
                        *identity, segment.startDate, segment.endDate);
                    if (segmentIdentity.isEmpty()) continue;
                    const BacktestDataQualityAssessment segmentQuality =
                        BacktestDataQualityGate::assess(
                            segmentIdentity, segment.startDate, segment.endDate,
                            instrumentDays, importAudits);
                    if (!segmentQuality.eligible) continue;
                    RawTickResearchSegmentResult segmentResult;
                    segmentResult.segmentNumber = job->researchSegments.size() + 1;
                    segmentResult.startDate = segment.startDate;
                    segmentResult.endDate = segment.endDate;
                    segmentResult.tradingDayCount = segment.tradingDayCount;
                    segmentResult.actualTicks = segmentIdentity.rawTickTotal();
                    segmentResult.datasetVersion = segmentIdentity.datasetVersion;
                    segmentResult.qualityEvidenceVersion =
                        rawTickQualityEvidenceVersion(
                            segmentIdentity, segment.startDate, segment.endDate,
                            instrumentDays, importAudits, segmentQuality);
                    segmentResult.qualityAssessment =
                        segmentQuality.toVariantMap();
                    job->researchSegmentIdentities.append(segmentIdentity);
                    job->researchSegments.append(segmentResult);
                }
                if (job->researchSegmentIdentities.isEmpty()) {
                    BatchBacktestItemResult item = baseItem(
                        dataset, &*identity, &job->qualityAssessment);
                    item.actualBars = actualCount;
                    item.error = QStringLiteral(
                        "分段研究拒绝：没有通过严格质量门禁的独立完整区间");
                    failWithoutEngine(item);
                    continue;
                }
                job->phase = BatchExecutionJob::Phase::SegmentedResearch;
                const RawTickResearchSegmentResult &first =
                    job->researchSegments.first();
                config = makeConfig(
                    dataset, job->researchSegmentIdentities.first(),
                    first.startDate, first.endDate,
                    QStringLiteral(":segment:1"));
                config.ruleSnapshot = {
                    {QStringLiteral("evaluationMethod"),
                     QStringLiteral("independent_segmented_research_v1")},
                    {QStringLiteral("segmentNumber"), 1},
                    {QStringLiteral("segmentCount"),
                     job->researchSegments.size()},
                    {QStringLiteral("continuousBacktest"), false},
                    {QStringLiteral("archivable"), false}};
            } else if (m_request.holdoutTradingDays > 0) {
                QVector<QDate> observedDates;
                observedDates.reserve(identity->days.size());
                for (const RawTickDayFingerprint &day : identity->days) {
                    observedDates.append(day.tradingDate);
                }
                std::sort(observedDates.begin(), observedDates.end());
                const int foldCount = std::max(1, m_request.walkForwardFoldCount);
                const int requiredDates = m_request.holdoutTradingDays * foldCount + 2;
                if (observedDates.size() < requiredDates) {
                    BatchBacktestItemResult item = baseItem(
                        dataset, &*identity, &job->qualityAssessment);
                    item.actualBars = actualCount;
                    item.error = foldCount > 1
                        ? QStringLiteral(
                            "多折 walk-forward 至少需要 2 个初始训练交易日和 %1×%2 个验证交易日；当前只有 %3 日")
                              .arg(foldCount)
                              .arg(m_request.holdoutTradingDays)
                              .arg(observedDates.size())
                        : QStringLiteral(
                            "样本外验证至少需要 2 个训练交易日和 %1 个验证交易日；当前只有 %2 日")
                              .arg(m_request.holdoutTradingDays)
                              .arg(observedDates.size());
                    failWithoutEngine(item);
                    continue;
                }
                if (foldCount > 1) {
                    const int firstValidationIndex = observedDates.size()
                        - foldCount * m_request.holdoutTradingDays;
                    job->walkForwardFolds.reserve(foldCount);
                    job->walkForwardTrainingIdentities.reserve(foldCount);
                    job->walkForwardValidationIdentities.reserve(foldCount);
                    for (int foldIndex = 0; foldIndex < foldCount; ++foldIndex) {
                        const int validationStartIndex = firstValidationIndex
                            + foldIndex * m_request.holdoutTradingDays;
                        const int validationEndIndex = validationStartIndex
                            + m_request.holdoutTradingDays - 1;
                        RawTickWalkForwardFoldResult fold;
                        fold.foldNumber = foldIndex + 1;
                        fold.trainingStartDate = observedDates.first();
                        fold.trainingEndDate = observedDates.at(validationStartIndex - 1);
                        fold.validationStartDate = observedDates.at(validationStartIndex);
                        fold.validationEndDate = observedDates.at(validationEndIndex);
                        job->walkForwardTrainingIdentities.append(identityForDateRange(
                            *identity, fold.trainingStartDate, fold.trainingEndDate));
                        job->walkForwardValidationIdentities.append(identityForDateRange(
                            *identity, fold.validationStartDate, fold.validationEndDate));
                        fold.trainingActualTicks =
                            job->walkForwardTrainingIdentities.last().rawTickTotal();
                        fold.validationActualTicks =
                            job->walkForwardValidationIdentities.last().rawTickTotal();
                        fold.trainingDatasetVersion =
                            job->walkForwardTrainingIdentities.last().datasetVersion;
                        fold.validationDatasetVersion =
                            job->walkForwardValidationIdentities.last().datasetVersion;
                        job->walkForwardFolds.append(fold);
                    }
                    job->phase = BatchExecutionJob::Phase::Training;
                    const RawTickWalkForwardFoldResult &firstFold =
                        job->walkForwardFolds.first();
                    config = makeConfig(
                        dataset, job->walkForwardTrainingIdentities.first(),
                        firstFold.trainingStartDate, firstFold.trainingEndDate,
                        QStringLiteral(":walk-forward:1:training"));
                } else {
                    const int splitIndex = observedDates.size()
                        - m_request.holdoutTradingDays;
                    const QDate trainingStart = observedDates.first();
                    const QDate trainingEnd = observedDates.at(splitIndex - 1);
                    const QDate holdoutStart = observedDates.at(splitIndex);
                    const QDate holdoutEnd = observedDates.last();
                    job->trainingIdentity = identityForDateRange(
                        *identity, trainingStart, trainingEnd);
                    job->holdoutIdentity = identityForDateRange(
                        *identity, holdoutStart, holdoutEnd);
                    job->phase = BatchExecutionJob::Phase::Training;
                    config = makeConfig(dataset, job->trainingIdentity,
                                        trainingStart, trainingEnd,
                                        QStringLiteral(":training"));
                }
            }

            auto *engine = new BacktestEngine(this);
            job->engine = engine;
            m_engines.append(engine);
            connect(engine, &BacktestEngine::finished, this,
                    [this, job](const PerformanceSummary &) {
                BacktestEngine *engine = job->engine;
                if (!m_running || !engine || !m_engines.contains(engine)) return;
                if (job->phase == BatchExecutionJob::Phase::SegmentedResearch) {
                    RawTickResearchSegmentResult &segment =
                        job->researchSegments[job->currentResearchSegment];
                    const BacktestResult segmentResult = engine->result();
                    segment.actualTicks = segmentResult.config.actualBars;
                    segment.dataVersion = segmentResult.config.dataVersion;
                    segment.summary = segmentResult.summary;
                    segment.endingPosition = segmentResult.finalPosition;
                    ++job->currentResearchSegment;
                    if (job->currentResearchSegment
                        < job->researchSegments.size()) {
                        RawTickResearchSegmentResult &nextSegment =
                            job->researchSegments[job->currentResearchSegment];
                        const BacktestDatasetIdentity &nextIdentity =
                            job->researchSegmentIdentities.at(
                                job->currentResearchSegment);
                        BacktestConfig nextConfig = makeConfig(
                            job->dataset, nextIdentity,
                            nextSegment.startDate, nextSegment.endDate,
                            QStringLiteral(":segment:%1")
                                .arg(nextSegment.segmentNumber));
                        nextConfig.ruleSnapshot = {
                            {QStringLiteral("evaluationMethod"),
                             QStringLiteral("independent_segmented_research_v1")},
                            {QStringLiteral("segmentNumber"),
                             nextSegment.segmentNumber},
                            {QStringLiteral("segmentCount"),
                             job->researchSegments.size()},
                            {QStringLiteral("continuousBacktest"), false},
                            {QStringLiteral("archivable"), false}};
                        engine->startStreamedRawTicks(
                            nextConfig, nextIdentity.rawTickTotal(),
                            makeRawTickChunkLoader(
                                job->dataset.instrument, nextIdentity));
                        return;
                    }

                    BatchBacktestItemResult item = baseItem(
                        job->dataset, &job->fullIdentity,
                        &job->qualityAssessment);
                    item.succeeded = true;
                    item.researchOnly = true;
                    item.segmentedResearch = true;
                    item.researchSegments = job->researchSegments;
                    item.actualBars = 0;
                    item.datasetDayCount = 0;
                    item.datasetRawTickCount = 0;
                    QVector<double> returns;
                    returns.reserve(item.researchSegments.size());
                    double returnSum = 0.0;
                    int winningTrades = 0;
                    int losingTrades = 0;
                    for (int index = 0;
                         index < item.researchSegments.size(); ++index) {
                        const RawTickResearchSegmentResult &completed =
                            item.researchSegments.at(index);
                        item.actualBars += completed.actualTicks;
                        item.datasetDayCount +=
                            job->researchSegmentIdentities.at(index).days.size();
                        item.datasetRawTickCount += completed.actualTicks;
                        returns.append(completed.summary.totalReturnPercent);
                        returnSum += completed.summary.totalReturnPercent;
                        item.segmentedWorstDrawdownPercent = std::max(
                            item.segmentedWorstDrawdownPercent,
                            completed.summary.maxDrawdownPercent);
                        item.summary.totalCommission +=
                            completed.summary.totalCommission;
                        item.summary.totalTrades += completed.summary.totalTrades;
                        winningTrades += completed.summary.winningTrades;
                        losingTrades += completed.summary.losingTrades;
                    }
                    std::sort(returns.begin(), returns.end());
                    item.segmentedMeanReturnPercent = returnSum
                        / static_cast<double>(returns.size());
                    const int middle = returns.size() / 2;
                    item.segmentedMedianReturnPercent = returns.size() % 2
                        ? returns.at(middle)
                        : (returns.at(middle - 1) + returns.at(middle)) / 2.0;
                    item.segmentedWorstReturnPercent = returns.first();
                    item.segmentedBestReturnPercent = returns.last();
                    // 各段都从独立初始资金启动，不能合成为连续权益或复利收益。
                    item.summary.initialCapital = 0.0;
                    item.summary.finalEquity = 0.0;
                    item.summary.totalReturnPercent = 0.0;
                    item.summary.maxDrawdownPercent =
                        item.segmentedWorstDrawdownPercent;
                    item.summary.winningTrades = winningTrades;
                    item.summary.losingTrades = losingTrades;
                    item.summary.winRatePercent = item.summary.totalTrades > 0
                        ? static_cast<double>(winningTrades)
                              / item.summary.totalTrades * 100.0
                        : 0.0;
                    // 独立区间没有连续收益序列，Sharpe 保持不可推导的 0。
                    item.summary.sharpeRatio = 0.0;
                    item.researchPlanVersion = segmentedResearchPlanVersion(
                        item.researchSegments);
                    item.datasetVersion = item.researchPlanVersion;
                    item.dataVersion = segmentedExecutionVersion(
                        item.researchSegments);
                    item.elapsedMilliseconds = m_clock.elapsed() - job->startedAt;
                    completeEngine(engine, item, job);
                    return;
                }
                if (job->phase == BatchExecutionJob::Phase::Training) {
                    if (job->multiFoldWalkForward()) {
                        RawTickWalkForwardFoldResult &fold =
                            job->walkForwardFolds[job->currentWalkForwardFold];
                        fold.trainingSummary = engine->result().summary;
                        fold.trainingDataVersion = engine->result().config.dataVersion;
                        job->phase = BatchExecutionJob::Phase::Holdout;
                        const BacktestDatasetIdentity &validationIdentity =
                            job->walkForwardValidationIdentities.at(
                                job->currentWalkForwardFold);
                        BacktestConfig validationConfig = makeConfig(
                            job->dataset, validationIdentity,
                            fold.validationStartDate, fold.validationEndDate,
                            QStringLiteral(":walk-forward:%1:validation")
                                .arg(fold.foldNumber));
                        validationConfig.ruleSnapshot = {
                            {QStringLiteral("evaluationMethod"),
                             QStringLiteral("expanding_walk_forward_v1")},
                            {QStringLiteral("foldNumber"), fold.foldNumber},
                            {QStringLiteral("foldCount"),
                             job->walkForwardFolds.size()},
                            {QStringLiteral("validationTradingDays"),
                             m_request.holdoutTradingDays},
                            {QStringLiteral("trainingDatasetVersion"),
                             fold.trainingDatasetVersion},
                            {QStringLiteral("trainingDataVersion"),
                             fold.trainingDataVersion},
                            {QStringLiteral("trainingSummary"),
                             summarySnapshot(fold.trainingSummary)},
                            {QStringLiteral("validationDatasetVersion"),
                             fold.validationDatasetVersion}};
                        engine->startStreamedRawTicks(
                            validationConfig,
                            validationIdentity.rawTickTotal(),
                            makeRawTickChunkLoader(
                                job->dataset.instrument, validationIdentity));
                        return;
                    }
                    job->trainingSummary = engine->result().summary;
                    job->trainingDataVersion = engine->result().config.dataVersion;
                    job->phase = BatchExecutionJob::Phase::Holdout;
                    BacktestConfig holdoutConfig = makeConfig(
                        job->dataset, job->holdoutIdentity,
                        job->holdoutIdentity.firstTradingDate,
                        job->holdoutIdentity.lastTradingDate,
                        QStringLiteral(":holdout"));
                    holdoutConfig.ruleSnapshot = {
                        {QStringLiteral("evaluationMethod"),
                         QStringLiteral("time_ordered_holdout_v1")},
                        {QStringLiteral("holdoutTradingDays"),
                         m_request.holdoutTradingDays},
                        {QStringLiteral("trainingStartDate"),
                         job->trainingIdentity.firstTradingDate},
                        {QStringLiteral("trainingEndDate"),
                         job->trainingIdentity.lastTradingDate},
                        {QStringLiteral("trainingActualTicks"),
                         job->trainingIdentity.rawTickTotal()},
                        {QStringLiteral("trainingDataVersion"),
                         job->trainingDataVersion},
                        {QStringLiteral("trainingDatasetVersion"),
                         job->trainingIdentity.datasetVersion},
                        {QStringLiteral("trainingSummary"),
                         summarySnapshot(job->trainingSummary)},
                        {QStringLiteral("holdoutStartDate"),
                         job->holdoutIdentity.firstTradingDate},
                        {QStringLiteral("holdoutEndDate"),
                         job->holdoutIdentity.lastTradingDate},
                        {QStringLiteral("holdoutDatasetVersion"),
                         job->holdoutIdentity.datasetVersion}};
                    engine->startStreamedRawTicks(
                        holdoutConfig, job->holdoutIdentity.rawTickTotal(),
                        makeRawTickChunkLoader(
                            job->dataset.instrument, job->holdoutIdentity));
                    return;
                }

                if (job->phase == BatchExecutionJob::Phase::Holdout
                    && job->multiFoldWalkForward()) {
                    RawTickWalkForwardFoldResult &fold =
                        job->walkForwardFolds[job->currentWalkForwardFold];
                    fold.validationSummary = engine->result().summary;
                    fold.validationDataVersion = engine->result().config.dataVersion;
                    ++job->currentWalkForwardFold;
                    if (job->currentWalkForwardFold < job->walkForwardFolds.size()) {
                        RawTickWalkForwardFoldResult &nextFold =
                            job->walkForwardFolds[job->currentWalkForwardFold];
                        job->phase = BatchExecutionJob::Phase::Training;
                        const BacktestDatasetIdentity &trainingIdentity =
                            job->walkForwardTrainingIdentities.at(
                                job->currentWalkForwardFold);
                        BacktestConfig trainingConfig = makeConfig(
                            job->dataset, trainingIdentity,
                            nextFold.trainingStartDate, nextFold.trainingEndDate,
                            QStringLiteral(":walk-forward:%1:training")
                                .arg(nextFold.foldNumber));
                        engine->startStreamedRawTicks(
                            trainingConfig, trainingIdentity.rawTickTotal(),
                            makeRawTickChunkLoader(
                                job->dataset.instrument, trainingIdentity));
                        return;
                    }

                    BatchBacktestItemResult item = baseItem(
                        job->dataset, &job->fullIdentity,
                        &job->qualityAssessment);
                    item.succeeded = true;
                    item.researchOnly = true;
                    item.walkForwardEnabled = true;
                    item.walkForwardFoldCount = job->walkForwardFolds.size();
                    item.walkForwardFolds = job->walkForwardFolds;
                    item.actualBars = 0;
                    QVector<double> returns;
                    returns.reserve(item.walkForwardFolds.size());
                    double sum = 0.0;
                    for (const RawTickWalkForwardFoldResult &completedFold
                         : item.walkForwardFolds) {
                        item.actualBars += completedFold.validationActualTicks;
                        const double value =
                            completedFold.validationSummary.totalReturnPercent;
                        returns.append(value);
                        sum += value;
                    }
                    std::sort(returns.begin(), returns.end());
                    item.walkForwardMeanReturnPercent =
                        sum / static_cast<double>(returns.size());
                    item.walkForwardWorstReturnPercent = returns.first();
                    item.walkForwardBestReturnPercent = returns.last();
                    const int middle = returns.size() / 2;
                    item.walkForwardMedianReturnPercent = returns.size() % 2
                        ? returns.at(middle)
                        : (returns.at(middle - 1) + returns.at(middle)) / 2.0;
                    item.elapsedMilliseconds = m_clock.elapsed() - job->startedAt;
                    completeEngine(engine, item, job);
                    return;
                }

                const BacktestResult result = engine->result();
                BatchBacktestItemResult item = baseItem(
                    job->dataset, &job->fullIdentity,
                    &job->qualityAssessment);
                item.actualBars = result.config.actualBars;
                item.succeeded = true;
                item.dataVersion = result.config.dataVersion;
                item.summary = result.summary;
                item.finalPosition = result.finalPosition;
                item.elapsedMilliseconds = m_clock.elapsed() - job->startedAt;
                if (job->phase == BatchExecutionJob::Phase::Holdout) {
                    item.trainingStartDate = job->trainingIdentity.firstTradingDate;
                    item.trainingEndDate = job->trainingIdentity.lastTradingDate;
                    item.holdoutStartDate = job->holdoutIdentity.firstTradingDate;
                    item.holdoutEndDate = job->holdoutIdentity.lastTradingDate;
                    item.trainingActualTicks = job->trainingIdentity.rawTickTotal();
                    item.holdoutActualTicks = result.config.actualBars;
                    item.trainingDataVersion = job->trainingDataVersion;
                    item.holdoutDataVersion = result.config.dataVersion;
                    item.trainingDatasetVersion =
                        job->trainingIdentity.datasetVersion;
                    item.holdoutDatasetVersion =
                        job->holdoutIdentity.datasetVersion;
                    item.trainingSummary = job->trainingSummary;
                    item.holdoutSummary = result.summary;
                }
                postCompleted(job->dataset.instrument.symbol, result);
                completeEngine(engine, item, job);
            });
            connect(engine, &BacktestEngine::errorOccurred, this,
                    [this, job](const QString &message) {
                BacktestEngine *engine = job->engine;
                if (!m_running || !engine || !m_engines.contains(engine)) return;
                BatchBacktestItemResult item = baseItem(
                    job->dataset, &job->fullIdentity,
                    &job->qualityAssessment);
                item.error = job->phase == BatchExecutionJob::Phase::Training
                    ? QStringLiteral("训练段失败：%1").arg(message)
                    : (job->phase == BatchExecutionJob::Phase::Holdout
                       ? QStringLiteral("验证段失败：%1").arg(message)
                       : (job->phase == BatchExecutionJob::Phase::SegmentedResearch
                          ? QStringLiteral("独立研究段 %1 失败：%2")
                                .arg(job->currentResearchSegment + 1)
                                .arg(message)
                          : message));
                item.elapsedMilliseconds = m_clock.elapsed() - job->startedAt;
                completeEngine(engine, item, job);
            });
            connect(engine, &BacktestEngine::rawTickChunkResidencyChanged, this,
                    [this, job](int observations) {
                const qint64 normalized = std::max(0, observations);
                const qint64 previous = job->residentObservationCount;
                if (previous == normalized) return;
                if (previous <= 0 && normalized > 0) {
                    ++m_activeRawTickDatasets;
                } else if (previous > 0 && normalized <= 0) {
                    m_activeRawTickDatasets = std::max(
                        0, m_activeRawTickDatasets - 1);
                }
                m_activeRawTickObservations = std::max<qint64>(
                    0, m_activeRawTickObservations - previous + normalized);
                job->residentObservationCount = normalized;
                postRawTickResidency();
            });
            if (streamedRawTicks) {
                const BacktestDatasetIdentity &executionIdentity =
                    job->multiFoldWalkForward()
                    ? job->walkForwardTrainingIdentities.at(
                          job->currentWalkForwardFold)
                    : (job->segmentedResearch()
                       ? job->researchSegmentIdentities.at(
                             job->currentResearchSegment)
                       : job->phase == BatchExecutionJob::Phase::Training
                       ? job->trainingIdentity : *identity);
                engine->startStreamedRawTicks(
                    config, executionIdentity.rawTickTotal(),
                    makeRawTickChunkLoader(
                        dataset.instrument, executionIdentity));
            } else {
                engine->start(config, std::move(candles));
            }
        }
        if (m_running && m_completed >= m_request.datasets.size()
            && m_engines.isEmpty()) {
            finish(false, QString{});
        }
    }

    void completeEngine(BacktestEngine *engine, const BatchBacktestItemResult &item,
                        const std::shared_ptr<BatchExecutionJob> &job)
    {
        m_engines.removeOne(engine);
        engine->deleteLater();
        if (job && job->residentObservationCount > 0) {
            m_activeRawTickDatasets = std::max(0, m_activeRawTickDatasets - 1);
            m_activeRawTickObservations = std::max<qint64>(
                0, m_activeRawTickObservations - job->residentObservationCount);
            job->residentObservationCount = 0;
            postRawTickResidency();
        }
        ++m_completed;
        if (item.succeeded) ++m_succeeded;
        else ++m_failed;
        postItem(item);
        postProgress();
        if (m_owner && m_owner->cancelRequested()) {
            cancel();
            return;
        }
        QTimer::singleShot(0, this, [this]() { launchMore(); });
    }

    void postItem(const BatchBacktestItemResult &item)
    {
        QPointer<BatchBacktestService> guard(m_owner);
        QMetaObject::invokeMethod(m_owner, [guard, item]() {
            if (guard) emit guard->itemReady(item.requestId, item);
        }, Qt::QueuedConnection);
    }

    void postCompleted(const QString &symbol, const BacktestResult &result)
    {
        const quint64 requestId = m_request.requestId;
        QPointer<BatchBacktestService> guard(m_owner);
        QMetaObject::invokeMethod(m_owner, [guard, requestId, symbol, result]() {
            if (guard) emit guard->itemCompleted(requestId, symbol, result);
        }, Qt::QueuedConnection);
    }

    void postProgress()
    {
        const quint64 requestId = m_request.requestId;
        const int total = m_request.datasets.size();
        const int completed = m_completed;
        const int succeeded = m_succeeded;
        const int failed = m_failed;
        QPointer<BatchBacktestService> guard(m_owner);
        QMetaObject::invokeMethod(m_owner,
            [guard, requestId, completed, total, succeeded, failed]() {
                if (guard) emit guard->progressChanged(
                    requestId, completed, total, succeeded, failed);
            }, Qt::QueuedConnection);
    }

    void postRawTickResidency()
    {
        const quint64 requestId = m_request.requestId;
        const int residentDatasets = m_activeRawTickDatasets;
        const qint64 residentObservations = m_activeRawTickObservations;
        QPointer<BatchBacktestService> guard(m_owner);
        QMetaObject::invokeMethod(m_owner,
            [guard, requestId, residentDatasets, residentObservations]() {
                if (guard) emit guard->rawTickResidencyChanged(
                    requestId, residentDatasets, residentObservations);
            }, Qt::QueuedConnection);
    }

    void postFinished(quint64 requestId, bool cancelled, const QString &error)
    {
        QPointer<BatchBacktestService> guard(m_owner);
        QMetaObject::invokeMethod(m_owner, [guard, requestId, cancelled, error]() {
            if (guard) guard->acceptFinished(requestId, cancelled, error);
        }, Qt::QueuedConnection);
    }

    void finish(bool cancelled, const QString &error)
    {
        if (!m_running) return;
        const quint64 requestId = m_request.requestId;
        m_running = false;
        m_store.reset();
        postFinished(requestId, cancelled, error);
    }

    QPointer<BatchBacktestService> m_owner;
    BatchBacktestRequest m_request;
    std::unique_ptr<SqliteLocalDataStore> m_store;
    QVector<BacktestEngine *> m_engines;
    QElapsedTimer m_clock;
    int m_nextDataset = 0;
    int m_completed = 0;
    int m_succeeded = 0;
    int m_failed = 0;
    int m_activeRawTickDatasets = 0;
    qint64 m_activeRawTickObservations = 0;
    bool m_running = false;
};

BatchBacktestService::BatchBacktestService(QObject *parent)
    : QObject(parent), m_worker(new BatchBacktestWorker(this))
{
    qRegisterMetaType<BatchBacktestRequest>();
    qRegisterMetaType<BatchBacktestItemResult>();
    qRegisterMetaType<BacktestResult>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderBatchBacktest"));
    m_thread.start();
}

BatchBacktestService::~BatchBacktestService()
{
    if (m_worker && m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, [worker = m_worker]() { worker->cancel(); },
                                  Qt::BlockingQueuedConnection);
    }
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

void BatchBacktestService::setDatabasePath(const QString &databasePath)
{
    if (!m_running) m_databasePath = databasePath;
}

bool BatchBacktestService::start(const BatchBacktestRequest &request)
{
    if (m_running || !m_worker || m_databasePath.isEmpty() || request.requestId == 0) return false;
    m_running = true;
    m_activeRequestId = request.requestId;
    m_cancelRequested.store(false, std::memory_order_release);
    const QString databasePath = m_databasePath;
    const bool queued = QMetaObject::invokeMethod(m_worker,
        [worker = m_worker, databasePath, request]() { worker->start(databasePath, request); },
        Qt::QueuedConnection);
    if (!queued) {
        m_running = false;
        m_activeRequestId = 0;
    }
    return queued;
}

void BatchBacktestService::cancel()
{
    if (!m_running || !m_worker) return;
    m_cancelRequested.store(true, std::memory_order_release);
    QMetaObject::invokeMethod(m_worker, [worker = m_worker]() { worker->cancel(); },
                              Qt::QueuedConnection);
}

bool BatchBacktestService::isRunning() const { return m_running; }

bool BatchBacktestService::cancelRequested() const
{
    return m_cancelRequested.load(std::memory_order_acquire);
}

void BatchBacktestService::acceptFinished(
    quint64 requestId, bool cancelled, const QString &error)
{
    if (requestId != m_activeRequestId) return;
    m_running = false;
    emit finished(requestId, cancelled, error);
}

} // namespace pp
