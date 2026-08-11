#include "services/KlineDataFetcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QTimeZone>

#include <memory>

namespace pp {

static QString findBridgeScript(const QString &fileName)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QStringLiteral("scripts/") + fileName,
        QStringLiteral("../scripts/") + fileName,
        QStringLiteral("../../scripts/") + fileName,
        appDir + QStringLiteral("/../../scripts/") + fileName,
        appDir + QStringLiteral("/../scripts/") + fileName,
        appDir + QStringLiteral("/scripts/") + fileName,
    };

    for (const auto &path : candidates) {
        const QFileInfo fi(path);
        if (fi.exists() && fi.isFile())
            return fi.absoluteFilePath();
    }
    return {};
}

static QString cachedPython()
{
    static const QString python = []() -> QString {
        QStringList candidates;
        const QString configuredPython = qEnvironmentVariable("PPTRADER_PYTHON");
        if (!configuredPython.isEmpty()) {
            candidates.append(configuredPython);
        }
        candidates.append({QStringLiteral("python"), QStringLiteral("python3"), QStringLiteral("py")});
        for (const QString &name : candidates) {
            QProcess test;
            test.start(name, {QStringLiteral("--version")});
            if (test.waitForFinished(1000) && test.exitCode() == 0)
                return name;
        }
        return configuredPython.isEmpty() ? QStringLiteral("python") : configuredPython;
    }();
    return python;
}

KlineDataFetcher::KlineDataFetcher(KlineProviderBackend backend, QObject *parent)
    : HistoricalDataProvider(parent)
    , m_backend(backend)
{
}

KlineDataFetcher::~KlineDataFetcher()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        m_process->waitForFinished(3000);
    }
}

QString KlineDataFetcher::bridgeFileName() const
{
    switch (m_backend) {
    case KlineProviderBackend::BaoStock:
        return QStringLiteral("baostock_kline.py");
    case KlineProviderBackend::Tushare:
        return QStringLiteral("tushare_kline.py");
    case KlineProviderBackend::Mootdx:
        return QStringLiteral("mootdx_kline.py");
    }
    return {};
}

QString KlineDataFetcher::providerId() const
{
    switch (m_backend) {
    case KlineProviderBackend::BaoStock: return QStringLiteral("baostock");
    case KlineProviderBackend::Tushare: return QStringLiteral("tushare_pro");
    case KlineProviderBackend::Mootdx: return QStringLiteral("mootdx");
    }
    return QStringLiteral("unknown");
}

HistoricalProviderCapability KlineDataFetcher::capability() const
{
    HistoricalProviderCapability result;
    result.providerId = providerId();
    result.markets = {Market::AShare};
    result.venues = {QStringLiteral("上交所"), QStringLiteral("深交所"),
                     QStringLiteral("SSE"), QStringLiteral("SZSE")};
    result.configurationMessage = QStringLiteral("尚未检查运行依赖");
    result.configured = !findBridgeScript(bridgeFileName()).isEmpty();
    if (m_backend == KlineProviderBackend::Tushare) {
        result.displayName = QStringLiteral("Tushare Pro 日线");
        result.frequencies = {QStringLiteral("day")};
        result.adjustments = {QStringLiteral("none"), QStringLiteral("qfq"),
                              QStringLiteral("hfq")};
        result.earliestDate = QDate(1990, 12, 19);
        result.maximumBarsPerRequest = 5000;
        result.rateLimitPerMinute = 500;
        result.priority = 10;
        result.authenticationRequired = true;
        result.supportsSuspended = false;
        result.supportsDelisted = true;
        if (qEnvironmentVariableIsEmpty("PPTRADER_TUSHARE_TOKEN")) {
            result.configured = false;
            result.configurationMessage = QStringLiteral(
                "未配置 PPTRADER_TUSHARE_TOKEN；不会尝试访问 Tushare");
        } else {
            result.configurationMessage = QStringLiteral("Token 已由运行环境提供");
        }
    } else if (m_backend == KlineProviderBackend::BaoStock) {
        result.displayName = QStringLiteral("BaoStock 免费日线");
        result.frequencies = {QStringLiteral("day")};
        result.adjustments = {QStringLiteral("none"), QStringLiteral("qfq"),
                              QStringLiteral("hfq")};
        result.earliestDate = QDate(1990, 12, 19);
        result.maximumBarsPerRequest = 5000;
        result.priority = 20;
        result.supportsSuspended = true;
        result.supportsDelisted = true;
        result.configurationMessage = result.configured
            ? QStringLiteral("免费日线桥已就绪；运行时仍会检查 Python 包")
            : QStringLiteral("找不到 BaoStock 桥接脚本");
    } else {
        result.displayName = QStringLiteral("mootdx 兼容行情");
        result.frequencies = {QStringLiteral("1m"), QStringLiteral("5m"),
                              QStringLiteral("15m"), QStringLiteral("30m"),
                              QStringLiteral("60m"), QStringLiteral("day")};
        result.adjustments = {QStringLiteral("none")};
        result.maximumBarsPerRequest = 5000;
        result.priority = 30;
        result.supportsSuspended = false;
        result.supportsDelisted = false;
        result.configurationMessage = result.configured
            ? QStringLiteral("兼容源桥接脚本已就绪")
            : QStringLiteral("找不到 mootdx 桥接脚本");
    }
    return result;
}

void KlineDataFetcher::fetch(quint64 requestId, const Instrument &instrument,
                             const QString &freq, int count)
{
    fetchAdjusted(requestId, instrument, freq, count, QStringLiteral("none"));
}

void KlineDataFetcher::fetchAdjusted(
    quint64 requestId, const Instrument &instrument, const QString &freq,
    int count, const QString &adjustment)
{
    const HistoricalProviderCapability descriptor = capability();
    const QString normalizedAdjustment = adjustment.isEmpty()
        ? QStringLiteral("none") : adjustment;
    if (!descriptor.supports(instrument, freq, normalizedAdjustment)) {
        emit fetchError(requestId, descriptor.configured
            ? QStringLiteral("%1 不支持 %2/%3")
                  .arg(descriptor.displayName, freq, normalizedAdjustment)
            : descriptor.configurationMessage);
        return;
    }
    const QString scriptPath = findBridgeScript(bridgeFileName());
    if (scriptPath.isEmpty()) {
        emit fetchError(requestId,
                        QStringLiteral("找不到桥接脚本 %1 (CWD: %2)")
                            .arg(bridgeFileName(), QDir::currentPath()));
        return;
    }

    const QStringList args = {scriptPath, QStringLiteral("--symbol"), instrument.symbol,
                              QStringLiteral("--freq"), freq,
                              QStringLiteral("--count"), QString::number(count),
                              QStringLiteral("--adjustment"), normalizedAdjustment};

    if (m_process && m_process->state() != QProcess::NotRunning) {
        disconnect(m_process, nullptr, this, nullptr);
        m_process->terminate();
        m_process->deleteLater();
    }

    auto *process = new QProcess(this);
    m_process = process;
    const auto candles = std::make_shared<QVector<Candle>>();
    const auto providerError = std::make_shared<QString>();
    const auto timedOut = std::make_shared<bool>(false);

    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process, candles, providerError, instrument, freq,
             normalizedAdjustment]() {
        while (process->canReadLine()) {
            const QByteArray line = process->readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (!document.isObject()) continue;
            const QJsonObject object = document.object();
            if (object.contains(QStringLiteral("error"))) {
                *providerError = object.value(QStringLiteral("error")).toString();
                continue;
            }
            if (object.contains(QStringLiteral("status"))) continue;

            Candle candle;
            candle.instrument = instrument;
            candle.open = object.value(QStringLiteral("open")).toDouble();
            candle.high = object.value(QStringLiteral("high")).toDouble();
            candle.low = object.value(QStringLiteral("low")).toDouble();
            candle.close = object.value(QStringLiteral("close")).toDouble();
            candle.volume = object.value(QStringLiteral("volume")).toDouble();
            const QString dateTimeText = object.value(QStringLiteral("datetime")).toString();
            candle.startTime = QDateTime::fromString(dateTimeText, QStringLiteral("yyyy-MM-dd HH:mm"));
            if (!candle.startTime.isValid()) candle.startTime = QDateTime::fromString(dateTimeText, Qt::ISODate);
            if (!candle.startTime.isValid()) candle.startTime = QDateTime::fromString(dateTimeText, QStringLiteral("yyyy-MM-dd"));
            if (!candle.startTime.isValid()) candle.startTime = QDateTime::fromString(dateTimeText, QStringLiteral("yyyyMMdd"));
            if (candle.startTime.isValid()) {
                candle.startTime = QDateTime(candle.startTime.date(), candle.startTime.time(),
                                             QTimeZone("Asia/Shanghai"));
            }
            if (freq == QStringLiteral("day")) candle.periodSeconds = 86400;
            else if (freq == QStringLiteral("1m")) candle.periodSeconds = 60;
            else if (freq == QStringLiteral("5m")) candle.periodSeconds = 300;
            else if (freq == QStringLiteral("15m")) candle.periodSeconds = 900;
            else if (freq == QStringLiteral("30m")) candle.periodSeconds = 1800;
            else if (freq == QStringLiteral("60m")) candle.periodSeconds = 3600;
            candle.source = object.value(QStringLiteral("source"))
                                .toString(providerId());
            candle.adjustment = object.value(QStringLiteral("adjustment"))
                                    .toString(normalizedAdjustment);
            candle.downloadedAt = QDateTime::currentDateTimeUtc();
            if (candle.open > 0.0 && candle.close > 0.0) candles->append(candle);
        }
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, process, requestId](QProcess::ProcessError) {
        emit fetchError(requestId, QStringLiteral("K线进程错误: %1").arg(process->errorString()));
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, requestId, candles, providerError, timedOut](
                int exitCode, QProcess::ExitStatus exitStatus) {
        if (*timedOut) {
            emit fetchError(requestId, QStringLiteral("K线 Provider 请求超时"));
        } else if (exitStatus == QProcess::CrashExit) {
            emit fetchError(requestId, QStringLiteral("K线进程崩溃"));
        } else if (exitCode != 0) {
            emit fetchError(requestId, QStringLiteral("K线进程退出, 代码: %1").arg(exitCode));
        } else if (!providerError->isEmpty()) {
            emit fetchError(requestId, *providerError);
        } else if (!candles->isEmpty()) {
            emit candlesReady(requestId, *candles);
        } else {
            emit fetchError(requestId, QStringLiteral("未获取到K线数据"));
        }
        if (m_process == process) m_process = nullptr;
        process->deleteLater();
    });

    process->start(cachedPython(), args);
    QPointer<QProcess> guardedProcess(process);
    QTimer::singleShot(45000, this, [guardedProcess, timedOut]() {
        if (guardedProcess && guardedProcess->state() != QProcess::NotRunning) {
            *timedOut = true;
            guardedProcess->kill();
        }
    });
}

} // namespace pp
