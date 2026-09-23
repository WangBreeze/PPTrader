#include "services/MootdxMarketDataProvider.h"

#include "domain/RealtimeProviderTypes.h"
#include "services/ManagedProcessTree.h"
#include "services/MootdxCompactSnapshotParser.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryFile>

#include <algorithm>

namespace pp {

MootdxMarketDataProvider::MootdxMarketDataProvider(
    const QString &serverOverride, QObject *parent)
    : MarketDataProvider(parent)
    , m_serverOverride(serverOverride.trimmed())
{
    m_process = new QProcess(this);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &MootdxMarketDataProvider::onReadyRead);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &MootdxMarketDataProvider::onReadyReadStderr);
    connect(m_process, &QProcess::errorOccurred,
            this, &MootdxMarketDataProvider::onProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MootdxMarketDataProvider::onProcessFinished);
}

MootdxMarketDataProvider::~MootdxMarketDataProvider()
{
    stop();
}

static QString findBridgeScript()
{
    const QStringList candidates = {
        QStringLiteral("scripts/mootdx_bridge.py"),
        QStringLiteral("../scripts/mootdx_bridge.py"),
        QStringLiteral("../../scripts/mootdx_bridge.py"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../scripts/mootdx_bridge.py"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../scripts/mootdx_bridge.py"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/scripts/mootdx_bridge.py"),
    };

    for (const auto &path : candidates) {
        if (QFileInfo::exists(path)) {
            return QFileInfo(path).absoluteFilePath();
        }
    }

    return QStringLiteral("scripts/mootdx_bridge.py");
}

static const QString &cachedPython()
{
    static const QString python = []() -> QString {
        const QString configuredPython = qEnvironmentVariable("PPTRADER_PYTHON");
        if (!configuredPython.isEmpty() && QFileInfo::exists(configuredPython)) {
            return QFileInfo(configuredPython).absoluteFilePath();
        }
        const QStringList names = {
            QStringLiteral("python"),
            QStringLiteral("python3"),
            QStringLiteral("py"),
        };

        for (const auto &name : names) {
            QProcess test;
            test.start(name, {QStringLiteral("--version")});
            if (test.waitForFinished(1000) && test.exitCode() == 0) {
                return name;
            }
        }

        return {};
    }();

    return python;
}

void MootdxMarketDataProvider::warmUpPython()
{
    cachedPython();
}

void MootdxMarketDataProvider::start(const QVector<Instrument> &instruments)
{
    m_stopping = false;
    m_stdoutBuffer.clear();
    m_lastProcessDiagnostic.clear();
    m_lastProcessStderr.clear();
    m_lastReceivedCount = 0;
    m_lastBatchId.clear();
    m_lastUpdatedAt = {};
    if (instruments.isEmpty()) {
        publishState(ProviderHealthState::Error, QStringLiteral("A 股行情启动失败"),
                     QStringLiteral("没有提供标的"));
        emit providerError(QStringLiteral("没有提供标的"));
        return;
    }

    m_instrumentBySymbol.clear();
    m_instrumentByCode.clear();
    m_snapshotAssembler.setUniverse(instruments);
    QStringList symbols;
    for (const auto &inst : instruments) {
        if (inst.market == Market::AShare) {
            symbols.append(inst.symbol);
            m_instrumentBySymbol[inst.symbol] = inst;
            bool codeOk = false;
            const int symbolCode = inst.symbol.toInt(&codeOk);
            if (codeOk) m_instrumentByCode.insert(symbolCode, inst);
        }
    }

    if (symbols.isEmpty()) {
        publishState(ProviderHealthState::Error, QStringLiteral("A 股行情启动失败"),
                     QStringLiteral("没有 A 股标的"));
        emit providerError(QStringLiteral("没有 A 股标的"));
        return;
    }
    m_expectedCount = symbols.size();
    publishState(ProviderHealthState::Starting,
                 QStringLiteral("正在启动 A 股行情"));

    if (m_process->state() != QProcess::NotRunning) {
        stop();
    }

    if (!m_symbolsFilePath.isEmpty()) {
        QFile::remove(m_symbolsFilePath);
        m_symbolsFilePath.clear();
    }
    QTemporaryFile symbolsFile(
        QDir::tempPath() + QStringLiteral("/pptrader-symbols-XXXXXX.txt"));
    symbolsFile.setAutoRemove(false);
    if (!symbolsFile.open()) {
        publishState(ProviderHealthState::Error, QStringLiteral("A 股行情启动失败"),
                     QStringLiteral("无法创建 A 股股票池临时文件"));
        emit providerError(QStringLiteral("无法创建 A 股股票池临时文件"));
        return;
    }
    symbolsFile.write(symbols.join(QLatin1Char('\n')).toUtf8());
    symbolsFile.write("\n");
    m_symbolsFilePath = symbolsFile.fileName();
    symbolsFile.close();

    const QString bridgePath = findBridgeScript();
    if (!QFileInfo::exists(bridgePath)) {
        QFile::remove(m_symbolsFilePath);
        m_symbolsFilePath.clear();
        publishState(ProviderHealthState::Error, QStringLiteral("A 股行情启动失败"),
                     QStringLiteral("找不到桥接脚本: %1").arg(bridgePath));
        emit providerError(QStringLiteral("找不到桥接脚本: %1").arg(bridgePath));
        return;
    }

    const QString pythonExe = cachedPython();
    if (pythonExe.isEmpty()) {
        QFile::remove(m_symbolsFilePath);
        m_symbolsFilePath.clear();
        publishState(ProviderHealthState::Error, QStringLiteral("A 股行情启动失败"),
                     QStringLiteral("未找到可用 Python"));
        emit providerError(QStringLiteral(
            "未找到可用 Python，请安装 requirements.txt 或设置 PPTRADER_PYTHON"));
        return;
    }
    bool validInterval = false;
    const int requestedInterval = qEnvironmentVariableIntValue(
        "PPTRADER_TDX_INTERVAL_SECONDS", &validInterval);
    const int intervalSeconds = normalizedRealtimePollingIntervalSeconds(
        validInterval ? requestedInterval : 60);
    QStringList args = {
        bridgePath,
        QStringLiteral("--symbols-file"),
        m_symbolsFilePath,
        QStringLiteral("--interval"),
        QString::number(intervalSeconds),
    };
    const QString configuredServer = m_serverOverride.isEmpty()
        ? qEnvironmentVariable("PPTRADER_TDX_SERVER").trimmed()
        : m_serverOverride;
    if (!configuredServer.isEmpty()) {
        args.append({QStringLiteral("--server"), configuredServer});
    }
    bool validMaximum = false;
    const int maximumBatches = qEnvironmentVariableIntValue(
        "PPTRADER_TDX_MAX_BATCHES", &validMaximum);
    if (validMaximum && maximumBatches > 0) {
        args.append({QStringLiteral("--max-batches"), QString::number(maximumBatches)});
    }

    emit providerError(QStringLiteral("A股行情: 启动中 %1 %2").arg(pythonExe, bridgePath));

    m_process->start(pythonExe, args);
}

void MootdxMarketDataProvider::stop()
{
    m_stopping = true;
    ManagedProcessTree::stop(m_process);
    m_activeBatchId.clear();
    m_activeBatchError.clear();
    if (!m_symbolsFilePath.isEmpty()) {
        QFile::remove(m_symbolsFilePath);
        m_symbolsFilePath.clear();
    }
    publishState(ProviderHealthState::Stopped, QStringLiteral("A 股行情已停止"), {},
                 m_lastReceivedCount, m_lastBatchId, m_lastUpdatedAt);
}

void MootdxMarketDataProvider::onReadyRead()
{
    drainStandardOutput(false);
}

void MootdxMarketDataProvider::drainStandardOutput(bool finalDrain)
{
    m_stdoutBuffer.append(m_process->readAllStandardOutput());
    qsizetype newline = -1;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (!line.isEmpty()) parseLine(line);
    }
    if (finalDrain) {
        const QByteArray line = m_stdoutBuffer.trimmed();
        m_stdoutBuffer.clear();
        if (!line.isEmpty()) parseLine(line);
    }
}

void MootdxMarketDataProvider::onReadyReadStderr()
{
    const QByteArray data = m_process->readAllStandardError();
    if (!data.isEmpty()) {
        m_lastProcessStderr = QString::fromUtf8(data).trimmed();
        emit providerError(m_lastProcessStderr);
    }
}

void MootdxMarketDataProvider::parseLine(const QByteArray &line)
{
    if (line.startsWith("S\t")) {
        if (!MootdxCompactSnapshotParser::parse(line, &m_compactChunkBuffer)) {
            m_activeBatchError = m_compactChunkBuffer.error;
            emit providerError(QStringLiteral("A股行情紧凑块无效：%1")
                                   .arg(m_compactChunkBuffer.error));
            return;
        }
        if (m_activeBatchId.isEmpty()
            || m_compactChunkBuffer.batchId != m_activeBatchId) {
            emit providerError(QStringLiteral("A股行情紧凑块批次标识不匹配"));
            return;
        }
        for (const MootdxCompactQuoteRow &row : m_compactChunkBuffer.quotes) {
            const auto instrument = m_instrumentByCode.constFind(row.symbolCode);
            if (instrument == m_instrumentByCode.cend()) continue;
            Tick tick;
            tick.instrument = *instrument;
            tick.timestamp = m_compactChunkBuffer.timestamp;
            tick.batchId = m_compactChunkBuffer.batchId;
            tick.price = row.price;
            tick.volume = row.volume;
            tick.bid = row.bid;
            tick.ask = row.ask;
            tick.open = row.open;
            tick.high = row.high;
            tick.low = row.low;
            tick.previousClose = row.previousClose;
            tick.amount = row.amount;
            m_snapshotAssembler.addTick(tick);
        }
        return;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError) {
        return;
    }

    const QJsonObject obj = doc.object();

    const QString event = obj.value(QStringLiteral("event")).toString();
    if (event == QStringLiteral("snapshot_begin")) {
        completeActiveSnapshot(QDateTime::currentDateTimeUtc());
        m_activeBatchId = obj.value(QStringLiteral("batch_id")).toString();
        m_activeBatchError.clear();
        const int protocolExpected = obj.value(QStringLiteral("expected")).toInt();
        if (protocolExpected > 0 && protocolExpected != m_expectedCount) {
            m_activeBatchError = QStringLiteral("股票池数量与桥接协议不一致：%1/%2")
                .arg(protocolExpected).arg(m_expectedCount);
        }
        const QDateTime timestamp = QDateTime::fromString(
            obj.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
        m_snapshotAssembler.beginBatch(m_activeBatchId,
            timestamp.isValid() ? timestamp : QDateTime::currentDateTimeUtc());
        publishState(ProviderHealthState::Starting, QStringLiteral("正在采集全市场快照"),
                     m_activeBatchError, 0, m_activeBatchId, timestamp);
        return;
    }
    if (event == QStringLiteral("snapshot_end")) {
        const QString batchId = obj.value(QStringLiteral("batch_id")).toString();
        if (!m_activeBatchId.isEmpty() && batchId == m_activeBatchId) {
            const int protocolExpected = obj.value(QStringLiteral("expected")).toInt();
            const int protocolReceived = obj.value(QStringLiteral("received")).toInt();
            if (protocolExpected > 0 && protocolExpected != m_expectedCount) {
                m_activeBatchError = QStringLiteral("结束事件 expected 不一致：%1/%2")
                    .arg(protocolExpected).arg(m_expectedCount);
            }
            if (protocolReceived >= 0
                && protocolReceived != m_snapshotAssembler.receivedCount()) {
                const QString mismatch = QStringLiteral("结束事件 received 不一致：%1/%2")
                    .arg(protocolReceived).arg(m_snapshotAssembler.receivedCount());
                m_activeBatchError = m_activeBatchError.isEmpty()
                    ? mismatch : m_activeBatchError + QStringLiteral("；") + mismatch;
            }
            completeActiveSnapshot(QDateTime::fromString(
                obj.value(QStringLiteral("completed_at")).toString(), Qt::ISODateWithMs));
        } else if (!m_activeBatchId.isEmpty()) {
            emit providerError(QStringLiteral("A股行情批次结束标识不匹配"));
        }
        return;
    }
    if (event == QStringLiteral("snapshot_chunk")) {
        const QString batchId = obj.value(QStringLiteral("batch_id")).toString();
        if (m_activeBatchId.isEmpty() || batchId != m_activeBatchId) {
            emit providerError(QStringLiteral("A股行情批次数据标识不匹配"));
            return;
        }
        const QDateTime chunkTimestamp = QDateTime::fromString(
            obj.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
        const QJsonArray quotes = obj.value(QStringLiteral("quotes")).toArray();
        for (const QJsonValue &value : quotes) {
            if (!value.isObject()) continue;
            Tick tick = jsonToTick(value.toObject());
            tick.batchId = batchId;
            if (chunkTimestamp.isValid()) tick.timestamp = chunkTimestamp;
            if (!tick.instrument.symbol.isEmpty() && tick.price > 0.0) {
                m_snapshotAssembler.addTick(tick);
            }
        }
        return;
    }

    if (obj.contains(QStringLiteral("error"))) {
        const QString errorMessage = obj.value(QStringLiteral("error")).toString();
        m_lastProcessDiagnostic = errorMessage;
        m_activeBatchError = m_activeBatchError.isEmpty()
            ? errorMessage : m_activeBatchError + QStringLiteral("；") + errorMessage;
        publishState(m_activeBatchId.isEmpty() ? ProviderHealthState::Error
                                               : ProviderHealthState::Degraded,
                     QStringLiteral("A 股行情采集异常"), errorMessage,
                     m_snapshotAssembler.receivedCount(), m_activeBatchId);
        emit providerError(QStringLiteral("A股行情: %1").arg(errorMessage));
        return;
    }

    if (obj.contains(QStringLiteral("status"))) {
        const QString statusMessage = obj.value(QStringLiteral("status")).toString();
        publishState(ProviderHealthState::Starting, statusMessage);
        return;
    }

    const Tick tick = jsonToTick(obj);
    if (tick.instrument.symbol.isEmpty() || tick.price <= 0.0) {
        return;
    }

    if (!m_activeBatchId.isEmpty() && tick.batchId == m_activeBatchId) {
        m_snapshotAssembler.addTick(tick);
    }
    emit tickReceived(tick);
}

Tick MootdxMarketDataProvider::jsonToTick(const QJsonObject &obj) const
{
    Tick tick;
    const QString symbol = obj.value(QStringLiteral("symbol")).toString();

    auto it = m_instrumentBySymbol.find(symbol);
    if (it != m_instrumentBySymbol.end()) {
        tick.instrument = *it;
    } else {
        tick.instrument.symbol = symbol;
        tick.instrument.name = obj.value(QStringLiteral("name")).toString();
        tick.instrument.market = Market::AShare;
        tick.instrument.venue = QStringLiteral("沪深");
    }

    tick.timestamp = QDateTime::fromString(
        obj.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
    if (!tick.timestamp.isValid()) tick.timestamp = QDateTime::currentDateTimeUtc();
    tick.batchId = obj.value(QStringLiteral("batch_id")).toString();
    tick.price = obj.value(QStringLiteral("price")).toDouble();
    tick.previousClose = obj.value(QStringLiteral("pre_close")).toDouble();
    tick.open = obj.value(QStringLiteral("open")).toDouble();
    tick.high = obj.value(QStringLiteral("high")).toDouble();
    tick.low = obj.value(QStringLiteral("low")).toDouble();
    tick.volume = obj.value(QStringLiteral("volume")).toDouble();
    tick.amount = obj.value(QStringLiteral("amount")).toDouble();
    tick.bid = obj.value(QStringLiteral("bid")).toDouble();
    tick.ask = obj.value(QStringLiteral("ask")).toDouble();

    return tick;
}

void MootdxMarketDataProvider::onProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    const QString message = QStringLiteral("A股行情进程错误: %1").arg(m_process->errorString());
    publishState(ProviderHealthState::Error, QStringLiteral("A 股行情进程错误"), message,
                 m_snapshotAssembler.receivedCount(), m_activeBatchId);
    emit providerError(message);
}

void MootdxMarketDataProvider::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    // finished 可能紧跟最后一行 stdout/stderr 到达。退出状态覆盖 Provider
    // 状态前先排空缓冲区，保留桥接器给出的真实根因。
    drainStandardOutput(true);
    onReadyReadStderr();
    if (!m_stopping) completeActiveSnapshot(QDateTime::currentDateTimeUtc());
    if (m_stopping) return;
    if (exitStatus == QProcess::CrashExit) {
        publishState(ProviderHealthState::Error, QStringLiteral("A 股行情进程崩溃"),
                     QStringLiteral("行情桥接进程异常退出"));
        emit providerError(QStringLiteral("A股行情进程崩溃"));
    } else if (exitCode != 0) {
        const QString detail = !m_lastProcessDiagnostic.isEmpty()
            ? m_lastProcessDiagnostic
            : !m_lastProcessStderr.isEmpty()
                ? m_lastProcessStderr
                : QStringLiteral("桥接器未返回诊断信息");
        const QString error = QStringLiteral("退出代码：%1；%2")
                                  .arg(exitCode)
                                  .arg(detail);
        publishState(ProviderHealthState::Error, QStringLiteral("A 股行情进程退出"),
                     error);
        emit providerError(QStringLiteral("A股行情进程退出：%1").arg(error));
    } else {
        publishState(ProviderHealthState::Stopped, QStringLiteral("A 股行情进程已结束"), {},
                     m_lastReceivedCount, m_lastBatchId, m_lastUpdatedAt);
    }
}

void MootdxMarketDataProvider::completeActiveSnapshot(const QDateTime &completedAt)
{
    if (m_activeBatchId.isEmpty()) return;
    MarketSnapshot snapshot = m_snapshotAssembler.completeBatch(
        completedAt.isValid() ? completedAt : QDateTime::currentDateTimeUtc());
    const QString batchId = m_activeBatchId;
    const QString batchError = m_activeBatchError;
    m_activeBatchId.clear();
    m_activeBatchError.clear();
    m_lastReceivedCount = snapshot.quotes.size();
    m_lastBatchId = batchId;
    m_lastUpdatedAt = snapshot.completedAt;
    emit snapshotReceived(snapshot);
    const ProviderHealthState health = providerHealthForBatch(
        snapshot.expectedCount, snapshot.quotes.size(), !batchError.isEmpty());
    publishState(health,
                 health == ProviderHealthState::Healthy
                     ? QStringLiteral("全市场快照采集完成")
                     : QStringLiteral("全市场快照采集不完整"),
                 batchError, snapshot.quotes.size(), batchId, snapshot.completedAt);
}

void MootdxMarketDataProvider::publishState(ProviderHealthState health,
                                             const QString &message,
                                             const QString &error,
                                             int receivedCount,
                                             const QString &batchId,
                                             const QDateTime &updatedAt)
{
    emit providerStateChanged(static_cast<int>(health), message, error,
                              m_expectedCount, std::max(0, receivedCount), batchId,
                              updatedAt.isValid() ? updatedAt : QDateTime::currentDateTimeUtc());
}

} // namespace pp
