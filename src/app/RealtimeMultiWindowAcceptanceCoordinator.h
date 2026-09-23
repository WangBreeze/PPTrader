#pragma once

#include <QFile>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVector>

class QCoreApplication;

namespace pp {

class TradingController;
struct FactorFrame;

// 可见应用级双实时窗口验收编排器。它只调用 TradingController 公共契约，
// 不写行情事实表，也不通过 UI Automation 操作窗口。
class RealtimeMultiWindowAcceptanceCoordinator final : public QObject {
    Q_OBJECT

public:
    struct Options {
        QString outputDirectory;
        QStringList firstWindowSymbols;
        QStringList secondWindowSymbols;
        QString strategyStudyPlanPath;
        int durationSeconds = 1800;
        int sampleIntervalSeconds = 15;
    };

    static bool optionsFromEnvironment(Options *options, QString *error);

    RealtimeMultiWindowAcceptanceCoordinator(
        TradingController *controller, QCoreApplication *application,
        Options options, QObject *parent = nullptr);
    ~RealtimeMultiWindowAcceptanceCoordinator() override;

public slots:
    void start();
    void finishInterrupted();

private:
    void sample();
    void switchWindow(int windowIndex);
    bool loadStrategyStudyPlan(QString *error);
    void captureStrategyFrame(const pp::FactorFrame &frame);
    void finish(bool completed, const QString &error = {});
    void writeSummary(bool completed, const QString &error);

    TradingController *m_controller = nullptr;
    QCoreApplication *m_application = nullptr;
    Options m_options;
    QFile m_roundsFile;
    QFile m_minuteResponsesFile;
    QFile m_strategyFramesFile;
    QTimer m_sampleTimer;
    QTimer m_finishTimer;
    QStringList m_windowIds;
    QVector<int> m_symbolIndexes {0, 0};
    QHash<QString, QDateTime> m_lastBarsByWindowAndSymbol;
    QHash<QString, QString> m_strategyCategoryBySymbol;
    QSet<QString> m_strategyApproachSymbols;
    QSet<QString> m_strategyLimitHitSymbols;
    QSet<QString> m_strategyExactLimitTouchSymbols;
    QSet<QString> m_strategyLatestExactLimitSymbols;
    QString m_strategyPlanVersion;
    QString m_strategyHistoryThrough;
    QString m_strategyFrozenAt;
    QString m_strategyTargetTradingDay;
    int m_sampleCount = 0;
    int m_bothWindowSampleCount = 0;
    int m_switchAttemptCount = 0;
    int m_switchSuccessCount = 0;
    int m_monotonicViolationCount = 0;
    int m_todayBarSampleCount = 0;
    int m_maxWindowCount = 0;
    int m_strategyFrameCount = 0;
    int m_strategyOutOfTargetDayFrameCount = 0;
    int m_strategyMonotonicViolationCount = 0;
    QDateTime m_lastStrategyFrameTime;
    bool m_started = false;
    bool m_finished = false;
    QDateTime m_startedAt;
};

} // namespace pp
