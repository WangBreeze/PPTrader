#pragma once

#include "domain/AShareCalendarTypes.h"
#include "domain/BacktestDatasetTypes.h"

#include <QDate>
#include <QDateTime>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace pp {

enum class BacktestDataQualityStatus {
    Pass = 0,
    Warning,
    Reject
};

inline QString backtestDataQualityStatusCode(BacktestDataQualityStatus status)
{
    switch (status) {
    case BacktestDataQualityStatus::Pass: return QStringLiteral("pass");
    case BacktestDataQualityStatus::Warning: return QStringLiteral("warning");
    case BacktestDataQualityStatus::Reject: return QStringLiteral("reject");
    }
    return QStringLiteral("reject");
}

// 对应一次实际解析并写入原始 Tick 的逐日审计。旧数据没有审计时保持未知，
// 不能由 raw_count 反推出“零坏行”。
struct RawTickImportDayAudit {
    QDate tradingDate;
    QString rawContentSha256;
    int auditVersion = 0;
    qint64 linesRead = 0;
    qint64 validRecords = 0;
    qint64 skippedRecords = 0;
    qint64 outOfOrderRecords = 0;
    QString qualityStatus;
    QString source;
    QDateTime auditedAt;

    bool isValid() const
    {
        return tradingDate.isValid()
            && rawContentSha256.startsWith(QStringLiteral("sha256:"))
            && rawContentSha256.size() == 71 && auditVersion > 0
            && linesRead >= validRecords && validRecords > 0
            && skippedRecords >= 0 && outOfOrderRecords >= 0;
    }
};

struct BacktestDataQualityPolicy {
    double maximumSkippedRecordRatio = 0.01;
};

struct BacktestDataQualityAssessment {
    BacktestDataQualityStatus status = BacktestDataQualityStatus::Reject;
    bool eligible = false;
    QVector<QDate> observedRawTickDates;
    QVector<QDate> missingTradingDates;
    QVector<QDate> explainedNoTradeDates;
    QVector<QDate> unexpectedNonTradingDates;
    QVector<QDate> contradictorySuspensionDates;
    QVector<QDate> unknownImportAuditDates;
    QVector<QDate> outOfOrderAuditDates;
    QVector<QDate> excessiveSkippedDates;
    QVector<QDate> failedAuditDates;
    QVector<QDate> auditCountMismatchDates;
    int auditedDayCount = 0;
    qint64 validRecords = 0;
    qint64 skippedRecords = 0;
    qint64 outOfOrderRecords = 0;
    double skippedRecordRatio = 0.0;
    bool calendarCoverageKnown = false;
    bool corporateActionCoverageKnown = false;
    QVector<QDate> corporateActionDates;
    QStringList messages;

    QVariantMap toVariantMap() const;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::BacktestDataQualityAssessment)
