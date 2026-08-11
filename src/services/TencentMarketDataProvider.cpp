#include "services/TencentMarketDataProvider.h"

#include "services/TencentQuoteParser.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace pp {

TencentMarketDataProvider::TencentMarketDataProvider(QObject *parent)
    : MarketDataProvider(parent)
{
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this,
            &TencentMarketDataProvider::beginSnapshot);
}

TencentMarketDataProvider::~TencentMarketDataProvider()
{
    stop();
}

void TencentMarketDataProvider::start(const QVector<Instrument> &instruments)
{
    stop();
    m_instruments.clear();
    m_instrumentBySymbol.clear();
    for (const Instrument &instrument : instruments) {
        const QString providerSymbol = TencentQuoteParser::providerCode(instrument);
        if (providerSymbol.isEmpty()) continue;
        m_instruments.append(instrument);
        m_instrumentBySymbol.insert(instrument.symbol, instrument);
    }
    if (m_instruments.isEmpty()) {
        publishState(ProviderHealthState::Error, QStringLiteral("腾讯行情启动失败"),
                     QStringLiteral("没有可映射的沪深 A 股标的"));
        emit providerError(QStringLiteral("腾讯行情没有可映射的沪深 A 股标的"));
        return;
    }
    m_snapshotAssembler.setUniverse(m_instruments);
    bool validInterval = false;
    const int intervalSeconds = qEnvironmentVariableIntValue(
        "PPTRADER_TENCENT_INTERVAL_SECONDS", &validInterval);
    m_pollIntervalMilliseconds = std::max(60, validInterval ? intervalSeconds : 60) * 1000;
    bool validMaximum = false;
    m_maximumBatches = qEnvironmentVariableIntValue(
        "PPTRADER_TENCENT_MAX_BATCHES", &validMaximum);
    if (!validMaximum || m_maximumBatches < 0) m_maximumBatches = 0;
    m_completedBatches = 0;
    m_lastEmittedQuoteTime = {};
    m_running = true;
    publishState(ProviderHealthState::Starting,
                 QStringLiteral("正在启动腾讯公开报价（60 秒轮询）"));
    beginSnapshot();
}

void TencentMarketDataProvider::stop()
{
    m_running = false;
    m_pollTimer.stop();
    if (m_activeReply) {
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply.clear();
    }
    m_batchId.clear();
    m_errors.clear();
    if (!m_instruments.isEmpty()) {
        publishState(ProviderHealthState::Stopped, QStringLiteral("腾讯公开报价已停止"));
    }
}

void TencentMarketDataProvider::beginSnapshot()
{
    if (!m_running || m_activeReply) return;
    m_offset = 0;
    m_errors.clear();
    m_startedAt = QDateTime::currentDateTimeUtc();
    m_latestQuoteTime = {};
    m_batchId = QStringLiteral("tencent-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_snapshotAssembler.beginBatch(m_batchId, m_startedAt);
    publishState(ProviderHealthState::Starting,
                 QStringLiteral("正在采集腾讯全市场分钟级快照"));
    requestNextChunk();
}

void TencentMarketDataProvider::requestNextChunk()
{
    if (!m_running) return;
    if (m_offset >= m_instruments.size()) {
        completeSnapshot();
        return;
    }
    const int requestedCount = std::min(
        m_batchSize, static_cast<int>(m_instruments.size()) - m_offset);
    QStringList providerSymbols;
    providerSymbols.reserve(requestedCount);
    for (int index = 0; index < requestedCount; ++index) {
        providerSymbols.append(TencentQuoteParser::providerCode(
            m_instruments.at(m_offset + index)));
    }
    m_offset += requestedCount;
    const QByteArray encodedUrl = QByteArray("https://qt.gtimg.cn/q=")
        + providerSymbols.join(QLatin1Char(',')).toLatin1();
    QNetworkRequest request(QUrl::fromEncoded(encodedUrl));
    request.setTransferTimeout(10000);
    request.setRawHeader("User-Agent", "PPTrader/0.1 realtime-provider");
    request.setRawHeader("Referer", "https://gu.qq.com/");
    QNetworkReply *reply = m_network.get(request);
    m_activeReply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, requestedCount]() { handleReply(reply, requestedCount); });
}

void TencentMarketDataProvider::handleReply(QNetworkReply *reply, int requestedCount)
{
    if (!reply) return;
    const bool current = reply == m_activeReply;
    if (current) m_activeReply.clear();
    if (!m_running || !current) {
        reply->deleteLater();
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        m_errors.append(QStringLiteral("批次 %1：%2")
                            .arg((m_offset - requestedCount) / m_batchSize)
                            .arg(reply->errorString()));
    } else {
        const QVector<Tick> ticks = TencentQuoteParser::parse(
            reply->readAll(), m_instrumentBySymbol, m_batchId);
        for (const Tick &tick : ticks) {
            if (tick.timestamp.isValid()
                && (!m_latestQuoteTime.isValid() || tick.timestamp > m_latestQuoteTime)) {
                m_latestQuoteTime = tick.timestamp;
            }
            if (m_snapshotAssembler.addTick(tick)) emit tickReceived(tick);
        }
    }
    reply->deleteLater();
    requestNextChunk();
}

void TencentMarketDataProvider::completeSnapshot()
{
    if (!m_running || m_batchId.isEmpty()) return;
    const QDateTime fetchedAt = QDateTime::currentDateTimeUtc();
    MarketSnapshot snapshot = m_snapshotAssembler.completeBatch(fetchedAt);
    if (m_latestQuoteTime.isValid()) {
        snapshot.timestamp = m_latestQuoteTime;
        snapshot.completedAt = m_latestQuoteTime;
    }
    m_batchId.clear();
    const bool freshSnapshot = !m_latestQuoteTime.isValid()
        || !m_lastEmittedQuoteTime.isValid()
        || m_latestQuoteTime > m_lastEmittedQuoteTime;
    if (freshSnapshot) {
        emit snapshotReceived(snapshot);
        m_lastEmittedQuoteTime = m_latestQuoteTime;
    }
    const bool hadFailure = !m_errors.isEmpty();
    const ProviderHealthState health = providerHealthForBatch(
        snapshot.expectedCount, snapshot.quotes.size(), hadFailure);
    const QString error = m_errors.join(QStringLiteral("；"));
    publishState(health,
                 !freshSnapshot
                     ? QStringLiteral("腾讯连接正常，市场未产生新分钟报价")
                     : health == ProviderHealthState::Healthy
                         ? QStringLiteral("腾讯全市场快照采集完成")
                         : QStringLiteral("腾讯全市场快照不完整"), error,
                 snapshot.quotes.size(), snapshot.batchId);
    if (hadFailure) emit providerError(QStringLiteral("腾讯行情：%1").arg(error));
    ++m_completedBatches;
    if (m_maximumBatches > 0 && m_completedBatches >= m_maximumBatches) {
        m_running = false;
        publishState(ProviderHealthState::Stopped,
                     QStringLiteral("腾讯行情已完成受控批次数"));
        return;
    }
    m_pollTimer.start(m_pollIntervalMilliseconds);
}

void TencentMarketDataProvider::publishState(
    ProviderHealthState health, const QString &message, const QString &error,
    int receivedCount, const QString &batchId)
{
    emit providerStateChanged(static_cast<int>(health), message, error,
                              m_instruments.size(),
                              receivedCount >= 0 ? receivedCount
                                                 : m_snapshotAssembler.receivedCount(),
                              batchId.isEmpty() ? m_batchId : batchId,
                              QDateTime::currentDateTimeUtc());
}

} // namespace pp
