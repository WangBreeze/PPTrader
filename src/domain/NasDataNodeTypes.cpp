#include "domain/NasDataNodeTypes.h"

namespace pp {

QVariantMap NasCatalogEntry::toVariantMap() const
{
    return {{QStringLiteral("symbol"), symbol},
            {QStringLiteral("tradingDate"), tradingDate.toString(Qt::ISODate)},
            {QStringLiteral("format"), format},
            {QStringLiteral("size"), size},
            {QStringLiteral("sha256"), sha256},
            {QStringLiteral("version"), version},
            {QStringLiteral("downloadPath"), downloadPath}};
}

QVariantMap NasSyncPlanItem::toVariantMap() const
{
    QString statusText;
    switch (status) {
    case NasSyncItemStatus::Ready: statusText = QStringLiteral("待同步"); break;
    case NasSyncItemStatus::Downloading: statusText = QStringLiteral("下载中"); break;
    case NasSyncItemStatus::Importing: statusText = QStringLiteral("导入中"); break;
    case NasSyncItemStatus::Succeeded: statusText = QStringLiteral("已完成"); break;
    case NasSyncItemStatus::Failed: statusText = QStringLiteral("失败"); break;
    case NasSyncItemStatus::Cancelled: statusText = QStringLiteral("已取消"); break;
    }
    QVariantMap result = entry.toVariantMap();
    result.insert(QStringLiteral("status"), static_cast<int>(status));
    result.insert(QStringLiteral("statusText"), statusText);
    result.insert(QStringLiteral("rawTicksWritten"), rawTicksWritten);
    result.insert(QStringLiteral("candleBarsWritten"), candleBarsWritten);
    result.insert(QStringLiteral("fromCache"), fromCache);
    result.insert(QStringLiteral("transferElapsedMilliseconds"), transferElapsedMilliseconds);
    result.insert(QStringLiteral("parseElapsedMilliseconds"), parseElapsedMilliseconds);
    result.insert(QStringLiteral("rawDatabaseElapsedMilliseconds"), rawDatabaseElapsedMilliseconds);
    result.insert(QStringLiteral("candleDatabaseElapsedMilliseconds"), candleDatabaseElapsedMilliseconds);
    result.insert(QStringLiteral("importElapsedMilliseconds"), importElapsedMilliseconds);
    result.insert(QStringLiteral("error"), error);
    return result;
}

QVariantMap NasSyncSummary::toVariantMap() const
{
    return {{QStringLiteral("total"), total},
            {QStringLiteral("completed"), completed},
            {QStringLiteral("succeeded"), succeeded},
            {QStringLiteral("failed"), failed},
            {QStringLiteral("pending"), pending},
            {QStringLiteral("symbols"), symbols},
            {QStringLiteral("totalBytes"), totalBytes},
            {QStringLiteral("succeededBytes"), succeededBytes},
            {QStringLiteral("rawTicksWritten"), rawTicksWritten},
            {QStringLiteral("candleBarsWritten"), candleBarsWritten},
            {QStringLiteral("transferElapsedMilliseconds"), transferElapsedMilliseconds},
            {QStringLiteral("parseElapsedMilliseconds"), parseElapsedMilliseconds},
            {QStringLiteral("rawDatabaseElapsedMilliseconds"), rawDatabaseElapsedMilliseconds},
            {QStringLiteral("candleDatabaseElapsedMilliseconds"), candleDatabaseElapsedMilliseconds},
            {QStringLiteral("importElapsedMilliseconds"), importElapsedMilliseconds},
            {QStringLiteral("cacheHits"), cacheHits}};
}

} // namespace pp
