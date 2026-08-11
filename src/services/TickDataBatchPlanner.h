#pragma once

#include "domain/TickDataImportTypes.h"

#include <QHash>

namespace pp {

struct TickDataBatchPlanResult {
    bool succeeded = false;
    QVector<TickDataBatchPlanItem> items;
    int excludedWeekendDays = 0;
    int excludedHolidayDays = 0;
    int calendarFallbackDays = 0;
    QString calendarSource;
    QString error;
};

class TickDataBatchPlanner final {
public:
    static constexpr int MaximumCalendarDays = 366;

    static TickDataBatchPlanResult create(const QString &rootDirectory,
                                          const Instrument &instrument,
                                          const QDate &startDate,
                                          const QDate &endDate,
                                          TickDataCsvFormat format,
                                          int periodSeconds,
                                          FiveMinuteVolumeUnit volumeUnit,
                                          const QHash<QDate, int> &existingBarsByDate = {},
                                          bool skipCompleteExisting = true);
    static int resetRetryableFiles(QVector<TickDataBatchPlanItem> *items);
    static void restoreCompletedItems(
        QVector<TickDataBatchPlanItem> *freshItems,
        const QVector<TickDataBatchPlanItem> &storedItems);
};

} // namespace pp
