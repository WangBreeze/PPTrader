#pragma once

#include "domain/RealtimeMinuteSeriesTypes.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

class QNetworkReply;

namespace pp {

class RealtimeMinuteRequestGate final {
public:
    quint64 begin();
    bool finish(quint64 requestId);
    quint64 currentRequestId() const;
    quint64 resolvedRequestId() const;
    bool loading() const;
    void cancel();

private:
    quint64 m_currentRequestId = 0;
    quint64 m_resolvedRequestId = 0;
    bool m_loading = false;
};

class RealtimeLiveSessionState final {
public:
    bool requestStart();
    bool requestStop();
    void updateProviderHealth(int health);
    bool requested() const;
    bool providerActive() const;
    bool minuteRefreshEnabled() const;

private:
    bool m_requested = false;
    bool m_providerActive = false;
};

class TencentMinuteBarService final : public QObject {
    Q_OBJECT

public:
    explicit TencentMinuteBarService(QObject *parent = nullptr);
    ~TencentMinuteBarService() override;

    bool request(quint64 requestId, const Instrument &instrument, int maximumBars = 320);
    void cancel();

    static RealtimeMinuteSeriesResult parseResponse(
        const QByteArray &payload, const Instrument &instrument,
        const QDateTime &requestedAt, const QDateTime &fetchedAt);
    static bool supportsPeriod(int periodSeconds);
    static int millisecondsUntilNextMinuteRefresh(
        const QDateTime &nowUtc, int settleDelayMilliseconds = 1500);
    // 本机上一个自然分钟的起点（UTC）；不证明 Provider 同标签 Bar 已闭合。
    static QDateTime expectedClosedMinute(const QDateTime &fetchedAtUtc);
    static QVector<Candle> aggregateCandles(
        const QVector<Candle> &minuteCandles, int periodSeconds);

signals:
    void resultReady(quint64 requestId, const pp::RealtimeMinuteSeriesResult &result);

private:
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
};

} // namespace pp
