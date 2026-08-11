#include "services/MarketSnapshotQueryService.h"

#include "services/LocalDataStore.h"

#include <QTimeZone>

#include <algorithm>

namespace pp {

MarketSnapshotQueryService::MarketSnapshotQueryService(LocalDataStore *store)
    : m_store(store)
{
}

std::optional<QDateTime> MarketSnapshotQueryService::parseMarketTime(const QString &value,
                                                                     QString *error)
{
    if (error) error->clear();
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        if (error) *error = QStringLiteral("快照时刻不能为空");
        return std::nullopt;
    }

    const QTimeZone shanghai("Asia/Shanghai");
    const QStringList localFormats {
        QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm"),
        QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz"),
        QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd'T'HH:mm")
    };
    for (const QString &format : localFormats) {
        const QDateTime local = QDateTime::fromString(trimmed, format);
        if (local.isValid()) {
            return QDateTime(local.date(), local.time(), shanghai).toUTC();
        }
    }

    QDateTime zoned = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
    if (!zoned.isValid()) zoned = QDateTime::fromString(trimmed, Qt::ISODate);
    if (zoned.isValid() && (trimmed.endsWith(QLatin1Char('Z'), Qt::CaseInsensitive)
        || trimmed.contains(QLatin1Char('+'))
        || trimmed.lastIndexOf(QLatin1Char('-')) > 9)) {
        return zoned.toUTC();
    }

    if (error) *error = QStringLiteral("快照时刻格式无效，应使用 yyyy-MM-dd HH:mm:ss");
    return std::nullopt;
}

MarketSnapshotQueryResult MarketSnapshotQueryService::evaluate(
    const MarketSnapshot &snapshot, const QDateTime &requestedTime,
    qint64 maximumStalenessMilliseconds)
{
    MarketSnapshotQueryResult result;
    result.snapshot = snapshot;
    result.requestedTime = requestedTime.toUTC();
    result.resolvedTime = snapshot.timestamp.toUTC();
    result.found = snapshot.timestamp.isValid();
    if (!result.found) {
        result.health = MarketSnapshotHealth::Error;
        result.error = QStringLiteral("快照缺少有效时刻");
        return result;
    }

    result.stalenessMilliseconds = std::max<qint64>(
        0, result.resolvedTime.msecsTo(result.requestedTime));
    if (snapshot.quotes.isEmpty()) {
        result.health = MarketSnapshotHealth::Empty;
        result.error = QStringLiteral("命中的已完成快照没有可用行情");
    } else if (result.stalenessMilliseconds > std::max<qint64>(0, maximumStalenessMilliseconds)) {
        result.health = MarketSnapshotHealth::Stale;
        result.error = QStringLiteral("最近快照已超过允许的 10 秒陈旧度");
    } else if (snapshot.completenessRatio() < 0.90) {
        result.health = MarketSnapshotHealth::Incomplete;
        result.error = QStringLiteral("快照完整率低于 90%");
    } else {
        result.health = MarketSnapshotHealth::Healthy;
    }
    return result;
}

MarketSnapshotQueryResult MarketSnapshotQueryService::query(
    const QDateTime &requestedTime, qint64 maximumStalenessMilliseconds) const
{
    if (!requestedTime.isValid()) {
        MarketSnapshotQueryResult result;
        result.health = MarketSnapshotHealth::Error;
        result.error = QStringLiteral("请求时刻无效");
        return result;
    }
    if (!m_store) {
        MarketSnapshotQueryResult result;
        result.requestedTime = requestedTime.toUTC();
        result.health = MarketSnapshotHealth::Error;
        result.error = QStringLiteral("快照存储服务不可用");
        return result;
    }
    const auto snapshot = m_store->loadMarketSnapshotAtOrBefore(requestedTime.toUTC());
    if (!snapshot.has_value()) {
        MarketSnapshotQueryResult result;
        result.requestedTime = requestedTime.toUTC();
        result.health = MarketSnapshotHealth::Empty;
        result.error = QStringLiteral("请求时刻之前没有已完成快照");
        return result;
    }
    return evaluate(*snapshot, requestedTime, maximumStalenessMilliseconds);
}

QString marketSnapshotHealthName(MarketSnapshotHealth health)
{
    switch (health) {
    case MarketSnapshotHealth::Empty: return QStringLiteral("empty");
    case MarketSnapshotHealth::Healthy: return QStringLiteral("healthy");
    case MarketSnapshotHealth::Incomplete: return QStringLiteral("incomplete");
    case MarketSnapshotHealth::Stale: return QStringLiteral("stale");
    case MarketSnapshotHealth::Error: return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

} // namespace pp
