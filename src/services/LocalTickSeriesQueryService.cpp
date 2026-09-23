#include "services/LocalTickSeriesQueryService.h"

#include "services/SqliteLocalDataStore.h"

#include <algorithm>
#include <cmath>

namespace pp {

namespace {

QVector<qint64> sampleIndices(qint64 count, int maximumPoints)
{
    QVector<qint64> result;
    if (count <= 0 || maximumPoints <= 0) return result;
    const int outputCount = int(std::min<qint64>(count, maximumPoints));
    result.reserve(outputCount);
    if (outputCount == 1) {
        result.append(0);
        return result;
    }
    for (int index = 0; index < outputCount; ++index) {
        const qint64 sourceIndex = qRound64(
            double(index) * double(count - 1) / double(outputCount - 1));
        if (result.isEmpty() || result.last() != sourceIndex) result.append(sourceIndex);
    }
    return result;
}

qint64 representedCount(const QVector<qint64> &indices, int offset, qint64 totalCount)
{
    if (offset < 0 || offset >= indices.size() || totalCount <= 0) return 0;
    const qint64 left = offset == 0
        ? 0 : (indices.at(offset - 1) + indices.at(offset)) / 2 + 1;
    const qint64 right = offset + 1 == indices.size()
        ? totalCount - 1 : (indices.at(offset) + indices.at(offset + 1)) / 2;
    return std::max<qint64>(1, right - left + 1);
}

} // namespace

LocalTickSeriesQueryService::LocalTickSeriesQueryService(SqliteLocalDataStore *store)
    : m_store(store)
{
}

LocalTickSeriesResult LocalTickSeriesQueryService::query(
    const LocalTickSeriesQuery &request, const std::atomic_bool *cancelled) const
{
    LocalTickSeriesResult result;
    result.query = request;
    const auto isCancelled = [cancelled]() { return cancelled && cancelled->load(); };
    if (isCancelled()) {
        result.error = QStringLiteral("本地查询已取消");
        return result;
    }
    if (!m_store) {
        result.error = QStringLiteral("本地逐笔数据库不可用");
        return result;
    }
    if (request.instrument.symbol.trimmed().isEmpty()) {
        result.error = QStringLiteral("请选择本地历史品种");
        return result;
    }
    if (request.maximumPoints < 2 || request.maximumPoints > 500000) {
        result.error = QStringLiteral("逐笔显示点数必须在 2–500000 之间");
        return result;
    }

    QDate day = request.tradingDate;
    const QVector<QDate> dates = m_store->rawTradeTradingDates(request.instrument);
    if (!day.isValid() && !dates.isEmpty()) day = dates.last();
    result.resolvedTradingDate = day;
    result.succeeded = true;
    if (!day.isValid() || !dates.contains(day)) return result;

    const RawTradeFingerprint fingerprint =
        m_store->rawTradeFingerprint(request.instrument, day);
    result.rawTickCount = fingerprint.count;
    const QVector<qint64> wanted = sampleIndices(fingerprint.count, request.maximumPoints);
    result.points.reserve(wanted.size());
    qint64 eventIndex = 0;
    int wantedOffset = 0;
    const bool readOk = m_store->forEachRawTradeTick(
        request.instrument, day, [&](const RawTradeTick &tick) {
            if (isCancelled()) return false;
            if (wantedOffset < wanted.size() && eventIndex == wanted.at(wantedOffset)) {
                LocalTickSeriesPoint point;
                point.tick = tick;
                point.eventIndex = eventIndex;
                point.representedCount = representedCount(
                    wanted, wantedOffset, fingerprint.count);
                result.points.append(point);
                ++wantedOffset;
            }
            ++eventIndex;
            return true;
        });
    if (!readOk || isCancelled()) {
        result.succeeded = false;
        result.points.clear();
        result.error = QStringLiteral("读取本地逐笔成交失败");
        return result;
    }
    result.rawTickCount = eventIndex;
    result.downsampled = eventIndex > result.points.size();
    return result;
}

QVector<LocalTickSeriesPoint> LocalTickSeriesQueryService::sampleByEventIndex(
    const QVector<RawTradeTick> &ticks, int maximumPoints)
{
    QVector<LocalTickSeriesPoint> result;
    const QVector<qint64> indices = sampleIndices(ticks.size(), maximumPoints);
    result.reserve(indices.size());
    for (int offset = 0; offset < indices.size(); ++offset) {
        const qint64 index = indices.at(offset);
        LocalTickSeriesPoint point;
        point.tick = ticks.at(int(index));
        point.eventIndex = index;
        point.representedCount = representedCount(indices, offset, ticks.size());
        result.append(point);
    }
    return result;
}

} // namespace pp
