#include "services/ExternalFactorResearchService.h"

#include "domain/FactorCandidateTypes.h"
#include "services/ExternalFactorResearchEngine.h"
#include "services/SqliteLocalDataStore.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <memory>

namespace pp {
namespace {

ExternalFactorResearchPreflightResult executePreflight(
    const QString &databasePath,
    const ExternalFactorResearchRequest &request)
{
    ExternalFactorResearchPreflightResult result;
    result.datasetId = request.datasetId;
    result.periodSeconds = request.periodSeconds;
    QElapsedTimer timer;
    timer.start();
    const auto fail = [&](const QString &message) {
        result.error = message;
        result.elapsedMilliseconds = timer.elapsed();
        return result;
    };
    const auto interrupted = []() {
        QThread *thread = QThread::currentThread();
        return thread && thread->isInterruptionRequested();
    };

    if (databasePath.trimmed().isEmpty())
        return fail(QStringLiteral("本地数据库路径为空"));
    if (request.periodSeconds <= 0)
        return fail(QStringLiteral("研究收益周期无效"));
    if (interrupted()) {
        result.cancelled = true;
        return fail(QStringLiteral("外部因子研究预检已取消"));
    }
    SqliteLocalDataStore store;
    if (!store.initialize(databasePath))
        return fail(QStringLiteral("无法打开本地 SQLite 数据库"));

    std::optional<FactorCandidateVersion> candidate;
    for (const FactorCandidateVersion &item :
         store.loadFactorCandidateVersions(10000)) {
        if (item.candidateId == request.candidateId
            && item.version == request.candidateVersion) {
            candidate = item;
            break;
        }
    }
    if (!candidate || !candidate->isFrozen())
        return fail(QStringLiteral("预检必须使用已冻结候选版本"));

    std::optional<ExternalFactorDataset> dataset;
    for (const ExternalFactorDataset &item :
         store.loadExternalFactorDatasets(10000)) {
        if (item.datasetId == request.datasetId) {
            dataset = item;
            break;
        }
    }
    if (!dataset)
        return fail(QStringLiteral("授权外部因子数据集不存在"));
    const QString factorKey = candidate->factorDefinition
                                  .value(QStringLiteral("factorKey"))
                                  .toString().trimmed();
    if (factorKey.isEmpty() || dataset->factorKey != factorKey
        || dataset->factorDefinitionSha256
            != candidate->computedFactorDefinitionSha256()) {
        return fail(QStringLiteral("候选定义与授权数据集身份不一致"));
    }

    bool bound = false;
    for (const ExternalFactorDatasetBinding &item :
         store.loadExternalFactorDatasetBindings(
             candidate->candidateId, candidate->version, 10000)) {
        if (item.datasetId == dataset->datasetId
            && item.candidateContentSha256 == candidate->contentSha256
            && item.datasetContentSha256 == dataset->contentSha256) {
            bound = true;
            break;
        }
    }
    if (!bound)
        return fail(QStringLiteral("冻结候选尚未绑定该授权数据集"));

    const QVector<ExternalFactorObservation> observations =
        store.loadExternalFactorObservations(dataset->datasetId);
    result.observationCount = observations.size();
    if (observations.isEmpty())
        return fail(QStringLiteral("授权数据集没有可用观测"));
    QSet<QString> symbolSet;
    QSet<QDateTime> timestampSet;
    for (const ExternalFactorObservation &observation : observations) {
        symbolSet.insert(observation.symbol);
        timestampSet.insert(observation.timestamp);
        if (!result.firstTimestamp.isValid()
            || observation.timestamp < result.firstTimestamp) {
            result.firstTimestamp = observation.timestamp;
        }
        if (!result.lastTimestamp.isValid()
            || observation.timestamp > result.lastTimestamp) {
            result.lastTimestamp = observation.timestamp;
        }
    }
    result.symbolCount = symbolSet.size();
    result.timestampCount = timestampSet.size();
    QStringList symbols = symbolSet.values();
    std::sort(symbols.begin(), symbols.end());

    QHash<QString, QVector<Candle>> candlesBySymbol;
    for (const QString &symbol : symbols) {
        if (interrupted()) {
            result.cancelled = true;
            return fail(QStringLiteral("外部因子研究预检已取消"));
        }
        Instrument instrument;
        instrument.symbol = symbol;
        instrument.name = symbol;
        instrument.market = Market::AShare;
        instrument.venue = symbol.startsWith(QLatin1Char('6'))
            ? QStringLiteral("XSHG") : QStringLiteral("XSHE");
        const auto identity = store.loadBacktestDatasetIdentity(
            instrument, request.periodSeconds);
        if (!identity || identity->isEmpty() || identity->hasStaleCache()
            || identity->derivedBarTotal() < 1) {
            result.missingSymbols.append(symbol);
            continue;
        }
        QVector<Candle> candles = store.loadCandles(
            instrument, request.periodSeconds,
            result.firstTimestamp.addSecs(-request.periodSeconds * 2),
            result.lastTimestamp);
        candles.erase(std::remove_if(candles.begin(), candles.end(),
            [](const Candle &candle) {
                return candle.source != QStringLiteral("raw_tick_cache_v1");
            }), candles.end());
        if (candles.isEmpty()) {
            result.missingSymbols.append(symbol);
            continue;
        }
        ++result.readySymbolCount;
        candlesBySymbol.insert(symbol, candles);
    }
    if (!result.missingSymbols.isEmpty()) {
        return fail(QStringLiteral(
            "本地 Tick 派生数据覆盖不足：%1/%2 标的就绪，缺少 %3")
                        .arg(result.readySymbolCount)
                        .arg(result.symbolCount)
                        .arg(result.missingSymbols.mid(0, 20).join(",")));
    }

    const ExternalFactorResearchResult research =
        ExternalFactorResearchEngine::analyze(
            request, observations, candlesBySymbol);
    if (!research.succeeded) return fail(research.error);
    result.evaluatedFrameCount = research.summary.evaluatedFrameCount;
    result.ready = true;
    result.elapsedMilliseconds = timer.elapsed();
    return result;
}

ExternalFactorResearchExecutionResult executeResearch(
    const QString &databasePath,
    const ExternalFactorResearchRequest &request)
{
    ExternalFactorResearchExecutionResult execution;
    QElapsedTimer timer;
    timer.start();
    const auto fail = [&](const QString &message) {
        execution.error = message;
        execution.elapsedMilliseconds = timer.elapsed();
        return execution;
    };
    const auto interrupted = []() {
        QThread *thread = QThread::currentThread();
        return thread && thread->isInterruptionRequested();
    };

    if (databasePath.trimmed().isEmpty())
        return fail(QStringLiteral("本地数据库路径为空"));
    if (interrupted()) {
        execution.cancelled = true;
        return fail(QStringLiteral("外部因子研究已取消"));
    }
    SqliteLocalDataStore store;
    if (!store.initialize(databasePath))
        return fail(QStringLiteral("无法打开本地 SQLite 数据库"));

    std::optional<FactorCandidateVersion> candidate;
    for (const FactorCandidateVersion &item :
         store.loadFactorCandidateVersions(10000)) {
        if (item.candidateId == request.candidateId
            && item.version == request.candidateVersion) {
            candidate = item;
            break;
        }
    }
    if (!candidate || !candidate->isFrozen())
        return fail(QStringLiteral("外部因子研究必须使用已冻结候选版本"));

    std::optional<ExternalFactorDataset> dataset;
    for (const ExternalFactorDataset &item :
         store.loadExternalFactorDatasets(10000)) {
        if (item.datasetId == request.datasetId) {
            dataset = item;
            break;
        }
    }
    if (!dataset)
        return fail(QStringLiteral("授权外部因子数据集不存在"));
    const QString factorKey = candidate->factorDefinition
                                  .value(QStringLiteral("factorKey"))
                                  .toString().trimmed();
    if (factorKey.isEmpty() || dataset->factorKey != factorKey
        || dataset->factorDefinitionSha256
            != candidate->computedFactorDefinitionSha256()) {
        return fail(QStringLiteral("候选定义与授权数据集身份不一致"));
    }

    std::optional<ExternalFactorDatasetBinding> binding;
    for (const ExternalFactorDatasetBinding &item :
         store.loadExternalFactorDatasetBindings(
             candidate->candidateId, candidate->version, 10000)) {
        if (item.datasetId == dataset->datasetId) {
            binding = item;
            break;
        }
    }
    if (!binding
        || binding->candidateContentSha256 != candidate->contentSha256
        || binding->datasetContentSha256 != dataset->contentSha256) {
        return fail(QStringLiteral("冻结候选尚未绑定该授权数据集"));
    }

    const QVector<ExternalFactorObservation> observations =
        store.loadExternalFactorObservations(dataset->datasetId);
    if (observations.isEmpty())
        return fail(QStringLiteral("授权数据集没有可用观测"));
    QSet<QString> symbolSet;
    QDateTime firstTimestamp;
    QDateTime lastTimestamp;
    for (const ExternalFactorObservation &observation : observations) {
        symbolSet.insert(observation.symbol);
        if (!firstTimestamp.isValid() || observation.timestamp < firstTimestamp)
            firstTimestamp = observation.timestamp;
        if (!lastTimestamp.isValid() || observation.timestamp > lastTimestamp)
            lastTimestamp = observation.timestamp;
    }
    QStringList symbols = symbolSet.values();
    std::sort(symbols.begin(), symbols.end());

    QHash<QString, QVector<Candle>> candlesBySymbol;
    QVector<BacktestDatasetIdentity> identities;
    identities.reserve(symbols.size());
    for (const QString &symbol : symbols) {
        if (interrupted()) {
            execution.cancelled = true;
            return fail(QStringLiteral("外部因子研究已取消"));
        }
        Instrument instrument;
        instrument.symbol = symbol;
        instrument.name = symbol;
        instrument.market = Market::AShare;
        instrument.venue = symbol.startsWith(QLatin1Char('6'))
            ? QStringLiteral("XSHG") : QStringLiteral("XSHE");
        const auto identity = store.loadBacktestDatasetIdentity(
            instrument, request.periodSeconds);
        if (!identity || identity->isEmpty() || identity->hasStaleCache()
            || identity->derivedBarTotal() < 1) {
            return fail(QStringLiteral(
                "%1 缺少周期 %2 秒的完整本地 Tick 派生数据")
                            .arg(symbol).arg(request.periodSeconds));
        }
        QVector<Candle> candles = store.loadCandles(
            instrument, request.periodSeconds,
            firstTimestamp.addSecs(-request.periodSeconds * 2),
            lastTimestamp);
        candles.erase(std::remove_if(candles.begin(), candles.end(),
            [](const Candle &candle) {
                return candle.source != QStringLiteral("raw_tick_cache_v1");
            }), candles.end());
        if (candles.isEmpty())
            return fail(QStringLiteral("%1 的本地 Tick 派生 K 线为空").arg(symbol));
        identities.append(*identity);
        candlesBySymbol.insert(symbol, candles);
    }

    ExternalFactorResearchResult research =
        ExternalFactorResearchEngine::analyze(
            request, observations, candlesBySymbol);
    if (!research.succeeded) return fail(research.error);
    research.summary.factorId = factorKey;
    research.summary.factorVersion = candidate->version;
    research.summary.factorAlias = candidate->name;
    research.summary.preprocessing = request.preprocessing;

    ExternalFactorResearchArchiveRecord archive;
    archive.resultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    archive.completedAt = QDateTime::currentDateTimeUtc();
    archive.candidateId = candidate->candidateId;
    archive.candidateVersion = candidate->version;
    archive.candidateContentSha256 = candidate->contentSha256;
    archive.datasetId = dataset->datasetId;
    archive.datasetContentSha256 = dataset->contentSha256;
    archive.factorDefinitionSha256 = dataset->factorDefinitionSha256;
    archive.factorKey = dataset->factorKey;
    archive.periodSeconds = request.periodSeconds;
    archive.preprocessing = request.preprocessing;
    archive.costPolicy = request.costPolicy;
    archive.neutralization = request.neutralization;
    archive.outOfSamplePolicy = request.outOfSamplePolicy;
    archive.returnDatasetIdentities = identities;
    archive.summary = research.summary;
    archive.identitySha256 = archive.computedIdentitySha256();
    QString saveError;
    if (!store.saveExternalFactorResearchArchive(archive, &saveError)) {
        return fail(saveError.isEmpty()
            ? QStringLiteral("研究完成，但归档写入失败")
            : QStringLiteral("研究完成，但归档写入失败：%1").arg(saveError));
    }
    execution.succeeded = true;
    execution.archive = archive;
    execution.elapsedMilliseconds = timer.elapsed();
    return execution;
}

} // namespace

ExternalFactorResearchService::ExternalFactorResearchService(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ExternalFactorResearchExecutionResult>();
    qRegisterMetaType<ExternalFactorResearchPreflightResult>();
}

ExternalFactorResearchService::~ExternalFactorResearchService()
{
    if (!m_thread) return;
    disconnect(m_thread, nullptr, this, nullptr);
    m_thread->requestInterruption();
    m_thread->wait();
    delete m_thread;
}

void ExternalFactorResearchService::setDatabasePath(
    const QString &databasePath)
{
    if (!m_thread) m_databasePath = databasePath;
}

quint64 ExternalFactorResearchService::preflight(
    const ExternalFactorResearchRequest &request)
{
    if (m_thread || m_databasePath.isEmpty()) return 0;
    const quint64 requestId = ++m_nextRequestId;
    const QString databasePath = m_databasePath;
    const auto result = std::make_shared<ExternalFactorResearchPreflightResult>();
    QThread *thread = QThread::create(
        [databasePath, request, result]() {
        *result = executePreflight(databasePath, request);
    });
    m_thread = thread;
    connect(thread, &QThread::finished, this,
            [this, thread, requestId, result]() {
        if (m_thread == thread) m_thread = nullptr;
        emit preflightFinished(requestId, *result);
        thread->deleteLater();
    });
    thread->start();
    return requestId;
}

quint64 ExternalFactorResearchService::start(
    const ExternalFactorResearchRequest &request)
{
    if (m_thread || m_databasePath.isEmpty()) return 0;
    const quint64 requestId = ++m_nextRequestId;
    const QString databasePath = m_databasePath;
    QPointer<ExternalFactorResearchService> guard(this);
    QThread *thread = QThread::create(
        [guard, databasePath, request, requestId]() {
        const ExternalFactorResearchExecutionResult result =
            executeResearch(databasePath, request);
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, requestId, result]() {
            if (guard) emit guard->finished(requestId, result);
        }, Qt::QueuedConnection);
    });
    m_thread = thread;
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_thread == thread) m_thread = nullptr;
        thread->deleteLater();
    });
    thread->start();
    return requestId;
}

bool ExternalFactorResearchService::running() const
{
    return m_thread != nullptr;
}

void ExternalFactorResearchService::cancel()
{
    if (m_thread) m_thread->requestInterruption();
}

} // namespace pp
