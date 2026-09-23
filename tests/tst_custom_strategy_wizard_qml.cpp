#include "strategy/CustomStrategyValidator.h"

#include <QGuiApplication>
#include <QAccessible>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>

#include <memory>

namespace pp {

class WizardControllerStub final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap customStrategySchema READ customStrategySchema CONSTANT)
    Q_PROPERTY(QObject *customStrategyModel READ self CONSTANT)
    Q_PROPERTY(QObject *candleModel READ self CONSTANT)
    Q_PROPERTY(QVariantList validationRuns READ validationRuns CONSTANT)
    Q_PROPERTY(QVariantList localBacktestDataCatalog READ localBacktestDataCatalog
               NOTIFY localBacktestDataCatalogChanged)
    Q_PROPERTY(QVariantList aShareIndustryImportHistory READ emptyList CONSTANT)
    Q_PROPERTY(bool selectedBacktestDataAvailable READ selectedBacktestDataAvailable CONSTANT)
    Q_PROPERTY(QString selectedBacktestSymbol READ selectedBacktestSymbol CONSTANT)
    Q_PROPERTY(QString selectedBacktestFrequency READ selectedBacktestFrequency CONSTANT)
    Q_PROPERTY(int selectedBacktestPeriodSeconds READ selectedBacktestPeriodSeconds CONSTANT)
    Q_PROPERTY(int selectedBacktestCandleCount READ selectedBacktestCandleCount CONSTANT)
    Q_PROPERTY(QString selectedBacktestInstrumentTitle READ selectedBacktestInstrumentTitle CONSTANT)
    Q_PROPERTY(QDateTime selectedBacktestFirstTime READ emptyDateTime CONSTANT)
    Q_PROPERTY(QDateTime selectedBacktestLastTime READ emptyDateTime CONSTANT)
    Q_PROPERTY(QString backtestInstrumentSelectionError READ emptyString CONSTANT)
    Q_PROPERTY(int backtestBarCount READ selectedBacktestCandleCount CONSTANT)
    Q_PROPERTY(QVariantMap backtestResultConfiguration READ backtestResultConfiguration
               NOTIFY backtestStateChanged)
    Q_PROPERTY(QString backtestError READ backtestError NOTIFY backtestStateChanged)
    Q_PROPERTY(QString backtestStatusText READ backtestStatusText NOTIFY backtestStateChanged)
    Q_PROPERTY(QString backtestResultStrategyId READ backtestResultStrategyId
               NOTIFY backtestStateChanged)
    Q_PROPERTY(QString backtestResultSymbol READ backtestResultSymbol
               NOTIFY backtestStateChanged)
    Q_PROPERTY(QString customStrategyError READ customStrategyError
               NOTIFY customStrategyStateChanged)
    Q_PROPERTY(bool customStrategyPreviewLoading READ customStrategyPreviewLoading NOTIFY customStrategyPreviewStateChanged)
    Q_PROPERTY(qulonglong customStrategyPreviewRequestId READ customStrategyPreviewRequestId NOTIFY customStrategyPreviewStateChanged)
    Q_PROPERTY(qulonglong customStrategyPreviewResolvedRequestId READ customStrategyPreviewResolvedRequestId NOTIFY customStrategyPreviewStateChanged)
    Q_PROPERTY(QVariantMap customStrategyPreviewResult READ customStrategyPreviewResult NOTIFY customStrategyPreviewStateChanged)
    Q_PROPERTY(QString customStrategyPreviewError READ customStrategyPreviewError NOTIFY customStrategyPreviewStateChanged)
    Q_PROPERTY(bool customStrategySaveLoading READ customStrategySaveLoading NOTIFY customStrategySaveStateChanged)
    Q_PROPERTY(qulonglong customStrategySaveRequestId READ customStrategySaveRequestId NOTIFY customStrategySaveStateChanged)
    Q_PROPERTY(qulonglong customStrategySaveResolvedRequestId READ customStrategySaveResolvedRequestId NOTIFY customStrategySaveStateChanged)
    Q_PROPERTY(QVariantMap customStrategySaveResult READ customStrategySaveResult NOTIFY customStrategySaveStateChanged)
    Q_PROPERTY(QString customStrategySaveError READ customStrategySaveError NOTIFY customStrategySaveStateChanged)
    Q_PROPERTY(bool backtestRunning READ backtestRunning NOTIFY backtestRunningChanged)
    Q_PROPERTY(double totalReturnPercent READ totalReturnPercent NOTIFY backtestStateChanged)
    Q_PROPERTY(int backtestTradeCount READ backtestTradeCount NOTIFY backtestStateChanged)
    Q_PROPERTY(int count READ zeroInt CONSTANT)
    Q_PROPERTY(bool crossSectionalBacktestRunning READ falseValue
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(bool batchBacktestRunning READ falseValue CONSTANT)
    Q_PROPERTY(bool portfolioBacktestRunning READ falseValue CONSTANT)
    Q_PROPERTY(QString crossSectionalBacktestStatusText READ crossSectionalBacktestStatusText
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalBacktestError READ emptyString
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantMap crossSectionalRiskConfiguration
               READ crossSectionalRiskConfiguration
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalRiskConfigurationError
               READ crossSectionalRiskConfigurationError
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantList crossSectionalFactorDefinitionCatalog READ emptyList
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantMap crossSectionalResultConfiguration READ emptyMap
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QAbstractItemModel *crossSectionalEquityCurveModel READ emptyModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *crossSectionalTradeModel READ emptyModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *crossSectionalArchiveModel READ emptyModel CONSTANT)
    Q_PROPERTY(int crossSectionalArchiveCount READ zeroInt
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(bool crossSectionalArchiveLoading READ falseValue
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalArchiveError READ emptyString
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QAbstractItemModel *crossSectionalFactorComparisonModel READ emptyModel CONSTANT)
    Q_PROPERTY(int crossSectionalFactorComparisonCount READ zeroInt
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(int crossSectionalMultipleTestingCandidateCount READ zeroInt
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalMultipleTestingCandidateSetSha256
               READ emptyString NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalMultipleTestingStatusText
               READ emptyString NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalFactorComparisonError READ emptyString
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString selectedCrossSectionalArchiveId READ emptyString
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString selectedFactorCandidateId READ emptyString CONSTANT)
    Q_PROPERTY(int selectedFactorCandidateVersion READ zeroInt CONSTANT)
    Q_PROPERTY(QString factorCandidateGovernanceStatusText READ emptyString CONSTANT)
    Q_PROPERTY(QString factorCandidateGovernanceError READ emptyString CONSTANT)
    Q_PROPERTY(QVariantList selectedFactorCandidateDatasetBindings READ emptyList CONSTANT)
    Q_PROPERTY(bool crossSectionalResultHistoricalSnapshot READ falseValue
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(bool crossSectionalReportExporting READ falseValue
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(qulonglong crossSectionalReportExportRequestId READ zeroRequestId
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalReportExportStatusText READ emptyString
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalReportExportError READ emptyString
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalReportExportPath READ emptyString
               NOTIFY crossSectionalBacktestStateChanged)

public:
    explicit WizardControllerStub(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    QVariantMap customStrategySchema() const { return m_validator.schemaDescriptor(); }
    QObject *self() { return this; }
    QVariantList validationRuns() const { return {}; }
    QVariantList emptyList() const { return {}; }
    QVariantList localBacktestDataCatalog() const { return m_localCatalog; }
    bool selectedBacktestDataAvailable() const { return true; }
    QString selectedBacktestSymbol() const { return QStringLiteral("600519"); }
    QString selectedBacktestFrequency() const { return QStringLiteral("5m"); }
    int selectedBacktestPeriodSeconds() const { return 300; }
    int selectedBacktestCandleCount() const { return 500; }
    QString selectedBacktestInstrumentTitle() const { return QStringLiteral("贵州茅台 · 600519"); }
    QDateTime emptyDateTime() const { return {}; }
    QString emptyString() const { return {}; }
    QVariantMap backtestResultConfiguration() const { return m_backtestResultConfiguration; }
    QString backtestError() const { return m_backtestError; }
    QString backtestStatusText() const { return m_backtestStatusText; }
    QString backtestResultStrategyId() const { return m_backtestResultStrategyId; }
    QString backtestResultSymbol() const { return m_backtestResultSymbol; }
    QString customStrategyError() const { return m_customStrategyError; }
    bool customStrategyPreviewLoading() const { return m_previewLoading; }
    qulonglong customStrategyPreviewRequestId() const { return m_previewRequestId; }
    qulonglong customStrategyPreviewResolvedRequestId() const { return m_previewResolvedRequestId; }
    QVariantMap customStrategyPreviewResult() const { return m_previewResult; }
    QString customStrategyPreviewError() const { return m_previewError; }
    bool customStrategySaveLoading() const { return m_saveLoading; }
    qulonglong customStrategySaveRequestId() const { return m_saveRequestId; }
    qulonglong customStrategySaveResolvedRequestId() const { return m_saveResolvedRequestId; }
    QVariantMap customStrategySaveResult() const { return m_saveResult; }
    QString customStrategySaveError() const { return m_customStrategyError; }
    bool backtestRunning() const { return m_backtestRunning; }
    double totalReturnPercent() const { return m_totalReturnPercent; }
    int backtestTradeCount() const { return m_backtestTradeCount; }
    int zeroInt() const { return 0; }
    qulonglong zeroRequestId() const { return 0; }
    bool falseValue() const { return false; }
    QString crossSectionalBacktestStatusText() const { return QStringLiteral("未运行"); }
    QVariantMap crossSectionalRiskConfiguration() const
    {
        return {{QStringLiteral("maximumLegWeight"), m_maximumLegWeight},
                {QStringLiteral("maximumActiveLegs"), m_maximumActiveLegs},
                {QStringLiteral("maximumConsecutiveMissingBars"),
                 m_maximumConsecutiveMissingBars},
                {QStringLiteral("maximumPortfolioLossPercent"),
                 m_maximumPortfolioLossPercent},
                {QStringLiteral("maximumPortfolioDrawdownPercent"),
                 m_maximumPortfolioDrawdownPercent},
                {QStringLiteral("maximumIndustryWeight"),
                 m_maximumIndustryWeight},
                {QStringLiteral("industryDatasetId"),
                 m_industryDatasetId},
                {QStringLiteral("factorExposureAlias"),
                 m_factorExposureAlias},
                {QStringLiteral("maximumAbsoluteFactorExposure"),
                 m_maximumAbsoluteFactorExposure},
                {QStringLiteral("factorResearchAlias"),
                 m_factorResearchAlias},
                {QStringLiteral("factorPreprocessing"), QVariantMap{
                     {QStringLiteral("missingValuePolicy"), QStringLiteral("pairwise_drop")},
                     {QStringLiteral("winsorizationPolicy"), QStringLiteral("percentile_clip")},
                     {QStringLiteral("lowerQuantile"), 0.01},
                     {QStringLiteral("upperQuantile"), 0.99},
                     {QStringLiteral("minimumPairs"), 3}}},
                {QStringLiteral("factorResearchCost"), QVariantMap{
                     {QStringLiteral("oneWayCostBps"), m_factorResearchCostBps}}},
                {QStringLiteral("tPlusOne"), true}};
    }
    QString crossSectionalRiskConfigurationError() const
    {
        return m_crossSectionalRiskConfigurationError;
    }
    QVariantMap emptyMap() const { return {}; }
    QAbstractItemModel *emptyModel() { return &m_emptyModel; }
    void setLocalCatalog(const QVariantList &catalog)
    {
        m_localCatalog = catalog;
        emit localBacktestDataCatalogChanged();
    }
    QString lastCrossSectionalJson() const { return m_lastCrossSectionalJson; }
    int lastCrossSectionalPeriod() const { return m_lastCrossSectionalPeriod; }
    int lastCrossSectionalBars() const { return m_lastCrossSectionalBars; }
    void setSaveFailure(const QString &error) { m_saveFailure = error; }
    void setBacktestCompletionFailure(const QString &error)
    {
        m_backtestCompletionFailure = error;
    }

    Q_INVOKABLE int indexOf(const QString &strategyId, int version) const
    {
        return strategyId == m_savedStrategyId
            && (version <= 0 || version == m_savedStrategyVersion) ? 0 : -1;
    }
    Q_INVOKABLE QVariantMap get(int index) const
    {
        return index == 0 && !m_savedStrategyId.isEmpty()
            ? QVariantMap{{QStringLiteral("strategyId"), m_savedStrategyId},
                          {QStringLiteral("version"), m_savedStrategyVersion}}
            : QVariantMap{};
    }
    Q_INVOKABLE bool backtestResultMatchesCustomStrategy(
        const QString &strategyId, int version) const
    {
        return !m_backtestResultConfiguration.isEmpty()
            && strategyId == m_backtestResultStrategyId
            && version == m_backtestResultVersion;
    }
    Q_INVOKABLE QVariantMap customStrategyLocalRunAvailability(
        const QString &strategyId, int version, const QVariantMap &) const
    {
        const bool available = strategyId == m_savedStrategyId
            && version == m_savedStrategyVersion && !m_backtestRunning;
        return {{QStringLiteral("available"), available},
                {QStringLiteral("error"), available
                    ? QString{} : QStringLiteral("测试未保存版本")}};
    }
    Q_INVOKABLE QVariantMap validateCustomStrategyJson(const QString &json) const
    {
        CustomStrategyDefinition definition;
        QString error;
        const bool valid = m_validator.parseAndValidate(json, &definition, &error);
        return {{QStringLiteral("valid"), valid}, {QStringLiteral("error"), error}};
    }
    Q_INVOKABLE QVariantMap analyzeCustomStrategyJson(const QString &json,
                                                       const QVariantMap &) const
    {
        const QVariantMap validation = validateCustomStrategyJson(json);
        return {{QStringLiteral("executable"), validation.value(QStringLiteral("valid"))},
                {QStringLiteral("error"), validation.value(QStringLiteral("error"))}};
    }
    Q_INVOKABLE QVariantMap customStrategyPreview(
        const QString &, const QString &symbol, int periodSeconds, int) const
    {
        return {{QStringLiteral("symbol"), symbol},
                {QStringLiteral("periodSeconds"), periodSeconds},
                {QStringLiteral("source"), QStringLiteral("raw_tick_cache_v1")},
                {QStringLiteral("barCount"), 2},
                {QStringLiteral("bars"), QVariantList{
                    QVariantMap{{QStringLiteral("c"), 10.0}},
                    QVariantMap{{QStringLiteral("c"), 11.0}}}}};
    }
    Q_INVOKABLE qulonglong requestCustomStrategyPreview(
        const QString &, const QString &symbol, int periodSeconds, int)
    {
        const quint64 requestId = ++m_previewRequestId;
        m_previewLoading = true;
        m_previewResult.clear();
        m_previewError.clear();
        emit customStrategyPreviewStateChanged();
        QTimer::singleShot(0, this, [this, requestId, symbol, periodSeconds] {
            if (requestId != m_previewRequestId) return;
            m_previewLoading = false;
            m_previewResolvedRequestId = requestId;
            m_previewResult = {{QStringLiteral("symbol"), symbol},
                {QStringLiteral("periodSeconds"), periodSeconds},
                {QStringLiteral("source"), QStringLiteral("raw_tick_cache_v1")},
                {QStringLiteral("barCount"), 2},
                {QStringLiteral("bars"), QVariantList{
                    QVariantMap{{QStringLiteral("c"), 10.0}},
                    QVariantMap{{QStringLiteral("c"), 11.0}}}}};
            emit customStrategyPreviewStateChanged();
        });
        return requestId;
    }
    Q_INVOKABLE bool saveCustomStrategyJson(const QString &json)
    {
        if (!m_saveFailure.isEmpty()) {
            m_customStrategyError = m_saveFailure;
            emit customStrategyStateChanged();
            return false;
        }
        CustomStrategyDefinition definition;
        if (!m_validator.parseAndValidate(json, &definition, &m_customStrategyError)) {
            emit customStrategyStateChanged();
            return false;
        }
        m_savedStrategyId = definition.strategyId;
        m_savedStrategyVersion = definition.version;
        m_customStrategyError.clear();
        emit customStrategyStateChanged();
        return true;
    }
    Q_INVOKABLE qulonglong requestSaveCustomStrategyJson(const QString &json)
    {
        const quint64 requestId = ++m_saveRequestId;
        m_saveLoading = true;
        m_saveResult.clear();
        emit customStrategySaveStateChanged();
        QTimer::singleShot(0, this, [this, requestId, json] {
            if (requestId != m_saveRequestId) return;
            m_saveLoading = false;
            m_saveResolvedRequestId = requestId;
            if (!saveCustomStrategyJson(json)) {
                m_saveResult = {{QStringLiteral("error"), m_customStrategyError}};
            } else {
                m_saveResult = {{QStringLiteral("success"), true},
                    {QStringLiteral("strategyId"), m_savedStrategyId},
                    {QStringLiteral("version"), m_savedStrategyVersion}};
            }
            emit customStrategySaveStateChanged();
        });
        return requestId;
    }
    Q_INVOKABLE double runSelectedLocalCustomStrategyBacktest(
        const QString &strategyId, int version, const QVariantMap &)
    {
        if (strategyId != m_savedStrategyId || version != m_savedStrategyVersion)
            return 0;
        m_backtestRunning = true;
        m_backtestStatusText = QStringLiteral("严格本地回测运行中");
        emit backtestRunningChanged();
        emit backtestStateChanged();
        QTimer::singleShot(0, this, [this, strategyId, version] {
            m_backtestRunning = false;
            if (!m_backtestCompletionFailure.isEmpty()) {
                m_backtestError = m_backtestCompletionFailure;
                m_backtestStatusText = QStringLiteral("严格本地回测失败");
                emit backtestRunningChanged();
                emit backtestStateChanged();
                return;
            }
            m_backtestStatusText = QStringLiteral("严格本地回测完成");
            m_backtestResultStrategyId = strategyId;
            m_backtestResultVersion = version;
            m_backtestResultSymbol = selectedBacktestSymbol();
            m_backtestResultConfiguration = {
                {QStringLiteral("dataVersion"), QString(64, QLatin1Char('a'))}};
            m_totalReturnPercent = 3.25;
            m_backtestTradeCount = 4;
            emit backtestRunningChanged();
            emit backtestStateChanged();
        });
        return 73;
    }
    Q_INVOKABLE void setBacktestSampleWindow(const QDate &, const QDate &) {}
    Q_INVOKABLE void clearBacktestSampleWindow() {}
    Q_INVOKABLE bool loadValidationRun(int) { return false; }
    Q_INVOKABLE bool saveValidationRun(const QString &) { return false; }
    Q_INVOKABLE QVariantMap replayBacktestBar(int) const { return {}; }
    Q_INVOKABLE void refreshLocalBacktestDataCatalog() {}
    Q_INVOKABLE bool selectLocalBacktestInstrument(const QString &, const QString &, int)
    {
        return false;
    }
    Q_INVOKABLE QVariantMap validateCrossSectionalBacktestRequest(
        const QString &json, int barCount, int periodSeconds) const
    {
        CustomStrategyDefinition definition;
        QString error;
        if (!m_validator.parseAndValidate(json, &definition, &error)) {
            return {{QStringLiteral("valid"), false},
                    {QStringLiteral("error"), error}};
        }
        const QJsonArray symbols = definition.document.value(QStringLiteral("universe"))
            .toObject().value(QStringLiteral("symbols")).toArray();
        for (const QJsonValue &value : symbols) {
            bool found = false;
            for (const QVariant &itemValue : m_localCatalog) {
                const QVariantMap item = itemValue.toMap();
                if (item.value(QStringLiteral("symbol")).toString() == value.toString()
                    && item.value(QStringLiteral("periodSeconds")).toInt() == periodSeconds
                    && item.value(QStringLiteral("candleCount")).toInt() >= barCount) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return {{QStringLiteral("valid"), false},
                        {QStringLiteral("error"), QStringLiteral("测试目录缺少同周期数据")}};
            }
        }
        return {{QStringLiteral("valid"), symbols.size() >= 2},
                {QStringLiteral("error"), symbols.size() >= 2
                    ? QString{} : QStringLiteral("至少需要两个标的")}};
    }
    Q_INVOKABLE bool setCrossSectionalRiskConfiguration(
        double maximumLegWeight, int maximumActiveLegs,
        int maximumConsecutiveMissingBars)
    {
        if (!qIsFinite(maximumLegWeight) || maximumLegWeight <= 0.0
            || maximumLegWeight > 1.0 || maximumActiveLegs < 0
            || maximumConsecutiveMissingBars < 0) {
            m_crossSectionalRiskConfigurationError =
                QStringLiteral("测试风险配置无效");
            emit crossSectionalBacktestStateChanged();
            return false;
        }
        m_maximumLegWeight = maximumLegWeight;
        m_maximumActiveLegs = maximumActiveLegs;
        m_maximumConsecutiveMissingBars = maximumConsecutiveMissingBars;
        m_crossSectionalRiskConfigurationError.clear();
        emit crossSectionalBacktestStateChanged();
        return true;
    }
    Q_INVOKABLE bool setCrossSectionalCircuitBreakerConfiguration(
        double maximumPortfolioLossPercent,
        double maximumPortfolioDrawdownPercent)
    {
        if (!qIsFinite(maximumPortfolioLossPercent)
            || maximumPortfolioLossPercent < 0.0
            || maximumPortfolioLossPercent > 100.0
            || !qIsFinite(maximumPortfolioDrawdownPercent)
            || maximumPortfolioDrawdownPercent < 0.0
            || maximumPortfolioDrawdownPercent > 100.0) {
            m_crossSectionalRiskConfigurationError =
                QStringLiteral("测试熔断配置无效");
            emit crossSectionalBacktestStateChanged();
            return false;
        }
        m_maximumPortfolioLossPercent = maximumPortfolioLossPercent;
        m_maximumPortfolioDrawdownPercent = maximumPortfolioDrawdownPercent;
        m_crossSectionalRiskConfigurationError.clear();
        emit crossSectionalBacktestStateChanged();
        return true;
    }
    Q_INVOKABLE bool setCrossSectionalIndustryConfiguration(
        double maximumIndustryWeight, const QString &industryDatasetId)
    {
        const QString normalizedDatasetId = industryDatasetId.trimmed();
        if (!qIsFinite(maximumIndustryWeight)
            || maximumIndustryWeight < 0.0
            || maximumIndustryWeight > 1.0
            || (maximumIndustryWeight > 0.0
                && normalizedDatasetId.isEmpty())) {
            m_crossSectionalRiskConfigurationError =
                QStringLiteral("测试行业风险配置无效");
            emit crossSectionalBacktestStateChanged();
            return false;
        }
        m_maximumIndustryWeight = maximumIndustryWeight;
        m_industryDatasetId = normalizedDatasetId;
        m_crossSectionalRiskConfigurationError.clear();
        emit crossSectionalBacktestStateChanged();
        return true;
    }
    Q_INVOKABLE bool setCrossSectionalFactorExposureConfiguration(
        double maximumAbsoluteFactorExposure, const QString &factorExposureAlias)
    {
        const QString normalizedAlias = factorExposureAlias.trimmed();
        if (!qIsFinite(maximumAbsoluteFactorExposure)
            || maximumAbsoluteFactorExposure < 0.0
            || maximumAbsoluteFactorExposure > 10.0
            || (maximumAbsoluteFactorExposure > 0.0
                && normalizedAlias.isEmpty())) {
            m_crossSectionalRiskConfigurationError =
                QStringLiteral("测试因子暴露配置无效");
            emit crossSectionalBacktestStateChanged();
            return false;
        }
        m_maximumAbsoluteFactorExposure = maximumAbsoluteFactorExposure;
        m_factorExposureAlias = maximumAbsoluteFactorExposure > 0.0
            ? normalizedAlias : QString{};
        m_crossSectionalRiskConfigurationError.clear();
        emit crossSectionalBacktestStateChanged();
        return true;
    }
    Q_INVOKABLE bool setCrossSectionalFactorResearchConfiguration(
        const QString &factorAlias, double lowerPercentile,
        double upperPercentile, int minimumPairs, double oneWayCostBps,
        bool industryNeutralization, int minimumIndustryGroupSize)
    {
        const QString alias = factorAlias.trimmed();
        if (!qIsFinite(lowerPercentile) || !qIsFinite(upperPercentile)
            || lowerPercentile < 0.0 || lowerPercentile >= upperPercentile
            || upperPercentile > 100.0 || minimumPairs < 2
            || !qIsFinite(oneWayCostBps)
            || oneWayCostBps < 0.0 || oneWayCostBps > 1000.0
            || minimumIndustryGroupSize < 2
            || (industryNeutralization && m_industryDatasetId.isEmpty())) {
            m_crossSectionalRiskConfigurationError =
                QStringLiteral("测试因子研究配置无效");
            emit crossSectionalBacktestStateChanged();
            return false;
        }
        m_factorResearchAlias = alias;
        m_factorResearchCostBps = oneWayCostBps;
        m_crossSectionalRiskConfigurationError.clear();
        emit crossSectionalBacktestStateChanged();
        return true;
    }
    Q_INVOKABLE qulonglong runCrossSectionalBacktestForPeriod(
        const QString &json, int barCount, int periodSeconds)
    {
        const QVariantMap validation = validateCrossSectionalBacktestRequest(
            json, barCount, periodSeconds);
        if (!validation.value(QStringLiteral("valid")).toBool()) return 0;
        m_lastCrossSectionalJson = json;
        m_lastCrossSectionalBars = barCount;
        m_lastCrossSectionalPeriod = periodSeconds;
        emit crossSectionalBacktestStateChanged();
        return 91;
    }
    Q_INVOKABLE void cancelCrossSectionalBacktest() {}
    Q_INVOKABLE bool loadCrossSectionalBacktestArchive(const QString &) { return false; }
    Q_INVOKABLE void refreshCrossSectionalBacktestArchiveList() {}
    Q_INVOKABLE bool toggleCrossSectionalFactorComparison(const QString &) { return false; }
    Q_INVOKABLE bool isCrossSectionalFactorComparisonSelected(const QString &) const
    {
        return false;
    }
    Q_INVOKABLE void clearCrossSectionalFactorComparison() {}
    Q_INVOKABLE qulonglong exportCrossSectionalFactorReport(const QString &)
    {
        return 0;
    }

signals:
    void backtestInstrumentChanged();
    void backtestConfigurationChanged();
    void backtestRunningChanged();
    void backtestStateChanged();
    void customStrategyStateChanged();
    void customStrategyPreviewStateChanged();
    void customStrategySaveStateChanged();
    void localBacktestDataCatalogChanged();
    void crossSectionalBacktestStateChanged();

private:
    CustomStrategyValidator m_validator;
    QString m_savedStrategyId;
    int m_savedStrategyVersion = 0;
    QString m_customStrategyError;
    bool m_backtestRunning = false;
    QString m_backtestError;
    QString m_backtestStatusText;
    QString m_backtestResultStrategyId;
    int m_backtestResultVersion = 0;
    QString m_backtestResultSymbol;
    QVariantMap m_backtestResultConfiguration;
    double m_totalReturnPercent = 0.0;
    int m_backtestTradeCount = 0;
    QString m_saveFailure;
    bool m_previewLoading = false;
    quint64 m_previewRequestId = 0;
    quint64 m_previewResolvedRequestId = 0;
    QVariantMap m_previewResult;
    QString m_previewError;
    bool m_saveLoading = false;
    quint64 m_saveRequestId = 0;
    quint64 m_saveResolvedRequestId = 0;
    QVariantMap m_saveResult;
    QString m_backtestCompletionFailure;
    QVariantList m_localCatalog;
    QString m_lastCrossSectionalJson;
    int m_lastCrossSectionalPeriod = 0;
    int m_lastCrossSectionalBars = 0;
    QString m_crossSectionalRiskConfigurationError;
    double m_maximumLegWeight = 0.35;
    int m_maximumActiveLegs = 10;
    int m_maximumConsecutiveMissingBars = 5;
    double m_maximumPortfolioLossPercent = 0.0;
    double m_maximumPortfolioDrawdownPercent = 0.0;
    double m_maximumIndustryWeight = 0.0;
    QString m_industryDatasetId;
    double m_maximumAbsoluteFactorExposure = 0.0;
    QString m_factorExposureAlias;
    QString m_factorResearchAlias;
    double m_factorResearchCostBps = 0.0;
    QStandardItemModel m_emptyModel;
};

class CustomStrategyWizardQmlTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void restoresLegacyDraftThroughActualQmlAndBuildsSchemaBackedJson();
    void invalidMacdParametersBlockTheIndicatorStep();
    void validMacdEditsAppearInGeneratedJson();
    void kdjParametersSurviveActualQmlDraftRestore();
    void indicatorCatalogScrollsToLastChoiceInHalfScreenWindow();
    void conditionOperandMenusKeepMouseSelectedAliasInJson_data();
    void conditionOperandMenusKeepMouseSelectedAliasInJson();
    void embeddedWizardDoesNotClip_data();
    void embeddedWizardDoesNotClip();
    void createsNestedOrNotConditionThroughActualQml();
    void savesRunsAndCompletesStrictLocalWorkflowThroughActualQml();
    void savingEditedDraftAdvancesVersionAndKeepsSelection();
    void saveFailureDoesNotAdvanceImmutableVersion();
    void asynchronousBacktestFailureSurfacesControllerError();
    void crossSectionalPanelBuildsAndRunsValidatedV3FromLocalCatalog();

private:
    std::unique_ptr<QObject> createWizard(QQmlEngine &engine, WizardControllerStub &controller);

    QTemporaryDir m_settingsDir;
};

void CustomStrategyWizardQmlTest::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("PPTraderTests"));
    QCoreApplication::setApplicationName(QStringLiteral("CustomStrategyWizardQmlTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    QSettings().clear();
}

void CustomStrategyWizardQmlTest::embeddedWizardDoesNotClip_data()
{
    QTest::addColumn<int>("embeddedWidth");
    QTest::newRow("minimum-window-embedded") << 724;
    QTest::newRow("half-screen-embedded") << 884;
    QTest::newRow("compact") << 1280;
    QTest::newRow("wide") << 1600;
}

void CustomStrategyWizardQmlTest::embeddedWizardDoesNotClip()
{
    QFETCH(int, embeddedWidth);
    QQmlEngine engine;
    WizardControllerStub controller;
    auto wizard = createWizard(engine, controller);
    QVERIFY(wizard);
    auto *item = qobject_cast<QQuickItem *>(wizard.get());
    QVERIFY(item);
    QQuickWindow window;
    item->setParentItem(window.contentItem());
    item->setSize(QSizeF(embeddedWidth, 540));
    window.resize(embeddedWidth, 540);
    wizard->setProperty("activeStep", 4);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *conditionArea = wizard->findChild<QQuickItem *>(QStringLiteral("wizardConditionArea"));
    auto *next = wizard->findChild<QQuickItem *>(QStringLiteral("wizardNextButton"));
    QVERIFY(conditionArea);
    QVERIFY(next);
    QTRY_VERIFY_WITH_TIMEOUT(conditionArea->width() >= 500, 500);
    const QPointF buttonEnd = next->mapToItem(item, QPointF(next->width(), next->height()));
    QVERIFY2(buttonEnd.x() <= embeddedWidth, qPrintable(QString::number(buttonEnd.x())));
    QVERIFY(buttonEnd.y() <= item->height());
    if (embeddedWidth < 1360) {
        QQuickItem *previewButton = nullptr;
        for (auto *candidate : wizard->findChildren<QQuickItem *>()) {
            if (candidate->property("text").toString() == QStringLiteral("查看预览")
                && candidate->metaObject()->indexOfSignal("clicked()") >= 0) {
                previewButton = candidate;
                break;
            }
        }
        QVERIFY(previewButton);
        const QPoint previewCenter = previewButton->mapToScene(
            QPointF(previewButton->width() / 2, previewButton->height() / 2)).toPoint();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, previewCenter);
        QTRY_VERIFY_WITH_TIMEOUT(wizard->property("compactPreviewVisible").toBool(), 500);
        QVERIFY(!conditionArea->isVisible());
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!wizard->property("compactPreviewVisible").toBool(), 500);
        QVERIFY(conditionArea->isVisible());
        QCOMPARE(wizard->property("activeStep").toInt(), 4);
        QTRY_VERIFY_WITH_TIMEOUT(conditionArea->width() >= 500, 500);
    }
    item->setParentItem(nullptr);
}

void CustomStrategyWizardQmlTest::cleanup()
{
    QSettings().clear();
}

std::unique_ptr<QObject> CustomStrategyWizardQmlTest::createWizard(
    QQmlEngine &engine, WizardControllerStub &controller)
{
    const QString componentsDir = QStringLiteral(PPTRADER_SOURCE_DIR "/qml/components");
    engine.addImportPath(componentsDir);

    QQmlComponent themeComponent(&engine,
        QUrl::fromLocalFile(componentsDir + QStringLiteral("/PPTheme.qml")));
    std::unique_ptr<QObject> theme(themeComponent.create());
    if (!theme) {
        qWarning().noquote() << themeComponent.errorString();
        return {};
    }

    QQmlComponent wizardComponent(&engine,
        QUrl::fromLocalFile(componentsDir + QStringLiteral("/CustomStrategyWizard.qml")));
    QVariantMap properties {
        {QStringLiteral("theme"), QVariant::fromValue(theme.get())},
        {QStringLiteral("controller"), QVariant::fromValue(&controller)},
    };
    std::unique_ptr<QObject> wizard(wizardComponent.createWithInitialProperties(properties));
    if (!wizard)
        qWarning().noquote() << wizardComponent.errorString();
    if (wizard)
        theme.release()->setParent(wizard.get());
    return wizard;
}

void CustomStrategyWizardQmlTest::restoresLegacyDraftThroughActualQmlAndBuildsSchemaBackedJson()
{
    const QJsonObject legacyDraft {
        {QStringLiteral("strategyId"), QStringLiteral("custom_legacy_qml")},
        {QStringLiteral("name"), QStringLiteral("真实 QML 旧草稿")},
        {QStringLiteral("series"), QJsonArray{QJsonObject{
             {QStringLiteral("name"), QStringLiteral("BOLLINGER")},
             {QStringLiteral("alias"), QStringLiteral("bands")},
             {QStringLiteral("visible"), true},
        }}},
        {QStringLiteral("entry"), QJsonArray{}},
        {QStringLiteral("exit"), QJsonArray{}},
    };
    QSettings settings;
    settings.setValue(QStringLiteral("wizardDraft"),
                      QString::fromUtf8(QJsonDocument(legacyDraft).toJson(QJsonDocument::Compact)));
    settings.sync();

    QQmlEngine engine;
    WizardControllerStub controller;
    std::unique_ptr<QObject> wizard = createWizard(engine, controller);
    QVERIFY(wizard);

    QVariant json;
    const bool invoked = QMetaObject::invokeMethod(
        wizard.get(), "buildJson", Q_RETURN_ARG(QVariant, json),
        Q_ARG(QVariant, 1),
        Q_ARG(QVariant, QStringLiteral("2026-08-19T08:00:00.000Z")));
    QVERIFY(invoked);

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toString().toUtf8(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    const QJsonArray series = document.object().value(QStringLiteral("series")).toArray();
    QCOMPARE(series.size(), 1);
    const QJsonObject bollinger = series.first().toObject();
    QCOMPARE(bollinger.value(QStringLiteral("indicator")).toString(), QStringLiteral("BOLLINGER"));
    QCOMPARE(bollinger.value(QStringLiteral("period")).toInt(), 20);
    QCOMPARE(bollinger.value(QStringLiteral("standardDeviations")).toDouble(), 2.0);
    QCOMPARE(bollinger.value(QStringLiteral("output")).toString(), QStringLiteral("middle"));
}

void CustomStrategyWizardQmlTest::invalidMacdParametersBlockTheIndicatorStep()
{
    QQmlEngine engine;
    WizardControllerStub controller;
    std::unique_ptr<QObject> wizard = createWizard(engine, controller);
    QVERIFY(wizard);

    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "addIndicator", Q_ARG(QVariant, QStringLiteral("MACD"))));
    wizard->setProperty("activeStep", 3);
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "setIndicatorParameter",
        Q_ARG(QVariant, QStringLiteral("fastPeriod")), Q_ARG(QVariant, 30)));

    QVariant mayContinue;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "canLeaveCurrentStep", Q_RETURN_ARG(QVariant, mayContinue)));
    QCOMPARE(mayContinue.toBool(), false);
    QCOMPARE(wizard->property("statusText").toString(),
             QStringLiteral("快速周期必须小于慢速周期"));
}

void CustomStrategyWizardQmlTest::validMacdEditsAppearInGeneratedJson()
{
    QQmlEngine engine;
    WizardControllerStub controller;
    std::unique_ptr<QObject> wizard = createWizard(engine, controller);
    QVERIFY(wizard);

    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "addIndicator", Q_ARG(QVariant, QStringLiteral("MACD"))));
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "setIndicatorParameter",
        Q_ARG(QVariant, QStringLiteral("fastPeriod")), Q_ARG(QVariant, 8)));
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "setIndicatorParameter",
        Q_ARG(QVariant, QStringLiteral("slowPeriod")), Q_ARG(QVariant, 21)));
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "setIndicatorParameter",
        Q_ARG(QVariant, QStringLiteral("output")), Q_ARG(QVariant, QStringLiteral("signal"))));

    QVariant json;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "buildJson", Q_RETURN_ARG(QVariant, json),
        Q_ARG(QVariant, 1),
        Q_ARG(QVariant, QStringLiteral("2026-08-19T08:00:00.000Z"))));
    const QJsonDocument document = QJsonDocument::fromJson(json.toString().toUtf8());
    const QJsonArray series = document.object().value(QStringLiteral("series")).toArray();
    QVERIFY(series.size() >= 1);
    const QJsonObject macd = series.last().toObject();
    QCOMPARE(macd.value(QStringLiteral("indicator")).toString(), QStringLiteral("MACD"));
    QCOMPARE(macd.value(QStringLiteral("fastPeriod")).toInt(), 8);
    QCOMPARE(macd.value(QStringLiteral("slowPeriod")).toInt(), 21);
    QCOMPARE(macd.value(QStringLiteral("signalPeriod")).toInt(), 9);
    QCOMPARE(macd.value(QStringLiteral("output")).toString(), QStringLiteral("signal"));

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.parseAndValidate(json.toString(), &definition, &error),
             qPrintable(error + QStringLiteral("\n") + json.toString()));
}

void CustomStrategyWizardQmlTest::kdjParametersSurviveActualQmlDraftRestore()
{
    QQmlEngine engine;
    WizardControllerStub controller;
    std::unique_ptr<QObject> wizard = createWizard(engine, controller);
    QVERIFY(wizard);
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "addIndicator", Q_ARG(QVariant, QStringLiteral("KDJ"))));

    QVariant json;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "buildJson", Q_RETURN_ARG(QVariant, json), Q_ARG(QVariant, 1),
        Q_ARG(QVariant, QStringLiteral("2026-09-07T08:00:00.000Z"))));
    const QJsonObject defaults = QJsonDocument::fromJson(json.toString().toUtf8())
        .object().value(QStringLiteral("series")).toArray().last().toObject();
    QCOMPARE(defaults.value(QStringLiteral("indicator")).toString(), QStringLiteral("KDJ"));
    QCOMPARE(defaults.value(QStringLiteral("period")).toInt(), 9);
    QCOMPARE(defaults.value(QStringLiteral("kPeriod")).toInt(), 3);
    QCOMPARE(defaults.value(QStringLiteral("dPeriod")).toInt(), 3);
    QCOMPARE(defaults.value(QStringLiteral("output")).toString(), QStringLiteral("k"));

    QVERIFY(QMetaObject::invokeMethod(wizard.get(), "setIndicatorParameter",
        Q_ARG(QVariant, QStringLiteral("period")), Q_ARG(QVariant, 7)));
    QVERIFY(QMetaObject::invokeMethod(wizard.get(), "setIndicatorParameter",
        Q_ARG(QVariant, QStringLiteral("kPeriod")), Q_ARG(QVariant, 2)));
    QVERIFY(QMetaObject::invokeMethod(wizard.get(), "setIndicatorParameter",
        Q_ARG(QVariant, QStringLiteral("dPeriod")), Q_ARG(QVariant, 4)));
    QVERIFY(QMetaObject::invokeMethod(wizard.get(), "setIndicatorParameter",
        Q_ARG(QVariant, QStringLiteral("output")), Q_ARG(QVariant, QStringLiteral("j"))));
    wizard->setProperty("activeStep", 3);
    QVariant mayContinue;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "canLeaveCurrentStep", Q_RETURN_ARG(QVariant, mayContinue)));
    QVERIFY(mayContinue.toBool());

    wizard.reset();
    QCoreApplication::processEvents();
    wizard = createWizard(engine, controller);
    QVERIFY(wizard);
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "buildJson", Q_RETURN_ARG(QVariant, json), Q_ARG(QVariant, 1),
        Q_ARG(QVariant, QStringLiteral("2026-09-07T08:00:00.000Z"))));
    const QJsonArray restored = QJsonDocument::fromJson(json.toString().toUtf8())
        .object().value(QStringLiteral("series")).toArray();
    const QJsonObject expected{
        {QStringLiteral("alias"), QStringLiteral("kdj")},
        {QStringLiteral("indicator"), QStringLiteral("KDJ")},
        {QStringLiteral("period"), 7},
        {QStringLiteral("kPeriod"), 2},
        {QStringLiteral("dPeriod"), 4},
        {QStringLiteral("output"), QStringLiteral("j")},
    };
    QCOMPARE(restored.last().toObject(), expected);
    QCOMPARE(restored.first().toObject().value(QStringLiteral("indicator")).toString(),
             QStringLiteral("SMA"));
    QVERIFY(!restored.first().toObject().contains(QStringLiteral("kPeriod")));
    QVERIFY(!restored.first().toObject().contains(QStringLiteral("dPeriod")));
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(CustomStrategyValidator().parseAndValidate(json.toString(), &definition, &error),
             qPrintable(error));
}

static QQuickItem *findVisualItem(QQuickItem *root, const QString &objectName)
{
    if (root->objectName() == objectName)
        return root;
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *found = findVisualItem(child, objectName))
            return found;
    }
    return nullptr;
}

static QQuickItem *findAccessibleItem(QQuickItem *root, const QString &name)
{
    if (!root->isVisible())
        return nullptr;
    if (QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(root);
        accessible && accessible->text(QAccessible::Name) == name)
        return root;
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *found = findAccessibleItem(child, name))
            return found;
    }
    return nullptr;
}

static QRectF visibleSceneRect(QQuickItem *item)
{
    if (!item || !item->isVisible())
        return {};
    QRectF rect = item->mapRectToScene(item->boundingRect());
    for (QQuickItem *parent = item->parentItem(); parent; parent = parent->parentItem()) {
        if (!parent->isVisible())
            return {};
        if (parent->clip())
            rect = rect.intersected(parent->mapRectToScene(parent->boundingRect()));
    }
    return rect.intersected(QRectF(QPointF(), QSizeF(item->window()->size())));
}

static QQuickItem *findMenuChoice(QQuickItem *root, const QString &text)
{
    if (!root->isVisible())
        return nullptr;
    if (root->property("text").toString() == text
        && root->metaObject()->indexOfSignal("clicked()") >= 0)
        return root;
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *found = findMenuChoice(child, text))
            return found;
    }
    return nullptr;
}

static void wheelDown(QQuickWindow &window, const QPointF &position)
{
    static ulong timestamp = 10000;
    QTest::mouseMove(&window, position.toPoint());
    QWheelEvent wheel(position, window.mapToGlobal(position.toPoint()),
        QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false);
    wheel.setTimestamp(timestamp += 40);
    QCoreApplication::sendEvent(&window, &wheel);
    QTest::qWait(40);
}

static bool waitForFlickableIdle(QQuickItem *item, int timeout = 2000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout) {
        bool moving = false;
        for (QQuickItem *current = item; current; current = current->parentItem()) {
            const QVariant movingValue = current->property("moving");
            if (movingValue.isValid() && movingValue.toBool()) {
                moving = true;
                break;
            }
        }
        if (!moving)
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(20);
    }
    return false;
}

static bool scrollIntoView(QQuickWindow &window, QQuickItem *item)
{
    for (int attempt = 0; attempt < 12; ++attempt) {
        const QPointF center = item->mapToScene(item->boundingRect().center());
        if (visibleSceneRect(item).contains(center))
            return waitForFlickableIdle(item);
        QQuickItem *scrollSurface = nullptr;
        for (QQuickItem *parent = item->parentItem(); parent;
             parent = parent->parentItem()) {
            if (parent->clip()
                && parent->property("contentHeight").toDouble() > parent->height()) {
                scrollSurface = parent;
            }
        }
        if (!scrollSurface)
            return false;
        const QRectF viewport = visibleSceneRect(scrollSurface);
        if (viewport.isEmpty())
            return false;
        wheelDown(window, QPointF(viewport.left() + 3, viewport.center().y()));
    }
    const QPointF center = item->mapToScene(item->boundingRect().center());
    return visibleSceneRect(item).contains(center) && waitForFlickableIdle(item);
}

void CustomStrategyWizardQmlTest::indicatorCatalogScrollsToLastChoiceInHalfScreenWindow()
{
    QQmlEngine engine;
    QQuickWindow window;
    window.resize(1280, 540);
    WizardControllerStub controller;
    const QVariantList indicatorSchemas = controller.customStrategySchema()
        .value(QStringLiteral("indicatorSchemas")).toList();
    QCOMPARE(indicatorSchemas.size(), 18);
    const QString lastIndicator = indicatorSchemas.last().toMap()
        .value(QStringLiteral("name")).toString();
    std::unique_ptr<QObject> wizard = createWizard(engine, controller);
    QVERIFY(wizard);
    auto *wizardItem = qobject_cast<QQuickItem *>(wizard.get());
    QVERIFY(wizardItem);
    wizardItem->setParentItem(window.contentItem());
    wizardItem->setSize(QSizeF(window.size()));
    wizard->setProperty("activeStep", 3);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *addButton = wizard->findChild<QQuickItem *>(QStringLiteral("addIndicatorButton"));
    QVERIFY(addButton);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
        addButton->mapToScene(QPointF(addButton->width() / 2, addButton->height() / 2)).toPoint());
    QObject *menu = wizard->findChild<QObject *>(QStringLiteral("indicatorCatalogPopup"));
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("opened").toBool());
    auto *catalog = wizard->findChild<QQuickItem *>(QStringLiteral("indicatorCatalog"));
    QVERIFY(catalog);
    const QRectF viewport = catalog->mapRectToScene(catalog->boundingRect());
    QVERIFY(viewport.top() >= 0);
    QVERIFY2(viewport.bottom() <= window.height(), "指标菜单不能展开到窗口外");

    const QPointF wheelPosition = viewport.center();
    QTest::mouseMove(&window, wheelPosition.toPoint());
    for (int index = 0; index < 20; ++index) {
        QWheelEvent wheel(wheelPosition, window.mapToGlobal(wheelPosition.toPoint()),
            QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
            Qt::NoScrollPhase, false);
        wheel.setTimestamp(1000 + index * 40);
        QCoreApplication::sendEvent(&window, &wheel);
        QTest::qWait(40);
    }
    QQuickItem *lastChoice = nullptr;
    QTRY_VERIFY((lastChoice = findVisualItem(catalog,
        QStringLiteral("addIndicator_") + lastIndicator)) != nullptr);
    QTRY_VERIFY2(viewport.contains(lastChoice->mapToScene(
        QPointF(lastChoice->width() / 2, lastChoice->height() / 2))),
        qPrintable(QStringLiteral("末项中心 y=%1，菜单可见范围 y=%2..%3")
            .arg(lastChoice->mapToScene(QPointF(lastChoice->width() / 2,
                                               lastChoice->height() / 2)).y())
            .arg(viewport.top()).arg(viewport.bottom())));
    QVERIFY2(waitForFlickableIdle(catalog),
             "指标目录滚动未停止，不能把停止滚动误判为选择点击");
    const QPoint choiceCenter = lastChoice->mapToScene(
        QPointF(lastChoice->width() / 2, lastChoice->height() / 2)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, choiceCenter);
    QTRY_VERIFY(!menu->property("visible").toBool());

    QVariant json;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "buildJson", Q_RETURN_ARG(QVariant, json), Q_ARG(QVariant, 1),
        Q_ARG(QVariant, QStringLiteral("2026-09-07T08:00:00.000Z"))));
    const QJsonArray series = QJsonDocument::fromJson(json.toString().toUtf8())
        .object().value(QStringLiteral("series")).toArray();
    QCOMPARE(series.last().toObject().value(QStringLiteral("indicator")).toString(),
             lastIndicator);

    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
        addButton->mapToScene(QPointF(addButton->width() / 2, addButton->height() / 2)).toPoint());
    QTRY_VERIFY(menu->property("opened").toBool());
    QQuickItem *donchianChoice = nullptr;
    QTRY_VERIFY((donchianChoice = findVisualItem(catalog,
        QStringLiteral("addIndicator_DONCHIAN"))) != nullptr);
    QTRY_VERIFY(catalog->mapRectToScene(catalog->boundingRect()).contains(
        donchianChoice->mapToScene(QPointF(donchianChoice->width() / 2,
                                          donchianChoice->height() / 2))));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
        donchianChoice->mapToScene(QPointF(donchianChoice->width() / 2,
                                          donchianChoice->height() / 2)).toPoint());
    QTRY_VERIFY(!menu->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "buildJson", Q_RETURN_ARG(QVariant, json), Q_ARG(QVariant, 1),
        Q_ARG(QVariant, QStringLiteral("2026-09-07T08:00:00.000Z"))));
    const QJsonObject donchian = QJsonDocument::fromJson(json.toString().toUtf8())
        .object().value(QStringLiteral("series")).toArray().last().toObject();
    QCOMPARE(donchian.value(QStringLiteral("indicator")).toString(), QStringLiteral("DONCHIAN"));
    QCOMPARE(donchian.value(QStringLiteral("period")).toInt(), 20);
    QCOMPARE(donchian.value(QStringLiteral("output")).toString(), QStringLiteral("middle"));
}

void CustomStrategyWizardQmlTest::conditionOperandMenusKeepMouseSelectedAliasInJson_data()
{
    QTest::addColumn<QSize>("windowSize");
    QTest::addColumn<QString>("operand");
    QTest::addColumn<QString>("accessibleName");
    // 尺寸是 Wizard 本身，不是含导航栏的 PPTrader 外层窗口；
    // 不覆盖 FLOW-03 中约 884px 的窄嵌入布局。
    for (const QString &operand : {QStringLiteral("left"), QStringLiteral("right")}) {
        const QString name = operand == QStringLiteral("left")
            ? QStringLiteral("条件 2 左操作数") : QStringLiteral("条件 2 右操作数");
        QTest::newRow(qPrintable(QStringLiteral("workbench-500-") + operand))
            << QSize(1600, 500) << operand << name;
        QTest::newRow(qPrintable(QStringLiteral("half-screen-") + operand))
            << QSize(1280, 540) << operand << name;
    }
}

void CustomStrategyWizardQmlTest::conditionOperandMenusKeepMouseSelectedAliasInJson()
{
    QFETCH(QSize, windowSize);
    QFETCH(QString, operand);
    QFETCH(QString, accessibleName);
    QQmlEngine engine;
    QQuickWindow window;
    window.resize(windowSize);
    WizardControllerStub controller;
    std::unique_ptr<QObject> wizard = createWizard(engine, controller);
    QVERIFY(wizard);
    auto *wizardItem = qobject_cast<QQuickItem *>(wizard.get());
    QVERIFY(wizardItem);
    wizardItem->setParentItem(window.contentItem());
    wizardItem->setSize(QSizeF(windowSize));
    // 只准备指标目录；条件本身必须由真实下拉菜单修改。
    QVERIFY(QMetaObject::invokeMethod(wizard.get(), "addIndicator",
        Q_ARG(QVariant, QStringLiteral("BIAS"))));
    wizard->setProperty("activeStep", 4);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QVariant json;
    const QVariant timestamp = QStringLiteral("2026-09-07T08:00:00.000Z");
    QVERIFY(QMetaObject::invokeMethod(wizard.get(), "buildJson", Q_RETURN_ARG(QVariant, json),
        Q_ARG(QVariant, 1), Q_ARG(QVariant, timestamp)));
    const QJsonObject before = QJsonDocument::fromJson(json.toString().toUtf8()).object();
    QJsonArray expectedConditions = before.value(QStringLiteral("entry")).toObject()
        .value(QStringLiteral("conditions")).toArray();
    QVERIFY(!expectedConditions.isEmpty());
    QJsonObject expectedComparison = expectedConditions.first().toObject();
    expectedComparison.insert(operand, QJsonObject{{QStringLiteral("series"), QStringLiteral("bias")}});
    expectedConditions.replace(0, expectedComparison);

    if (operand == QStringLiteral("right")) {
        QQuickItem *kindCombo = nullptr;
        QTRY_VERIFY2((kindCombo = findAccessibleItem(
            wizardItem, QStringLiteral("条件 2 右值类型"))) != nullptr,
            "找不到条件 2 右值类型控件");
        QVERIFY2(scrollIntoView(window, kindCombo),
                 "滚动后条件 2 右值类型控件仍不可点击");
        const QPointF kindCenter = kindCombo->mapToScene(
            kindCombo->boundingRect().center());
        QVERIFY2(visibleSceneRect(kindCombo).contains(kindCenter),
                 "条件 2 右值类型控件不可点击");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          kindCenter.toPoint());
        QPointer<QObject> kindPopup = qvariant_cast<QObject *>(
            kindCombo->property("popup"));
        QVERIFY(kindPopup);
        QTRY_VERIFY2(kindPopup->property("opened").toBool(),
                     "右值类型菜单未打开");
        auto *kindContent = qvariant_cast<QQuickItem *>(
            kindPopup->property("contentItem"));
        QVERIFY(kindContent);
        QQuickItem *seriesChoice = nullptr;
        QTRY_VERIFY2((seriesChoice = findMenuChoice(
            kindContent, QStringLiteral("指标"))) != nullptr,
            "右值类型菜单缺少指标选项");
        const QPointF seriesCenter = seriesChoice->mapToScene(
            seriesChoice->boundingRect().center());
        QVERIFY2(visibleSceneRect(seriesChoice).contains(seriesCenter),
                 "右值类型的指标选项不可点击");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          seriesCenter.toPoint());
        QTRY_VERIFY2(!kindPopup || !kindPopup->property("visible").toBool(),
                     "选择指标右值类型后菜单未关闭");
    }

    QQuickItem *combo = nullptr;
    QTRY_VERIFY2((combo = findAccessibleItem(wizardItem, accessibleName)) != nullptr,
        qPrintable(QStringLiteral("找不到可访问的条件控件：") + accessibleName));
    // 小工作台需要先滚动页面；在最外层滚动区边缘发事件，避免内层条件树截获。
    QVERIFY2(scrollIntoView(window, combo), "滚动后条件下拉框仍不可点击");
    const QPointF comboCenter = combo->mapToScene(combo->boundingRect().center());
    QVERIFY2(visibleSceneRect(combo).contains(comboCenter), "滚动后条件下拉框仍不可点击");
    QVERIFY2(waitForFlickableIdle(combo),
             "条件页面滚动未停止，不能把停止滚动误判为打开菜单");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, comboCenter.toPoint());
    QPointer<QObject> popup = qvariant_cast<QObject *>(combo->property("popup"));
    QVERIFY(popup);
    QTRY_VERIFY2(popup && popup->property("opened").toBool(), "点击条件下拉框未打开菜单");
    auto *popupContent = qvariant_cast<QQuickItem *>(popup->property("contentItem"));
    QVERIFY(popupContent);

    QQuickItem *choice = nullptr;
    for (int attempt = 0; attempt < 16; ++attempt) {
        choice = findMenuChoice(popupContent, QStringLiteral("bias"));
        if (choice && visibleSceneRect(choice).contains(
                choice->mapToScene(choice->boundingRect().center())))
            break;
        const QRectF viewport = visibleSceneRect(popupContent);
        QVERIFY2(!viewport.isEmpty(), "条件菜单不在窗口可见区域内");
        wheelDown(window, viewport.center());
    }
    QVERIFY2(choice, "滚动条件菜单后仍找不到新增的 bias 别名选项");
    const QPointF choiceCenter = choice->mapToScene(choice->boundingRect().center());
    QVERIFY2(visibleSceneRect(choice).contains(choiceCenter), "bias 别名选项不可点击");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, choiceCenter.toPoint());
    QTRY_VERIFY2(!popup || !popup->property("visible").toBool(), "选择操作数后菜单未关闭");

    QVERIFY(QMetaObject::invokeMethod(wizard.get(), "buildJson", Q_RETURN_ARG(QVariant, json),
        Q_ARG(QVariant, 1), Q_ARG(QVariant, timestamp)));
    const QJsonObject after = QJsonDocument::fromJson(json.toString().toUtf8()).object();
    QCOMPARE(after.value(QStringLiteral("entry")).toObject()
        .value(QStringLiteral("conditions")).toArray(), expectedConditions);
    QCOMPARE(after.value(QStringLiteral("series")), before.value(QStringLiteral("series")));
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(CustomStrategyValidator().parseAndValidate(json.toString(), &definition, &error),
        qPrintable(error));
}

void CustomStrategyWizardQmlTest::createsNestedOrNotConditionThroughActualQml()
{
    QQmlEngine engine;
    WizardControllerStub controller;
    std::unique_ptr<QObject> wizard = createWizard(engine, controller);
    QVERIFY(wizard);

    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "addConditionGroup",
        Q_ARG(QVariant, QString()), Q_ARG(QVariant, QStringLiteral("or"))));
    const QVariantMap negatedPatch{{QStringLiteral("negated"), true}};
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "updateCondition",
        Q_ARG(QVariant, QStringLiteral("2")),
        Q_ARG(QVariant, negatedPatch)));
    const QVariantMap comparisonPatch{
        {QStringLiteral("leftOperand"), QStringLiteral("volume")},
        {QStringLiteral("leftKind"), QStringLiteral("field")},
        {QStringLiteral("op"), QStringLiteral("gte")},
        {QStringLiteral("rightKind"), QStringLiteral("value")},
        {QStringLiteral("rightConstant"), 100.0}};
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "updateCondition",
        Q_ARG(QVariant, QStringLiteral("2/0")),
        Q_ARG(QVariant, comparisonPatch)));

    QVariant json;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "buildJson", Q_RETURN_ARG(QVariant, json),
        Q_ARG(QVariant, 1),
        Q_ARG(QVariant, QStringLiteral("2026-08-19T08:00:00.000Z"))));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toString().toUtf8(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    const QJsonObject entry = document.object().value(QStringLiteral("entry")).toObject();
    QCOMPARE(entry.value(QStringLiteral("op")).toString(), QStringLiteral("and"));
    const QJsonArray rootConditions = entry.value(QStringLiteral("conditions")).toArray();
    QCOMPARE(rootConditions.size(), 3);
    const QJsonObject negated = rootConditions.at(2).toObject();
    QCOMPARE(negated.value(QStringLiteral("op")).toString(), QStringLiteral("not"));
    const QJsonObject nestedOr = negated.value(QStringLiteral("condition")).toObject();
    QCOMPARE(nestedOr.value(QStringLiteral("op")).toString(), QStringLiteral("or"));
    const QJsonObject comparison = nestedOr.value(QStringLiteral("conditions"))
                                       .toArray().first().toObject();
    QCOMPARE(comparison.value(QStringLiteral("left")).toObject()
                 .value(QStringLiteral("field")).toString(), QStringLiteral("volume"));
    QCOMPARE(comparison.value(QStringLiteral("right")).toObject()
                 .value(QStringLiteral("value")).toDouble(), 100.0);

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.parseAndValidate(json.toString(), &definition, &error),
             qPrintable(error + QStringLiteral("\n") + json.toString()));
}

void CustomStrategyWizardQmlTest::savesRunsAndCompletesStrictLocalWorkflowThroughActualQml()
{
    QQmlEngine engine;
    WizardControllerStub controller;
    std::unique_ptr<QObject> wizard = createWizard(engine, controller);
    QVERIFY(wizard);

    QVERIFY(QMetaObject::invokeMethod(wizard.get(), "refreshPreview"));
    QVERIFY(controller.customStrategyPreviewLoading());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.customStrategyPreviewLoading(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        wizard->property("previewData").toMap().value(QStringLiteral("barCount")).toInt() == 2,
        1000);
    const QVariantMap preview = wizard->property("previewData").toMap();
    QCOMPARE(preview.value(QStringLiteral("source")).toString(),
             QStringLiteral("raw_tick_cache_v1"));
    QCOMPARE(preview.value(QStringLiteral("barCount")).toInt(), 2);
    QCOMPARE(wizard->property("previewDirty").toBool(), false);

    QVariant saved;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "saveWorkflowVersion", Q_RETURN_ARG(QVariant, saved)));
    QCOMPARE(saved.toBool(), true);
    QVERIFY(controller.customStrategySaveLoading());
    QCOMPARE(wizard->property("savedStrategyVersion").toInt(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.customStrategySaveLoading(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(wizard->property("savedStrategyVersion").toInt(), 1, 1000);
    QCOMPARE(wizard->property("savedStrategyId").toString(),
             QStringLiteral("custom_wizard_draft"));
    QCOMPARE(wizard->property("savedStrategyVersion").toInt(), 1);

    QVariant started;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "runWorkflowBacktest", Q_RETURN_ARG(QVariant, started)));
    QCOMPARE(started.toBool(), true);
    QCOMPARE(wizard->property("workflowTaskId").toInt(), 73);
    QVERIFY(controller.backtestRunning());

    QTRY_VERIFY_WITH_TIMEOUT(!controller.backtestRunning(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(wizard->property("workflowResultMatches").toBool(), 1000);
    QCOMPARE(controller.backtestResultConfiguration()
                 .value(QStringLiteral("dataVersion")).toString(),
             QString(64, QLatin1Char('a')));
    QCOMPARE(wizard->property("statusText").toString(),
             QStringLiteral("严格本地回测完成：custom_wizard_draft v1"));
}

void CustomStrategyWizardQmlTest::saveFailureDoesNotAdvanceImmutableVersion()
{
    QQmlEngine engine;
    WizardControllerStub controller;
    controller.setSaveFailure(QStringLiteral("测试 SQLite 写入失败"));
    std::unique_ptr<QObject> wizard = createWizard(engine, controller);
    QVERIFY(wizard);

    QVariant saved;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "saveWorkflowVersion", Q_RETURN_ARG(QVariant, saved)));
    QCOMPARE(saved.toBool(), true);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.customStrategySaveLoading(), 1000);
    QCOMPARE(wizard->property("savedStrategyId").toString(), QString());
    QCOMPARE(wizard->property("savedStrategyVersion").toInt(), 0);
    QCOMPARE(wizard->property("statusText").toString(),
             QStringLiteral("测试 SQLite 写入失败"));

    QVariant started;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "runWorkflowBacktest", Q_RETURN_ARG(QVariant, started)));
    QCOMPARE(started.toBool(), false);
    QCOMPARE(wizard->property("workflowTaskId").toInt(), 0);
}

void CustomStrategyWizardQmlTest::savingEditedDraftAdvancesVersionAndKeepsSelection()
{
    QQmlEngine engine;
    WizardControllerStub controller;
    std::unique_ptr<QObject> wizard = createWizard(engine, controller);
    QVERIFY(wizard);

    QVariant firstSave;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "saveWorkflowVersion", Q_RETURN_ARG(QVariant, firstSave)));
    QCOMPARE(firstSave.toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(wizard->property("savedStrategyVersion").toInt(), 1, 1000);

    wizard->setProperty("draftDescription", QStringLiteral("第二版规则说明"));
    QVariant secondSave;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "saveWorkflowVersion", Q_RETURN_ARG(QVariant, secondSave)));
    QCOMPARE(secondSave.toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(wizard->property("savedStrategyVersion").toInt(), 2, 1000);
    QCOMPARE(wizard->property("savedStrategyId").toString(),
             QStringLiteral("custom_wizard_draft"));
    QCOMPARE(wizard->property("savedStrategyVersion").toInt(), 2);

    QVariant unchanged;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "savedDraftUnchanged", Q_RETURN_ARG(QVariant, unchanged)));
    QCOMPARE(unchanged.toBool(), true);
}

void CustomStrategyWizardQmlTest::asynchronousBacktestFailureSurfacesControllerError()
{
    QQmlEngine engine;
    WizardControllerStub controller;
    std::unique_ptr<QObject> wizard = createWizard(engine, controller);
    QVERIFY(wizard);

    QVariant saved;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "saveWorkflowVersion", Q_RETURN_ARG(QVariant, saved)));
    QCOMPARE(saved.toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(wizard->property("savedStrategyVersion").toInt(), 1, 1000);
    controller.setBacktestCompletionFailure(QStringLiteral("测试派生数据身份失效"));

    QVariant started;
    QVERIFY(QMetaObject::invokeMethod(
        wizard.get(), "runWorkflowBacktest", Q_RETURN_ARG(QVariant, started)));
    QCOMPARE(started.toBool(), true);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.backtestRunning(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(wizard->property("statusText").toString(),
                              QStringLiteral("测试派生数据身份失效"), 1000);
    QCOMPARE(wizard->property("workflowResultMatches").toBool(), false);
}

void CustomStrategyWizardQmlTest::crossSectionalPanelBuildsAndRunsValidatedV3FromLocalCatalog()
{
    auto dataset = [](const QString &symbol) {
        return QVariantMap{{QStringLiteral("symbol"), symbol},
            {QStringLiteral("name"), QStringLiteral("测试标的")},
            {QStringLiteral("frequency"), QStringLiteral("1m")},
            {QStringLiteral("periodSeconds"), 60},
            {QStringLiteral("candleCount"), 500},
            {QStringLiteral("source"), QStringLiteral("raw_tick_cache_v1")},
            {QStringLiteral("datasetVersion"), QString(64, QLatin1Char('a'))},
            {QStringLiteral("datasetStaleDayCount"), 0},
            {QStringLiteral("backtestEligible"), true}};
    };

    QQmlEngine engine;
    WizardControllerStub controller;
    controller.setLocalCatalog({dataset(QStringLiteral("600001")),
                                dataset(QStringLiteral("600002")),
                                dataset(QStringLiteral("600003"))});
    const QString componentsDir = QStringLiteral(PPTRADER_SOURCE_DIR "/qml/components");
    engine.addImportPath(componentsDir);

    QQmlComponent themeComponent(&engine,
        QUrl::fromLocalFile(componentsDir + QStringLiteral("/PPTheme.qml")));
    std::unique_ptr<QObject> theme(themeComponent.create());
    QVERIFY2(theme, qPrintable(themeComponent.errorString()));

    QQmlComponent panelComponent(&engine,
        QUrl::fromLocalFile(componentsDir
                            + QStringLiteral("/CrossSectionalBacktestPanel.qml")));
    QVariantMap properties{
        {QStringLiteral("theme"), QVariant::fromValue(theme.get())},
        {QStringLiteral("controller"), QVariant::fromValue(&controller)},
        {QStringLiteral("width"), 1200},
        {QStringLiteral("height"), 700}};
    std::unique_ptr<QObject> panel(panelComponent.createWithInitialProperties(properties));
    QVERIFY2(panel, qPrintable(panelComponent.errorString()));
    theme.release()->setParent(panel.get());

    QVERIFY(QMetaObject::invokeMethod(
        panel.get(), "selectFirstEligibleSymbols", Q_ARG(QVariant, 3)));
    QCOMPARE(panel->property("selectedSymbols").toList().size(), 3);
    QCOMPARE(panel->property("selectedPeriodSeconds").toInt(), 60);

    QVariant json;
    QVERIFY(QMetaObject::invokeMethod(
        panel.get(), "buildJson", Q_RETURN_ARG(QVariant, json),
        Q_ARG(QVariant, QStringLiteral("2026-08-20T08:00:00.000Z"))));
    CustomStrategyDefinition definition;
    QString error;
    CustomStrategyValidator validator;
    QVERIFY2(validator.parseAndValidate(json.toString(), &definition, &error),
             qPrintable(error + QStringLiteral("\n") + json.toString()));
    QCOMPARE(definition.schemaVersion, 3);
    QCOMPARE(definition.document.value(QStringLiteral("universe")).toObject()
                 .value(QStringLiteral("symbols")).toArray().size(), 3);

    QVariant validation;
    QVERIFY(QMetaObject::invokeMethod(
        panel.get(), "validateForm", Q_RETURN_ARG(QVariant, validation)));
    QVERIFY2(validation.toMap().value(QStringLiteral("valid")).toBool(),
             qPrintable(validation.toMap().value(QStringLiteral("error")).toString()));

    QVariant taskId;
    QVERIFY(QMetaObject::invokeMethod(
        panel.get(), "runCrossSectional", Q_RETURN_ARG(QVariant, taskId)));
    QCOMPARE(taskId.toULongLong(), 91ULL);
    QCOMPARE(controller.lastCrossSectionalPeriod(), 60);
    QCOMPARE(controller.lastCrossSectionalBars(), 200);
    QVERIFY(!controller.lastCrossSectionalJson().isEmpty());
    QVERIFY2(validator.parseAndValidate(controller.lastCrossSectionalJson(),
                                        &definition, &error), qPrintable(error));

    QObject *configurationCard =
        panel->findChild<QObject *>(QStringLiteral("crossSectionalConfigurationCard"));
    QVERIFY(configurationCard);
    QVERIFY(panel->findChild<QObject *>(
        QStringLiteral("crossSectionalArchiveCard")));
    QVERIFY(panel->findChild<QObject *>(
        QStringLiteral("factorResearchResultCard")));
    QVERIFY(panel->findChild<QObject *>(
        QStringLiteral("factorResearchComparisonCard")));
    QVERIFY(panel->findChild<QObject *>(
        QStringLiteral("factorResearchCostField")));
    QVERIFY(panel->findChild<QObject *>(
        QStringLiteral("factorIndustryNeutralization")));
    QVERIFY(panel->findChild<QObject *>(
        QStringLiteral("factorIndustryMinimumGroupField")));
    QCoreApplication::processEvents();
    QCOMPARE(qRound(configurationCard->property("width").toDouble()), 430);

    panel->setProperty("width", 760);
    panel->setProperty("height", 1050);
    QCoreApplication::processEvents();
    QCOMPARE(panel->property("compactLayout").toBool(), true);
    panel->setProperty("width", 1200);
    QCoreApplication::processEvents();
    QCOMPARE(panel->property("compactLayout").toBool(), false);
    QCOMPARE(qRound(configurationCard->property("width").toDouble()), 430);
}

} // namespace pp

int main(int argc, char *argv[])
{
    // PPTrader 在 QApplication 构造前固定使用 Fusion。测试必须复现同一
    // Controls 样式；Qt 6.11 的 Windows 原生样式不再支持本页使用的
    // background/contentItem/indicator 自定义，命中区域也因此不同。
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
    QGuiApplication application(argc, argv);
    pp::CustomStrategyWizardQmlTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_custom_strategy_wizard_qml.moc"
