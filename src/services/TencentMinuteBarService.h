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

signals:
    void resultReady(quint64 requestId, const pp::RealtimeMinuteSeriesResult &result);

private:
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
};

} // namespace pp
