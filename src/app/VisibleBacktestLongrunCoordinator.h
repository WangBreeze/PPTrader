#pragma once

#include <QDate>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

class QWindow;

namespace pp {

class TradingController;

class VisibleBacktestLongrunCoordinator final : public QObject {
public:
    struct Options {
        QString outputDirectory;
        QString databasePath;
        QDate startDate;
        QDate endDate;
        int durationSeconds = 14'400;
        int maximumSymbols = 10;
        int maximumRounds = 0;
        int watchdogSeconds = 120;
    };

    static bool optionsFromEnvironment(Options *options, QString *error);

    VisibleBacktestLongrunCoordinator(TradingController *controller,
                                      QObject *rootObject, QWindow *window,
                                      Options options,
                                      QObject *parent = nullptr);

    void start();
    void finishInterrupted();

private:
    void launchNextRound();
    void poll();
    void completeRound();
    void fail(const QString &message);
    void finalize(const QString &status, const QString &error = {});
    bool appendRound(const QJsonObject &round);
    bool writeReport(const QString &status, const QString &error = {});
    void updateWindowTitle();
    static double percentile(QVector<qint64> values, double quantile);

    QPointer<TradingController> m_controller;
    QPointer<QObject> m_rootObject;
    QPointer<QWindow> m_window;
    Options m_options;
    QVariantMap m_strategyParameters;
    QTimer m_pollTimer;
    QTimer m_watchdogTimer;
    QTimer m_cancellationGraceTimer;
    QElapsedTimer m_totalElapsed;
    QElapsedTimer m_roundElapsed;
    QString m_startedAt;
    QString m_completedAt;
    QString m_reportPath;
    QString m_roundsPath;
    quint64 m_activeRequestId = 0;
    int m_roundsCompleted = 0;
    int m_totalTasksCompleted = 0;
    int m_totalTasksFailed = 0;
    int m_watchdogTimeouts = 0;
    qint64 m_minRawTickCount = -1;
    qint64 m_maxRawTickCount = 0;
    QVector<qint64> m_roundDurations;
    QString m_firstDigest;
    QString m_lastDigest;
    QJsonArray m_firstRows;
    QJsonArray m_lastRows;
    bool m_deterministic = true;
    double m_initialCapital = 0.0;
    double m_commissionRate = 0.0;
    double m_slippageRate = 0.0;
    bool m_active = false;
    bool m_cancelling = false;
    bool m_finalized = false;
    QString m_pendingFailure;
};

} // namespace pp
