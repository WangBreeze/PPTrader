#pragma once

#include "models/RealtimeChartPaneListModel.h"
#include "services/TencentMinuteBarService.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QTimer>

#include <functional>

namespace pp {

// 一个独立实时图表窗口的数据会话。每个会话拥有自己的网络请求、代次门控、
// 刷新计时器和派生周期模型，窗口之间不会互相取消请求或覆盖数据。
class RealtimeChartSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString windowId READ windowId CONSTANT)
    Q_PROPERTY(QString symbol READ symbol NOTIFY stateChanged)
    Q_PROPERTY(QString instrumentTitle READ instrumentTitle NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(QString source READ source NOTIFY stateChanged)
    Q_PROPERTY(QDateTime lastBarTime READ lastBarTime NOTIFY stateChanged)
    Q_PROPERTY(QDateTime expectedMinute READ expectedMinute NOTIFY stateChanged)
    Q_PROPERTY(QDateTime providerLastBarTime READ providerLastBarTime NOTIFY stateChanged)
    Q_PROPERTY(QDateTime modelCommittedAt READ modelCommittedAt NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap minuteRefreshDiagnostics READ minuteRefreshDiagnostics NOTIFY stateChanged)
    Q_PROPERTY(bool stale READ stale NOTIFY stateChanged)
    Q_PROPERTY(bool refreshEnabled READ refreshEnabled NOTIFY stateChanged)
    Q_PROPERTY(qulonglong requestId READ requestId NOTIFY stateChanged)
    Q_PROPERTY(qulonglong resolvedRequestId READ resolvedRequestId NOTIFY stateChanged)
    Q_PROPERTY(QAbstractItemModel *chartPaneModel READ chartPaneModel CONSTANT)
    Q_PROPERTY(int chartPaneCount READ chartPaneCount NOTIFY chartPanesChanged)
    Q_PROPERTY(QString chartPaneError READ chartPaneError NOTIFY chartPanesChanged)
    Q_PROPERTY(QVariantList recentInstruments READ recentInstruments
               NOTIFY recentInstrumentsChanged)
    Q_PROPERTY(int minuteCount READ minuteCount NOTIFY stateChanged)
    Q_PROPERTY(int fiveMinuteCount READ fiveMinuteCount NOTIFY stateChanged)
    Q_PROPERTY(int fifteenMinuteCount READ fifteenMinuteCount NOTIFY stateChanged)
    Q_PROPERTY(int sixtyMinuteCount READ sixtyMinuteCount NOTIFY stateChanged)
    Q_PROPERTY(int successfulRefreshCount READ successfulRefreshCount NOTIFY stateChanged)
    Q_PROPERTY(int failedRefreshCount READ failedRefreshCount NOTIFY stateChanged)
    Q_PROPERTY(int instrumentSwitchCount READ instrumentSwitchCount NOTIFY stateChanged)

public:
    using RequestSubmitter = std::function<bool(
        quint64, const Instrument &, int)>;
    using CancelCallback = std::function<void()>;

    explicit RealtimeChartSession(
        const QString &windowId, const Instrument &instrument,
        int refreshOffsetMilliseconds = 0, QObject *parent = nullptr);
    RealtimeChartSession(
        const QString &windowId, const Instrument &instrument,
        RequestSubmitter submitter, CancelCallback canceller,
        int refreshOffsetMilliseconds = 0, QObject *parent = nullptr);
    ~RealtimeChartSession() override;

    QString windowId() const;
    QString symbol() const;
    QString instrumentTitle() const;
    bool loading() const;
    QString statusText() const;
    QString errorText() const;
    QString source() const;
    QDateTime lastBarTime() const;
    QDateTime expectedMinute() const;
    QDateTime providerLastBarTime() const;
    QDateTime modelCommittedAt() const;
    QVariantMap minuteRefreshDiagnostics() const;
    bool stale() const;
    bool refreshEnabled() const;
    qulonglong requestId() const;
    qulonglong resolvedRequestId() const;
    int minuteCount() const;
    int fiveMinuteCount() const;
    int fifteenMinuteCount() const;
    int sixtyMinuteCount() const;
    int successfulRefreshCount() const;
    int failedRefreshCount() const;
    int instrumentSwitchCount() const;
    QAbstractItemModel *chartPaneModel();
    int chartPaneCount() const;
    QString chartPaneError() const;
    QVariantList recentInstruments() const;

    Q_INVOKABLE QAbstractItemModel *modelForPeriod(int periodSeconds);
    Q_INVOKABLE int countForPeriod(int periodSeconds) const;
    Q_INVOKABLE QString addChart(int periodSeconds = 0);
    Q_INVOKABLE bool removeChart(const QString &chartId);
    Q_INVOKABLE bool setChartPeriod(const QString &chartId, int periodSeconds);
    Q_INVOKABLE qulonglong requestRefresh();

    void setInstrument(const Instrument &instrument, bool requestImmediately);
    void setRefreshEnabled(bool enabled, bool requestImmediately = false);
    bool acceptResult(quint64 requestId,
                      const RealtimeMinuteSeriesResult &result);
    void shutdown();

signals:
    void stateChanged();
    void chartPanesChanged();
    void recentInstrumentsChanged();
    void refreshCompleted(const QVariantMap &diagnostics);

private:
    void initialize();
    qulonglong submitRefresh(bool retry);
    void scheduleRefresh();
    void scheduleCatchupRetry();
    void rebuildModels();
    void clearModels();
    void rememberInstrument(const Instrument &instrument);

    QString m_windowId;
    Instrument m_instrument;
    TencentMinuteBarService m_service;
    RealtimeMinuteRequestGate m_requestGate;
    RequestSubmitter m_submitter;
    CancelCallback m_canceller;
    QTimer m_refreshTimer;
    int m_refreshOffsetMilliseconds = 0;
    QVector<Candle> m_rawCandles;
    RealtimeChartPaneListModel m_chartPanes;
    QVector<Instrument> m_recentInstruments;
    QString m_chartPaneError;
    QString m_statusText = QStringLiteral("实时窗口尚未加载");
    QString m_errorText;
    QString m_source;
    QDateTime m_lastBarTime;
    QDateTime m_expectedMinute;
    QDateTime m_providerLastBarTime;
    QDateTime m_modelCommittedAt;
    bool m_stale = true;
    bool m_refreshEnabled = false;
    bool m_shutdownRequested = false;
    int m_successfulRefreshCount = 0;
    int m_failedRefreshCount = 0;
    int m_instrumentSwitchCount = 0;
    int m_catchupRetryCount = 0;
    bool m_retryPending = false;
    int m_roundBaselineCount = 0;
    QDateTime m_roundBaselineFetchedAt;
    QDateTime m_lastSuccessfulFetchedAt;
    QVariantMap m_minuteRefreshDiagnostics;
};

} // namespace pp
