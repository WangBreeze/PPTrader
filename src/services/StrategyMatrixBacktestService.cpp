#include "services/StrategyMatrixBacktestService.h"

#include "services/BacktestDataAvailabilityPlanner.h"
#include "services/BacktestDataQualityGate.h"
#include "services/BacktestEngine.h"
#include "services/SqliteLocalDataStore.h"
#include "strategy/CustomBacktestStrategy.h"

#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSet>
#include <QTimeZone>
#include <QTimer>
#include <QUuid>

#include <limits>
#include <memory>

namespace pp {

QString strategyMatrixParameterFingerprint(const QVariantMap &parameters)
{
    const QByteArray canonical = QJsonDocument::fromVariant(parameters)
                                     .toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(
        canonical, QCryptographicHash::Sha256).toHex().left(12));
}

QString strategyMatrixTaskId(const QString &runId,
                             const StrategyMatrixEntry &strategy,
                             const LocalHistoryDataset &dataset)
{
    return QStringLiteral("%1:%2:v%3:p%4:%5")
        .arg(runId, strategy.strategyId)
        .arg(strategy.version)
        .arg(strategyMatrixParameterFingerprint(strategy.parameters),
             dataset.instrument.symbol);
}

QString strategyMatrixRequestToJson(const StrategyMatrixRequest &request)
{
    QVariantList strategies;
    for (const StrategyMatrixEntry &entry : request.strategies) {
        QVariantMap map {{QStringLiteral("strategyId"), entry.strategyId},
                         {QStringLiteral("profileLabel"), entry.profileLabel},
                         {QStringLiteral("version"), entry.version},
                         {QStringLiteral("parameters"), entry.parameters},
                         {QStringLiteral("custom"), entry.custom}};
        if (entry.custom) {
            const auto &definition = entry.definitionSnapshot;
            map.insert(QStringLiteral("definition"), QVariantMap {
                {QStringLiteral("schemaVersion"), definition.schemaVersion},
                {QStringLiteral("strategyId"), definition.strategyId},
                {QStringLiteral("version"), definition.version},
                {QStringLiteral("name"), definition.name},
                {QStringLiteral("description"), definition.description},
                {QStringLiteral("author"), definition.author},
                {QStringLiteral("market"), definition.market},
                {QStringLiteral("executionScope"), definition.executionScope},
                {QStringLiteral("document"), definition.document.toVariantMap()}});
        }
        strategies.append(map);
    }
    QVariantList datasets;
    for (int index = 0; index < request.datasets.size(); ++index) {
        const auto &dataset = request.datasets.at(index);
        datasets.append(QVariantMap {
            {QStringLiteral("symbol"), dataset.instrument.symbol},
            {QStringLiteral("name"), dataset.instrument.name},
            {QStringLiteral("market"), int(dataset.instrument.market)},
            {QStringLiteral("venue"), dataset.instrument.venue},
            {QStringLiteral("periodSeconds"), dataset.periodSeconds},
            {QStringLiteral("source"), dataset.source},
            {QStringLiteral("identityJson"),
             index < request.frozenIdentities.size()
                 ? request.frozenIdentities.at(index).toJson() : QString{}}});
    }
    const QVariantMap root {
        {QStringLiteral("snapshotVersion"),
         request.dataMode == BacktestDataMode::RawTicks ? 2 : 1},
        {QStringLiteral("dataMode"), request.dataMode == BacktestDataMode::RawTicks
             ? QStringLiteral("raw_ticks") : QStringLiteral("candles")},
        {QStringLiteral("runId"), request.runId},
        {QStringLiteral("startDate"), request.startDate.toString(Qt::ISODate)},
        {QStringLiteral("endDate"), request.endDate.toString(Qt::ISODate)},
        {QStringLiteral("periodSeconds"), request.periodSeconds},
        {QStringLiteral("initialCapital"), request.initialCapital},
        {QStringLiteral("commissionRate"), request.commissionRate},
        {QStringLiteral("slippageRate"), request.slippageRate},
        {QStringLiteral("strategies"), strategies},
        {QStringLiteral("datasets"), datasets}};
    return QString::fromUtf8(QJsonDocument::fromVariant(root).toJson(
        QJsonDocument::Compact));
}

std::optional<StrategyMatrixRequest> strategyMatrixRequestFromJson(
    const QString &json, QString *error)
{
    if (error) error->clear();
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    const QVariantMap root = document.toVariant().toMap();
    StrategyMatrixRequest request;
    const int snapshotVersion = root.value(QStringLiteral("snapshotVersion")).toInt();
    if (!document.isObject() || (snapshotVersion != 1 && snapshotVersion != 2)) {
        if (error) *error = QStringLiteral("矩阵批次快照版本无效");
        return std::nullopt;
    }
    const QString mode = root.value(QStringLiteral("dataMode"),
        QStringLiteral("candles")).toString();
    if ((snapshotVersion == 1 && mode != QStringLiteral("candles"))
        || (snapshotVersion == 2 && mode != QStringLiteral("raw_ticks"))) {
        if (error) *error = QStringLiteral("矩阵批次数据模式无效");
        return std::nullopt;
    }
    request.dataMode = snapshotVersion == 2
        ? BacktestDataMode::RawTicks : BacktestDataMode::Candles;
    request.runId = root.value(QStringLiteral("runId")).toString();
    request.startDate = QDate::fromString(
        root.value(QStringLiteral("startDate")).toString(), Qt::ISODate);
    request.endDate = QDate::fromString(
        root.value(QStringLiteral("endDate")).toString(), Qt::ISODate);
    request.periodSeconds = root.value(QStringLiteral("periodSeconds")).toInt();
    request.initialCapital = root.value(QStringLiteral("initialCapital")).toDouble();
    request.commissionRate = root.value(QStringLiteral("commissionRate")).toDouble();
    request.slippageRate = root.value(QStringLiteral("slippageRate")).toDouble();
    CustomStrategyCompiler compiler;
    for (const QVariant &value : root.value(QStringLiteral("strategies")).toList()) {
        const QVariantMap map = value.toMap();
        StrategyMatrixEntry entry;
        entry.strategyId = map.value(QStringLiteral("strategyId")).toString();
        entry.profileLabel = map.value(QStringLiteral("profileLabel")).toString();
        entry.version = map.value(QStringLiteral("version")).toInt();
        entry.parameters = map.value(QStringLiteral("parameters")).toMap();
        entry.custom = map.value(QStringLiteral("custom")).toBool();
        if (entry.custom) {
            const QVariantMap frozen = map.value(QStringLiteral("definition")).toMap();
            auto &definition = entry.definitionSnapshot;
            definition.schemaVersion = frozen.value(QStringLiteral("schemaVersion")).toInt();
            definition.strategyId = frozen.value(QStringLiteral("strategyId")).toString();
            definition.version = frozen.value(QStringLiteral("version")).toInt();
            definition.name = frozen.value(QStringLiteral("name")).toString();
            definition.description = frozen.value(QStringLiteral("description")).toString();
            definition.author = frozen.value(QStringLiteral("author")).toString();
            definition.market = frozen.value(QStringLiteral("market")).toString();
            definition.executionScope = frozen.value(QStringLiteral("executionScope")).toString();
            definition.document = QJsonObject::fromVariantMap(
                frozen.value(QStringLiteral("document")).toMap());
            QString compileError;
            const auto plan = compiler.compile(definition, entry.parameters, &compileError);
            if (!plan || plan->strategyId != entry.strategyId
                || plan->version != entry.version
                || plan->normalizedParameters != entry.parameters) {
                if (error) *error = QStringLiteral("自定义策略快照无法恢复：%1")
                    .arg(compileError);
                return std::nullopt;
            }
            entry.customPlan = *plan;
        }
        request.strategies.append(entry);
    }
    for (const QVariant &value : root.value(QStringLiteral("datasets")).toList()) {
        const QVariantMap map = value.toMap();
        LocalHistoryDataset dataset;
        dataset.instrument.symbol = map.value(QStringLiteral("symbol")).toString();
        dataset.instrument.name = map.value(QStringLiteral("name")).toString();
        dataset.instrument.market = Market(map.value(QStringLiteral("market")).toInt());
        dataset.instrument.venue = map.value(QStringLiteral("venue")).toString();
        dataset.periodSeconds = map.value(QStringLiteral("periodSeconds")).toInt();
        dataset.source = map.value(QStringLiteral("source")).toString();
        BacktestDatasetIdentity identity;
        if (!identity.fromJson(map.value(QStringLiteral("identityJson")).toString())
            || identity.isEmpty()) {
            if (error) *error = QStringLiteral("矩阵数据身份快照无效");
            return std::nullopt;
        }
        request.datasets.append(dataset);
        request.frozenIdentities.append(identity);
    }
    if (request.runId.isEmpty() || !request.startDate.isValid()
        || !request.endDate.isValid() || request.startDate > request.endDate
        || (request.dataMode == BacktestDataMode::RawTicks
                ? request.periodSeconds != 0 : request.periodSeconds <= 0)
        || request.initialCapital <= 0.0
        || request.strategies.isEmpty() || request.datasets.isEmpty()) {
        if (error) *error = QStringLiteral("矩阵批次快照字段缺失");
        return std::nullopt;
    }
    return request;
}

namespace {
QDateTime shanghaiStart(const QDate &date)
{
    static const QTimeZone zone("Asia/Shanghai");
    return QDateTime(date, QTime(0, 0), zone).toUTC();
}
} // namespace

QVariantMap StrategyMatrixItem::toVariantMap() const
{
    return {{QStringLiteral("taskId"), taskId},
            {QStringLiteral("strategyId"), strategyId},
            {QStringLiteral("profileLabel"), profileLabel},
            {QStringLiteral("parameterFingerprint"), parameterFingerprint},
            {QStringLiteral("strategyVersion"), strategyVersion},
            {QStringLiteral("symbol"), symbol},
            {QStringLiteral("datasetVersion"), datasetVersion},
            {QStringLiteral("dataVersion"), dataVersion},
            {QStringLiteral("resultId"), resultId},
            {QStringLiteral("error"), error},
            {QStringLiteral("actualBars"), actualBars},
            {QStringLiteral("elapsedMilliseconds"), elapsedMilliseconds},
            {QStringLiteral("succeeded"), succeeded},
            {QStringLiteral("totalReturnPercent"), summary.totalReturnPercent},
            {QStringLiteral("maxDrawdownPercent"), summary.maxDrawdownPercent},
            {QStringLiteral("totalTrades"), summary.totalTrades},
            {QStringLiteral("totalCommission"), summary.totalCommission},
            {QStringLiteral("finalEquity"), summary.finalEquity}};
}

class StrategyMatrixWorker final : public QObject {
public:
    explicit StrategyMatrixWorker(StrategyMatrixBacktestService *owner)
        : m_owner(owner) {}

    void start(const QString &databasePath, const StrategyMatrixRequest &request)
    {
        m_request = request;
        m_datasetIndex = 0;
        m_strategyIndex = 0;
        m_candleOwnerResultId.clear();
        m_rawOwnerResultId.clear();
        m_running = true;
        m_clock.start();
        m_store = std::make_unique<SqliteLocalDataStore>();
        if (!m_store->initialize(databasePath)) {
            finish(false, QStringLiteral("矩阵回测数据库不可用"));
            return;
        }
        if (request.runId.isEmpty() || request.strategies.isEmpty()
            || request.datasets.isEmpty() || !request.startDate.isValid()
            || !request.endDate.isValid() || request.startDate > request.endDate
            || (request.dataMode == BacktestDataMode::RawTicks
                    ? request.periodSeconds != 0 : request.periodSeconds <= 0)
            || request.initialCapital <= 0.0) {
            finish(false, QStringLiteral("矩阵回测请求无效"));
            return;
        }
        runNext();
    }

    void cancel()
    {
        if (!m_running) return;
        if (m_engine) {
            disconnect(m_engine.get(), nullptr, this, nullptr);
            m_engine->cancel();
            m_engine.reset();
        }
        finish(true, {});
    }

private:
    StrategyMatrixItem currentItem() const
    {
        const auto &strategy = m_request.strategies.at(m_strategyIndex);
        const auto &dataset = m_request.datasets.at(m_datasetIndex);
        StrategyMatrixItem item;
        item.parameterFingerprint = strategyMatrixParameterFingerprint(
            strategy.parameters);
        item.taskId = strategyMatrixTaskId(m_request.runId, strategy, dataset);
        item.strategyId = strategy.strategyId;
        item.profileLabel = strategy.profileLabel;
        item.strategyVersion = strategy.version;
        item.symbol = dataset.instrument.symbol;
        item.datasetVersion = m_request.frozenIdentities.size() == m_request.datasets.size()
            ? m_request.frozenIdentities.at(m_datasetIndex).datasetVersion
            : m_identity.datasetVersion;
        return item;
    }

    void postItem(const StrategyMatrixItem &item)
    {
        QMetaObject::invokeMethod(m_owner, [owner = m_owner, item]() {
            emit owner->itemReady(item);
        }, Qt::QueuedConnection);
    }

    void finish(bool cancelled, const QString &error)
    {
        if (!m_running) return;
        m_running = false;
        m_engine.reset();
        m_preparedCandles = {};
        m_preparedRawDays.clear();
        m_store.reset();
        const QString runId = m_request.runId;
        QMetaObject::invokeMethod(m_owner, [owner = m_owner, runId, cancelled, error]() {
            owner->acceptFinished(runId, cancelled, error);
        }, Qt::QueuedConnection);
    }

    bool prepareDataset(QString *error)
    {
        m_preparedCandles = {};
        m_preparedRawDays.clear();
        m_identity = {};
        m_candleOwnerResultId.clear();
        m_rawOwnerResultId.clear();
        const auto &dataset = m_request.datasets.at(m_datasetIndex);
        const bool raw = m_request.dataMode == BacktestDataMode::RawTicks;
        auto identity = raw
            ? m_store->loadRawBacktestDatasetIdentity(dataset.instrument)
            : m_store->loadBacktestDatasetIdentity(
                  dataset.instrument, m_request.periodSeconds);
        if (!identity || identity->isEmpty()) {
            *error = QStringLiteral("数据集身份不可用");
            return false;
        }
        m_identity = BacktestDataAvailabilityPlanner::trimIdentity(
            *identity, m_request.startDate, m_request.endDate);
        if (m_identity.isEmpty()) {
            *error = QStringLiteral("日期范围内没有历史数据");
            return false;
        }
        if (m_request.frozenIdentities.size() == m_request.datasets.size()
            && m_request.frozenIdentities.at(m_datasetIndex).datasetVersion
                   != m_identity.datasetVersion) {
            *error = QStringLiteral("数据身份已变化，请创建新矩阵批次");
            return false;
        }
        const auto days = m_store->loadAShareInstrumentDayRecords(
            {dataset.instrument.symbol}, m_request.startDate, m_request.endDate);
        const auto audits = m_store->loadRawTickImportDayAudits(
            dataset.instrument, m_request.startDate, m_request.endDate);
        const auto quality = BacktestDataQualityGate::assess(
            m_identity, m_request.startDate, m_request.endDate, days, audits);
        if (!quality.eligible) {
            *error = QStringLiteral("历史数据质量准入拒绝：%1")
                .arg(quality.messages.join(QStringLiteral("；")));
            return false;
        }
        if (raw) {
            // 首版矩阵每股只读取一次 Tick，所有策略共享逐日只读输入。
            // 先给驻留内存设界；全市场容量另由受管批次控制。
            qint64 total = 0;
            for (const RawTickDayFingerprint &day : m_identity.days) {
                total += day.rawCount;
                if (total > RawMatrixMaxResidentTicksPerSymbol
                    || total > std::numeric_limits<int>::max()) {
                    *error = QStringLiteral("单股逐笔窗口超过首版 100 万 Tick 驻留上限");
                    return false;
                }
            }
            if (m_identity.days.isEmpty()
                || m_identity.firstTradingDate != m_request.startDate
                || m_identity.lastTradingDate != m_request.endDate
                || !m_identity.calendarCoverageKnown || m_identity.gapDayCount > 0
                || total <= 0) {
                *error = QStringLiteral("原始 Tick 日期窗口不连续或日历未覆盖");
                return false;
            }
            m_preparedRawDays.reserve(m_identity.days.size());
            for (const RawTickDayFingerprint &day : m_identity.days) {
                if (m_owner->m_cancelRequested.load(std::memory_order_acquire)) {
                    *error = QStringLiteral("矩阵回测已取消");
                    m_preparedRawDays.clear();
                    return false;
                }
                const RawTradeFingerprint before = m_store->rawTradeFingerprint(
                    dataset.instrument, day.tradingDate);
                if (before.count != day.rawCount
                    || before.contentSha256 != day.rawContentSha256) {
                    *error = QStringLiteral("执行前 Tick 日指纹变化");
                    m_preparedRawDays.clear();
                    return false;
                }
                QVector<RawTradeTick> ticks;
                ticks.reserve(static_cast<int>(day.rawCount));
                const bool loaded = m_store->forEachRawTradeTick(
                    dataset.instrument, day.tradingDate,
                    [this, &ticks](const RawTradeTick &tick) {
                        if (m_owner->m_cancelRequested.load(
                                std::memory_order_relaxed)) return false;
                        ticks.append(tick);
                        return true;
                    });
                const RawTradeFingerprint after = m_store->rawTradeFingerprint(
                    dataset.instrument, day.tradingDate);
                if (!loaded || ticks.size() != day.rawCount
                    || after.count != before.count
                    || after.contentSha256 != before.contentSha256) {
                    *error = QStringLiteral("分段读取时 Tick 日指纹变化");
                    m_preparedRawDays.clear();
                    return false;
                }
                m_preparedRawDays.append(std::move(ticks));
            }
            error->clear();
            return true;
        }
        if (m_identity.staleDayCount > 0) {
            *error = QStringLiteral("派生 K 线缓存过期 %1 日")
                .arg(m_identity.staleDayCount);
            return false;
        }
        QVector<Candle> candles = m_store->loadCandlesFromSource(
            dataset.instrument, m_request.periodSeconds,
            shanghaiStart(m_request.startDate),
            shanghaiStart(m_request.endDate.addDays(1)).addMSecs(-1),
            QStringLiteral("raw_tick_cache_v1"));
        if (candles.isEmpty()) {
            *error = QStringLiteral("日期范围内没有 Tick 派生 K 线");
            return false;
        }
        BacktestConfig config;
        config.instrument = dataset.instrument;
        config.periodSeconds = m_request.periodSeconds;
        config.sampleFrom = shanghaiStart(m_request.startDate);
        config.sampleTo = shanghaiStart(m_request.endDate.addDays(1)).addMSecs(-1);
        m_preparedCandles = BacktestEngine::prepareCandles(config, std::move(candles));
        error->clear();
        return true;
    }

    void runNext()
    {
        if (!m_running) return;
        if (m_owner->m_cancelRequested.load(std::memory_order_acquire)) {
            cancel();
            return;
        }
        if (m_datasetIndex >= m_request.datasets.size()) {
            finish(false, {});
            return;
        }
        if (m_strategyIndex == 0) {
            bool hasPending = false;
            for (const StrategyMatrixEntry &strategy : m_request.strategies) {
                if (!m_request.skipTaskIds.contains(strategyMatrixTaskId(
                        m_request.runId, strategy,
                        m_request.datasets.at(m_datasetIndex)))) {
                    hasPending = true;
                    break;
                }
            }
            if (!hasPending) {
                ++m_datasetIndex;
                QTimer::singleShot(0, this, [this]() { runNext(); });
                return;
            }
        }
        if (m_strategyIndex == 0) {
            QString error;
            if (!prepareDataset(&error)) {
                if (m_owner->m_cancelRequested.load(std::memory_order_acquire)) {
                    cancel();
                    return;
                }
                for (int index = 0; index < m_request.strategies.size(); ++index) {
                    m_strategyIndex = index;
                    StrategyMatrixItem item = currentItem();
                    item.error = error;
                    if (!m_request.skipTaskIds.contains(item.taskId)) postItem(item);
                }
                m_strategyIndex = 0;
                ++m_datasetIndex;
                QTimer::singleShot(0, this, [this]() { runNext(); });
                return;
            }
        }

        if (m_request.skipTaskIds.contains(strategyMatrixTaskId(
                m_request.runId, m_request.strategies.at(m_strategyIndex),
                m_request.datasets.at(m_datasetIndex)))) {
            QTimer::singleShot(0, this, [this]() {
                if (++m_strategyIndex >= m_request.strategies.size()) {
                    m_strategyIndex = 0;
                    ++m_datasetIndex;
                }
                runNext();
            });
            return;
        }

        const auto &dataset = m_request.datasets.at(m_datasetIndex);
        const auto &strategy = m_request.strategies.at(m_strategyIndex);
        StrategyMatrixItem item = currentItem();
        BacktestConfig config;
        config.taskId = item.taskId;
        config.strategyId = strategy.strategyId;
        config.strategyVersion = strategy.version;
        config.strategyParameters = strategy.parameters;
        if (strategy.custom) config.ruleSnapshot = strategy.customPlan.ruleSnapshot();
        config.datasetIdentity = m_identity;
        config.instrument = dataset.instrument;
        const bool raw = m_request.dataMode == BacktestDataMode::RawTicks;
        config.dataMode = m_request.dataMode;
        config.frequency = raw ? QStringLiteral("tick")
                               : localHistoryFrequency(m_request.periodSeconds);
        config.periodSeconds = m_request.periodSeconds;
        config.sampleFrom = shanghaiStart(m_request.startDate);
        config.sampleTo = shanghaiStart(m_request.endDate.addDays(1)).addMSecs(-1);
        config.initialCapital = m_request.initialCapital;
        config.commissionRate = m_request.commissionRate;
        config.slippageRate = m_request.slippageRate;
        config.lotSize = 100;

        std::unique_ptr<BacktestStrategy> provided;
        if (strategy.custom) {
            provided = std::make_unique<CustomBacktestStrategy>(
                strategy.customPlan, raw ? CustomBacktestInputMode::RawTicks
                                         : CustomBacktestInputMode::Candles);
            config.ruleSnapshot = provided->ruleSnapshot();
        }
        m_engine = std::make_unique<BacktestEngine>();
        m_engine->setCandleObservationsPerTurn(512);
        m_engine->setObservationSignalsEnabled(false);
        m_completing = false;
        const qint64 startedAt = m_clock.elapsed();
        connect(m_engine.get(), &BacktestEngine::finished, this,
                [this, item, startedAt](const PerformanceSummary &) mutable {
            const BacktestResult result = m_engine->result();
            item.actualBars = result.config.actualBars;
            item.dataVersion = result.config.dataVersion;
            item.summary = result.summary;
            item.elapsedMilliseconds = m_clock.elapsed() - startedAt;
            if (!isProfitableBacktestForArchive(result)) {
                item.succeeded = true;
                complete(item);
                return;
            }
            BacktestArchiveRecord archive;
            archive.resultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            archive.completedAt = QDateTime::currentDateTimeUtc();
            archive.result = result;
            if (m_request.dataMode == BacktestDataMode::Candles)
                archive.sharedCandleResultId = m_candleOwnerResultId;
            else archive.sharedRawTickResultId = m_rawOwnerResultId;
            QString archiveError;
            const bool saved = m_request.dataMode == BacktestDataMode::RawTicks
                ? m_store->saveDeferredRawTickBacktestArchive(
                      archive, {}, &archiveError)
                : m_store->saveBacktestArchive(archive);
            if (saved) {
                item.succeeded = true;
                item.resultId = archive.resultId;
                if (m_request.dataMode == BacktestDataMode::Candles
                    && m_candleOwnerResultId.isEmpty())
                    m_candleOwnerResultId = archive.resultId;
                if (m_request.dataMode == BacktestDataMode::RawTicks
                    && m_rawOwnerResultId.isEmpty())
                    m_rawOwnerResultId = archive.resultId;
            } else {
                item.error = archiveError.isEmpty()
                    ? QStringLiteral("回测已完成，但结果归档失败")
                    : QStringLiteral("回测已完成，但结果归档失败：%1")
                          .arg(archiveError);
            }
            complete(item);
        });
        connect(m_engine.get(), &BacktestEngine::errorOccurred, this,
                [this, item, startedAt](const QString &error) mutable {
            item.error = error;
            item.elapsedMilliseconds = m_clock.elapsed() - startedAt;
            complete(item);
        });
        if (raw) {
            if (provided && !provided->supportsProvidedRawTicks()) {
                item.error = QStringLiteral("自定义策略不支持原始 Tick 模式");
                complete(item);
                return;
            }
            int dayIndex = 0;
            m_engine->startStreamedRawTicks(
                config, m_identity.rawTickTotal(),
                [this, dayIndex](QVector<RawTradeTick> *chunk,
                                 QString *) mutable {
                    chunk->clear();
                    if (dayIndex < m_preparedRawDays.size())
                        *chunk = m_preparedRawDays.at(dayIndex++);
                    return true;
                }, std::move(provided));
        } else {
            m_engine->startPreparedCandles(
                config, m_preparedCandles, std::move(provided));
        }
        // start() 通过 errorOccurred 报告失败，统一走 complete()。
    }

    void complete(const StrategyMatrixItem &item)
    {
        if (m_completing) return;
        m_completing = true;
        postItem(item);
        QTimer::singleShot(0, this, [this]() {
            m_engine.reset();
            if (++m_strategyIndex >= m_request.strategies.size()) {
                m_strategyIndex = 0;
                ++m_datasetIndex;
            }
            runNext();
        });
    }

    StrategyMatrixBacktestService *m_owner = nullptr;
    StrategyMatrixRequest m_request;
    std::unique_ptr<SqliteLocalDataStore> m_store;
    std::unique_ptr<BacktestEngine> m_engine;
    BacktestEngine::PreparedCandleInput m_preparedCandles;
    QVector<QVector<RawTradeTick>> m_preparedRawDays;
    BacktestDatasetIdentity m_identity;
    QString m_candleOwnerResultId;
    QString m_rawOwnerResultId;
    QElapsedTimer m_clock;
    int m_datasetIndex = 0;
    int m_strategyIndex = 0;
    bool m_running = false;
    bool m_completing = false;
};

StrategyMatrixBacktestService::StrategyMatrixBacktestService(QObject *parent)
    : QObject(parent), m_worker(new StrategyMatrixWorker(this))
{
    qRegisterMetaType<StrategyMatrixItem>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderStrategyMatrix"));
    m_thread.start();
}

StrategyMatrixBacktestService::~StrategyMatrixBacktestService()
{
    if (m_worker && m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, [worker = m_worker]() { worker->cancel(); },
                                  Qt::BlockingQueuedConnection);
    }
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

void StrategyMatrixBacktestService::setDatabasePath(const QString &path)
{
    if (!m_running) m_databasePath = path;
}

bool StrategyMatrixBacktestService::start(const StrategyMatrixRequest &request)
{
    if (m_running || m_databasePath.isEmpty() || !m_worker || request.runId.isEmpty())
        return false;
    if (!request.frozenIdentities.isEmpty()
        && request.frozenIdentities.size() != request.datasets.size()) return false;
    if (request.dataMode == BacktestDataMode::RawTicks) {
        if (request.periodSeconds != 0
            || request.frozenIdentities.size() != request.datasets.size()
            || qint64(request.strategies.size()) * request.datasets.size()
                   > RawMatrixMaxTasks) return false;
        qint64 ticks = 0;
        for (const BacktestDatasetIdentity &identity : request.frozenIdentities) {
            for (const RawTickDayFingerprint &day : identity.days) {
                ticks += day.rawCount;
                if (day.rawCount <= 0
                    || ticks > RawMatrixMaxProcessedTicks
                    || ticks > std::numeric_limits<int>::max()) return false;
            }
        }
        if (ticks <= 0 || ticks * request.strategies.size()
                > RawMatrixMaxProcessedTicks) return false;
    }
    QSet<QString> profiles;
    for (const StrategyMatrixEntry &strategy : request.strategies) {
        const QString key = strategy.strategyId + QLatin1Char(':')
            + QString::number(strategy.version) + QLatin1Char(':')
            + strategyMatrixParameterFingerprint(strategy.parameters);
        if (profiles.contains(key)) return false;
        profiles.insert(key);
    }
    m_running = true;
    m_runId = request.runId;
    m_cancelRequested.store(false, std::memory_order_release);
    const bool queued = QMetaObject::invokeMethod(
        m_worker, [worker = m_worker, path = m_databasePath, request]() {
            worker->start(path, request);
        }, Qt::QueuedConnection);
    if (!queued) {
        m_running = false;
        m_runId.clear();
    }
    return queued;
}

void StrategyMatrixBacktestService::cancel()
{
    if (!m_running) return;
    m_cancelRequested.store(true, std::memory_order_release);
    QMetaObject::invokeMethod(m_worker, [worker = m_worker]() { worker->cancel(); },
                              Qt::QueuedConnection);
}

bool StrategyMatrixBacktestService::isRunning() const { return m_running; }

void StrategyMatrixBacktestService::acceptFinished(
    const QString &runId, bool cancelled, const QString &error)
{
    if (runId != m_runId) return;
    m_running = false;
    emit finished(runId, cancelled, error);
}

} // namespace pp
