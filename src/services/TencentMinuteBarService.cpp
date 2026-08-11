#include "services/TencentMinuteBarService.h"

#include "services/TencentQuoteParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>

namespace pp {

quint64 RealtimeMinuteRequestGate::begin()
{
    m_loading = true;
    return ++m_currentRequestId;
}

bool RealtimeMinuteRequestGate::finish(quint64 requestId)
{
    if (requestId == 0 || requestId != m_currentRequestId) return false;
    m_resolvedRequestId = requestId;
    m_loading = false;
    return true;
}

quint64 RealtimeMinuteRequestGate::currentRequestId() const { return m_currentRequestId; }
quint64 RealtimeMinuteRequestGate::resolvedRequestId() const { return m_resolvedRequestId; }
bool RealtimeMinuteRequestGate::loading() const { return m_loading; }
void RealtimeMinuteRequestGate::cancel()
{
    ++m_currentRequestId;
    m_loading = false;
}

TencentMinuteBarService::TencentMinuteBarService(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<RealtimeMinuteSeriesResult>();
}

TencentMinuteBarService::~TencentMinuteBarService()
{
    cancel();
}

bool TencentMinuteBarService::request(
    quint64 requestId, const Instrument &instrument, int maximumBars)
{
    const QString providerSymbol = TencentQuoteParser::providerCode(instrument);
    if (requestId == 0 || providerSymbol.isEmpty()) return false;
    cancel();
    const int limit = std::clamp(maximumBars, 1, 320);
    const QString url = QStringLiteral(
        "https://ifzq.gtimg.cn/appstock/app/kline/mkline?param=%1,m1,,%2")
                            .arg(providerSymbol).arg(limit);
    QNetworkRequest request {QUrl(url)};
    request.setTransferTimeout(10000);
    request.setRawHeader("User-Agent", "PPTrader/0.1 realtime-minute-series");
    request.setRawHeader("Referer", "https://gu.qq.com/");
    const QDateTime requestedAt = QDateTime::currentDateTimeUtc();
    QNetworkReply *reply = m_network.get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, requestId, instrument, requestedAt]() {
        if (reply != m_reply) {
            reply->deleteLater();
            return;
        }
        m_reply.clear();
        RealtimeMinuteSeriesResult result;
        if (reply->error() == QNetworkReply::NoError) {
            result = parseResponse(reply->readAll(), instrument, requestedAt,
                                   QDateTime::currentDateTimeUtc());
        } else {
            result.instrument = instrument;
            result.requestedAt = requestedAt;
            result.fetchedAt = QDateTime::currentDateTimeUtc();
            result.providerId = QStringLiteral("tencent_public_m1");
            result.error = QStringLiteral("腾讯 1 分钟行情请求失败：%1")
                               .arg(reply->errorString());
        }
        reply->deleteLater();
        emit resultReady(requestId, result);
    });
    return true;
}

void TencentMinuteBarService::cancel()
{
    if (!m_reply) return;
    disconnect(m_reply, nullptr, this, nullptr);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply.clear();
}

RealtimeMinuteSeriesResult TencentMinuteBarService::parseResponse(
    const QByteArray &payload, const Instrument &instrument,
    const QDateTime &requestedAt, const QDateTime &fetchedAt)
{
    RealtimeMinuteSeriesResult result;
    result.instrument = instrument;
    result.requestedAt = requestedAt;
    result.fetchedAt = fetchedAt;
    result.providerId = QStringLiteral("tencent_public_m1");
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("腾讯 1 分钟行情 JSON 无效：%1")
                           .arg(parseError.errorString());
        return result;
    }
    const QString providerSymbol = TencentQuoteParser::providerCode(instrument);
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("code")).toInt(-1) != 0) {
        result.error = QStringLiteral("腾讯 1 分钟行情返回错误：%1")
                           .arg(root.value(QStringLiteral("msg")).toString());
        return result;
    }
    const QJsonObject symbolObject = root.value(QStringLiteral("data")).toObject()
                                         .value(providerSymbol).toObject();
    const QJsonArray rows = symbolObject.value(QStringLiteral("m1")).toArray();
    QMap<QDateTime, Candle> parsed;
    const QTimeZone shanghai("Asia/Shanghai");
    QDate latestTradingDate;
    for (const QJsonValue &value : rows) {
        const QJsonArray row = value.toArray();
        if (row.size() < 6) continue;
        const QDateTime local = QDateTime::fromString(
            row.at(0).toString(), QStringLiteral("yyyyMMddHHmm"));
        if (!local.isValid()) continue;
        const QDateTime timestamp(local.date(), local.time(), shanghai);
        bool openOk = false;
        bool closeOk = false;
        bool highOk = false;
        bool lowOk = false;
        bool volumeOk = false;
        const double open = row.at(1).toString().toDouble(&openOk);
        const double close = row.at(2).toString().toDouble(&closeOk);
        const double high = row.at(3).toString().toDouble(&highOk);
        const double low = row.at(4).toString().toDouble(&lowOk);
        const double volumeLots = row.at(5).toString().toDouble(&volumeOk);
        if (!openOk || !closeOk || !highOk || !lowOk || !volumeOk
            || open <= 0.0 || close <= 0.0 || high < std::max(open, close)
            || low > std::min(open, close) || low <= 0.0 || volumeLots < 0.0) {
            continue;
        }
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = timestamp.toUTC();
        candle.periodSeconds = 60;
        candle.open = open;
        candle.high = high;
        candle.low = low;
        candle.close = close;
        candle.volume = volumeLots * 100.0;
        candle.source = result.providerId;
        candle.downloadedAt = fetchedAt;
        parsed.insert(candle.startTime, candle);
        if (!latestTradingDate.isValid() || local.date() > latestTradingDate)
            latestTradingDate = local.date();
    }
    for (const Candle &candle : std::as_const(parsed)) {
        if (candle.startTime.toTimeZone(shanghai).date() == latestTradingDate)
            result.candles.append(candle);
    }
    if (result.candles.isEmpty()) {
        result.error = QStringLiteral("腾讯没有返回该品种可用的 1 分钟 K 线");
        return result;
    }
    result.lastBarTime = result.candles.last().startTime;
    result.succeeded = true;
    return result;
}

} // namespace pp
