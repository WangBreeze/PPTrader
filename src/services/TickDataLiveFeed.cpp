#include "services/TickDataLiveFeed.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace pp {
namespace {

LiveTickRecord parseTick(const QJsonObject &object)
{
    LiveTickRecord record;
    record.time = object.value(QStringLiteral("time")).toString();
    record.price = object.value(QStringLiteral("price")).toDouble();
    record.volume = object.value(QStringLiteral("volume")).toInt();
    record.amount = object.value(QStringLiteral("amount")).toDouble();
    record.side = object.value(QStringLiteral("side")).toString().toUpper();
    return record;
}

// 把服务端 JSON 转成契约结构。字段缺失时保持默认值，不因单个字段异常而整批失败
// —— 采集器可能在上游接口变更后少给字段，调用方应能降级读取而不是直接报错。
LiveTickBatch parseBatch(const QJsonObject &object)
{
    LiveTickBatch batch;
    batch.symbol = object.value(QStringLiteral("symbol")).toString();
    batch.tradingDate = object.value(QStringLiteral("tradingDate")).toString();
    batch.serverTime = object.value(QStringLiteral("serverTime")).toString();
    batch.available = object.value(QStringLiteral("available")).toBool();
    batch.sessionClosed = object.value(QStringLiteral("sessionClosed")).toBool();
    batch.overflow = object.value(QStringLiteral("overflow")).toBool();
    batch.totalRows = object.value(QStringLiteral("totalRows")).toInt();
    batch.lastTime = object.value(QStringLiteral("lastTime")).toString();
    batch.after = object.value(QStringLiteral("after")).toString();
    batch.nextAfter = object.value(QStringLiteral("nextAfter")).toString();

    const QJsonArray ticks = object.value(QStringLiteral("ticks")).toArray();
    batch.ticks.reserve(ticks.size());
    for (const QJsonValue &value : ticks) {
        if (value.isObject()) batch.ticks.append(parseTick(value.toObject()));
    }

    const QString error = object.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) {
        batch.succeeded = false;
        batch.error = error;
        return batch;
    }
    batch.succeeded = true;
    return batch;
}

} // namespace

HttpTickDataLiveFeed::HttpTickDataLiveFeed(QObject *parent)
    : QObject(parent)
{
    // 跨线程投递需要注册；直接连接时也无副作用。
    qRegisterMetaType<pp::LiveTickBatch>("pp::LiveTickBatch");
    qRegisterMetaType<pp::LiveTickRecord>("pp::LiveTickRecord");
}

void HttpTickDataLiveFeed::setEndpoint(const QUrl &endpoint)
{
    m_endpoint = endpoint;
}

QUrl HttpTickDataLiveFeed::endpoint() const
{
    return m_endpoint;
}

quint64 HttpTickDataLiveFeed::fetch(const QString &symbol,
                                    const QString &after,
                                    const QString &tradingDate,
                                    int limit)
{
    const quint64 requestId = m_nextRequestId++;

    LiveTickBatch failed;
    failed.symbol = symbol;
    failed.after = after;
    failed.nextAfter = after;

    if (symbol.size() != 6) {
        failed.error = QStringLiteral("代码必须是 6 位数字");
        // 异步契约：即使是本地可判定的错误也走信号，调用方无需区分同步/异步路径。
        emit batchReady(requestId, failed);
        return requestId;
    }
    if (!m_endpoint.isValid() || m_endpoint.isEmpty()) {
        failed.error = QStringLiteral("未配置 TickData 节点地址");
        emit batchReady(requestId, failed);
        return requestId;
    }

    QUrl url(m_endpoint);
    url.setPath(QStringLiteral("/v1/live/%1").arg(symbol));
    QUrlQuery query;
    if (!after.isEmpty()) query.addQueryItem(QStringLiteral("after"), after);
    if (!tradingDate.isEmpty()) query.addQueryItem(QStringLiteral("date"), tradingDate);
    query.addQueryItem(QStringLiteral("limit"), QString::number(qBound(1, limit, 20000)));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("PPTrader/TickDataLiveFeed"));
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, requestId, symbol, after]() {
                reply->deleteLater();
                LiveTickBatch batch;
                batch.symbol = symbol;
                batch.after = after;
                batch.nextAfter = after;

                if (reply->error() != QNetworkReply::NoError) {
                    batch.error = reply->errorString();
                    emit batchReady(requestId, batch);
                    return;
                }
                const QByteArray payload = reply->readAll();
                QJsonParseError parseError{};
                const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
                if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                    batch.error = QStringLiteral("响应不是合法 JSON：%1")
                                      .arg(parseError.errorString());
                    emit batchReady(requestId, batch);
                    return;
                }
                emit batchReady(requestId, parseBatch(document.object()));
            });
    return requestId;
}

} // namespace pp
