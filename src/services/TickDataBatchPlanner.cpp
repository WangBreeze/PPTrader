#include "services/TickDataBatchPlanner.h"

#include "services/TickDataCsvImporter.h"
#include "services/AShareTradingCalendar.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace pp {

namespace {

bool requestConfigurationValid(const QString &rootDirectory,
                               const Instrument &instrument,
                               TickDataCsvFormat format,
                               int periodSeconds,
                               FiveMinuteVolumeUnit volumeUnit,
                               QString *error)
{
    const QString root = QDir::fromNativeSeparators(rootDirectory.trimmed());
    static const QRegularExpression symbolPattern(QStringLiteral("^[0-9]{6}$"));
    if (root.isEmpty() || !QDir::isAbsolutePath(root)
        || root.startsWith(QStringLiteral("//"))) {
        *error = QStringLiteral("根目录必须是显式选择的本地或已挂载绝对路径");
    } else if (instrument.market != Market::AShare
               || !symbolPattern.match(instrument.symbol).hasMatch()) {
        *error = QStringLiteral("批量导入只接受已加载股票池中的六位 A 股代码");
    } else if (format == TickDataCsvFormat::Tick
               && periodSeconds != 60 && periodSeconds != 300) {
        *error = QStringLiteral("逐笔批量导入只支持 60 或 300 秒聚合");
    } else if (format == TickDataCsvFormat::FiveMinute && periodSeconds != 300) {
        *error = QStringLiteral("5min 批量导入固定为 300 秒");
    } else if (format == TickDataCsvFormat::FiveMinute
               && volumeUnit == FiveMinuteVolumeUnit::Unknown) {
        *error = QStringLiteral("5min volume 单位必须显式选择股或手");
    }
    return error->isEmpty();
}

void refreshFileState(TickDataBatchPlanItem *item)
{
    const QFileInfo info(item->filePath);
    item->fileSize = info.isFile() ? info.size() : 0;
    item->status = info.isFile() && info.size() > 0
        ? TickDataBatchItemStatus::Ready : TickDataBatchItemStatus::Missing;
    item->error = info.isFile() && info.size() == 0
        ? QStringLiteral("文件为空")
        : !info.isFile() ? QStringLiteral("文件不存在") : QString{};
}

void applyCoverage(TickDataBatchPlanItem *item, int existingBars,
                   bool skipCompleteExisting)
{
    item->existingBars = qMax(0, existingBars);
    item->expectedBars = item->request.periodSeconds == 60 ? 240 : 48;
    item->coverageStatus = item->existingBars <= 0
        ? QStringLiteral("empty")
        : item->existingBars >= item->expectedBars
            ? QStringLiteral("complete") : QStringLiteral("partial");
    item->willOverwrite = item->existingBars > 0
        && item->status == TickDataBatchItemStatus::Ready;
    if (skipCompleteExisting && item->coverageStatus == QStringLiteral("complete")) {
        item->status = TickDataBatchItemStatus::SkippedExisting;
        item->willOverwrite = false;
        item->error = QStringLiteral("本地数据已达到完整日参考根数，按计划选项跳过");
    }
}

} // namespace

TickDataBatchPlanResult TickDataBatchPlanner::create(
    const QString &rootDirectory, const Instrument &instrument,
    const QDate &startDate, const QDate &endDate, TickDataCsvFormat format,
    int periodSeconds, FiveMinuteVolumeUnit volumeUnit,
    const QHash<QDate, int> &existingBarsByDate, bool skipCompleteExisting)
{
    TickDataBatchPlanResult result;
    if (!startDate.isValid() || !endDate.isValid() || startDate > endDate) {
        result.error = QStringLiteral("日期范围无效");
        return result;
    }
    const int calendarDays = startDate.daysTo(endDate) + 1;
    if (calendarDays > MaximumCalendarDays) {
        result.error = QStringLiteral("单次计划最多覆盖 366 个自然日");
        return result;
    }
    if (!requestConfigurationValid(rootDirectory, instrument, format,
                                   periodSeconds, volumeUnit, &result.error)) {
        return result;
    }

    for (QDate date = startDate; date <= endDate; date = date.addDays(1)) {
        const AShareTradingDayStatus calendar = AShareTradingCalendar::status(date);
        if (calendar.weekend) {
            ++result.excludedWeekendDays;
            continue;
        }
        if (calendar.holiday) {
            ++result.excludedHolidayDays;
            continue;
        }
        if (!calendar.coverageKnown) ++result.calendarFallbackDays;
        if (result.calendarSource.isEmpty()) result.calendarSource = calendar.source;
        else if (result.calendarSource != calendar.source)
            result.calendarSource = QStringLiteral("mixed");
        TickDataBatchPlanItem item;
        item.request.rootDirectory = rootDirectory;
        item.request.instrument = instrument;
        item.request.tradingDate = date;
        item.request.format = format;
        item.request.periodSeconds = periodSeconds;
        item.request.fiveMinuteVolumeUnit = volumeUnit;
        item.filePath = TickDataCsvImporter::expectedFilePath(item.request);
        item.calendarCoverageKnown = calendar.coverageKnown;
        item.calendarSource = calendar.source;
        item.calendarReason = calendar.reason;
        refreshFileState(&item);
        applyCoverage(&item, existingBarsByDate.value(date), skipCompleteExisting);
        result.items.append(item);
    }
    if (result.items.isEmpty()) {
        result.error = QStringLiteral("日期范围内没有可计划的 A 股交易日");
        return result;
    }
    result.succeeded = true;
    return result;
}

int TickDataBatchPlanner::resetRetryableFiles(QVector<TickDataBatchPlanItem> *items)
{
    if (!items) return 0;
    int readyCount = 0;
    for (TickDataBatchPlanItem &item : *items) {
        if (item.status == TickDataBatchItemStatus::Succeeded
            || item.status == TickDataBatchItemStatus::Running
            || item.status == TickDataBatchItemStatus::SkippedExisting) {
            continue;
        }
        refreshFileState(&item);
        item.linesRead = 0;
        item.validRecords = 0;
        item.skippedRecords = 0;
        item.writtenBars = 0;
        item.missingContinuousBars = 0;
        item.qualityStatus = QStringLiteral("unknown");
        item.source.clear();
        if (item.status == TickDataBatchItemStatus::Ready) ++readyCount;
    }
    return readyCount;
}

void TickDataBatchPlanner::restoreCompletedItems(
    QVector<TickDataBatchPlanItem> *freshItems,
    const QVector<TickDataBatchPlanItem> &storedItems)
{
    if (!freshItems) return;
    QHash<QDate, TickDataBatchPlanItem> storedByDate;
    for (const TickDataBatchPlanItem &stored : storedItems) {
        storedByDate.insert(stored.request.tradingDate, stored);
    }
    for (TickDataBatchPlanItem &fresh : *freshItems) {
        const auto it = storedByDate.constFind(fresh.request.tradingDate);
        if (it == storedByDate.cend()
            || it->status != TickDataBatchItemStatus::Succeeded) continue;
        const QString filePath = fresh.filePath;
        const qint64 fileSize = fresh.fileSize;
        const TickDataImportRequest request = fresh.request;
        const int existingBars = fresh.existingBars;
        const int expectedBars = fresh.expectedBars;
        const QString coverageStatus = fresh.coverageStatus;
        const bool calendarKnown = fresh.calendarCoverageKnown;
        const QString calendarSource = fresh.calendarSource;
        const QString calendarReason = fresh.calendarReason;
        fresh = *it;
        fresh.request = request;
        fresh.filePath = filePath;
        fresh.fileSize = fileSize;
        fresh.existingBars = existingBars;
        fresh.expectedBars = expectedBars;
        fresh.coverageStatus = coverageStatus;
        fresh.willOverwrite = false;
        fresh.calendarCoverageKnown = calendarKnown;
        fresh.calendarSource = calendarSource;
        fresh.calendarReason = calendarReason;
    }
}

} // namespace pp
