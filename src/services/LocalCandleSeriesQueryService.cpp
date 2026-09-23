#include "services/LocalCandleSeriesQueryService.h"

#include "services/SqliteLocalDataStore.h"
#include "services/RawTickCandleAggregator.h"

#include <algorithm>
#include <cmath>
#include <QTimeZone>

namespace pp {

namespace {

QString aggregationSegment(const Candle &candle)
{
    static const QTimeZone shanghai("Asia/Shanghai");
    const QDateTime local = candle.startTime.toUTC().toTimeZone(shanghai);
    const int session = local.time() < QTime(9, 30) ? 0
        : local.time() < QTime(12, 0) ? 1 : 2;
    return QStringLiteral("%1:%2").arg(local.date().toString(Qt::ISODate)).arg(session);
}

} // namespace

LocalCandleSeriesQueryService::LocalCandleSeriesQueryService(SqliteLocalDataStore *store)
    : m_store(store)
{
}

bool LocalCandleSeriesQueryService::refreshRawTickCandleCacheDay(
    const Instrument &instrument, const QDate &tradingDate, int periodSeconds,
    bool *regenerated, QString *error) const
{
    if (regenerated) *regenerated = false;
    if (error) error->clear();
    if (!m_store || instrument.symbol.trimmed().isEmpty() || !tradingDate.isValid()
        || !RawTickCandleAggregator::validPeriod(periodSeconds)) {
        if (error) *error = QStringLiteral("逐笔 K 线局部刷新参数无效");
        return false;
    }
    if (m_store->rawTickCandleCacheFresh(instrument, tradingDate, periodSeconds)) return true;

    RawTickCandleAggregator aggregator(instrument, periodSeconds);
    if (!m_store->forEachRawTradeTick(
            instrument, tradingDate,
            [&aggregator](const RawTradeTick &tick) {
                aggregator.append(tick);
                return true;
            })) {
        if (error) *error = QStringLiteral("流式读取原始逐笔数据失败");
        return false;
    }
    const RawTradeFingerprint fingerprint =
        m_store->rawTradeFingerprint(instrument, tradingDate);
    if (fingerprint.count <= 0 || !m_store->replaceRawTickCandleCache(
            instrument, tradingDate, periodSeconds, fingerprint, aggregator.finish())) {
        if (error) *error = QStringLiteral("写入周期 K 线缓存失败");
        return false;
    }
    if (regenerated) *regenerated = true;
    return true;
}

LocalCandleSeriesResult LocalCandleSeriesQueryService::query(
    const LocalCandleSeriesQuery &request, const std::atomic_bool *cancelled) const
{
    LocalCandleSeriesResult result;
    result.query = request;
    const auto isCancelled = [cancelled]() { return cancelled && cancelled->load(); };
    if (isCancelled()) {
        result.error = QStringLiteral("本地查询已取消");
        return result;
    }
    if (!m_store) {
        result.error = QStringLiteral("本地分钟序列数据库不可用");
        return result;
    }
    if (request.instrument.symbol.trimmed().isEmpty()) {
        result.error = QStringLiteral("未指定本地分钟序列品种");
        return result;
    }
    if (!RawTickCandleAggregator::validPeriod(request.periodSeconds)) {
        result.error = QStringLiteral("K 线周期必须在 1–86400 秒之间");
        return result;
    }
    if (request.page < 0 || request.pageSize < 100 || request.pageSize > 5000
        || request.maximumPoints < 50 || request.maximumPoints > 2000) {
        result.error = QStringLiteral("分页或降采样参数超出允许范围");
        return result;
    }

    const QVector<QDate> rawDates = m_store->rawTradeTradingDates(request.instrument);
    result.rawTickBacked = !rawDates.isEmpty();
    for (const QDate &date : rawDates) {
        if (isCancelled()) {
            result.error = QStringLiteral("本地查询已取消");
            return result;
        }
        if (m_store->rawTickCandleCacheFresh(
                request.instrument, date, request.periodSeconds)) continue;
        RawTickCandleAggregator aggregator(request.instrument, request.periodSeconds);
        if (!m_store->forEachRawTradeTick(
                request.instrument, date,
                [&aggregator, &isCancelled](const RawTradeTick &tick) {
                    if (isCancelled()) return false;
                    aggregator.append(tick);
                    return true;
                })) {
            result.error = QStringLiteral("流式读取原始逐笔数据失败");
            return result;
        }
        if (isCancelled()) {
            result.error = QStringLiteral("本地查询已取消");
            return result;
        }
        const QVector<Candle> generated = aggregator.finish();
        const RawTradeFingerprint fingerprint =
            m_store->rawTradeFingerprint(request.instrument, date);
        if (!m_store->replaceRawTickCandleCache(
                request.instrument, date, request.periodSeconds,
                fingerprint, generated)) {
            result.error = QStringLiteral("写入周期 K 线缓存失败");
            return result;
        }
        ++result.regeneratedCacheDays;
    }

    if (request.latestTradingDayOnly) {
        QDate latestDate = rawDates.isEmpty() ? QDate{} : rawDates.last();
        if (!latestDate.isValid()) {
            bool latestSucceeded = false;
            const QVector<Candle> latest = m_store->loadCandlePage(
                request.instrument, request.periodSeconds, 0, 1, &latestSucceeded);
            if (latestSucceeded && !latest.isEmpty()) {
                latestDate = latest.last().startTime.toTimeZone(
                    QTimeZone("Asia/Shanghai")).date();
            }
        }
        result.resolvedTradingDate = latestDate;
        if (!latestDate.isValid()) {
            result.succeeded = true;
            return result;
        }
        static const QTimeZone shanghai("Asia/Shanghai");
        const QDateTime from(latestDate, QTime(0, 0), shanghai);
        QVector<Candle> dayCandles = m_store->loadCandles(
            request.instrument, request.periodSeconds, from.toUTC(),
            from.addDays(1).addMSecs(-1).toUTC());
        result.totalBars = dayCandles.size();
        result.pageCount = result.totalBars == 0 ? 0
            : (result.totalBars + request.pageSize - 1) / request.pageSize;
        if (result.totalBars == 0) {
            result.succeeded = true;
            return result;
        }
        if (request.page >= result.pageCount) {
            result.error = QStringLiteral("请求页码超出交易日数据范围");
            return result;
        }
        const int end = result.totalBars - request.page * request.pageSize;
        const int begin = std::max(0, end - request.pageSize);
        const QVector<Candle> pageCandles = dayCandles.mid(begin, end - begin);
        result.rawBarCount = pageCandles.size();
        result.points = aggregate(pageCandles, request.maximumPoints, &result.bucketSize);
        result.downsampled = result.bucketSize > 1;
        result.succeeded = true;
        return result;
    }

    bool countSucceeded = false;
    result.totalBars = m_store->candleCount(
        request.instrument, request.periodSeconds, &countSucceeded);
    if (!countSucceeded) {
        result.error = QStringLiteral("统计本地分钟序列失败");
        return result;
    }
    result.pageCount = result.totalBars == 0
        ? 0 : (result.totalBars + request.pageSize - 1) / request.pageSize;
    if (result.totalBars == 0) {
        result.succeeded = true;
        return result;
    }
    if (request.page >= result.pageCount) {
        result.error = QStringLiteral("请求页码超出本地数据范围");
        return result;
    }

    bool pageSucceeded = false;
    const QVector<Candle> candles = m_store->loadCandlePage(
        request.instrument, request.periodSeconds, request.page, request.pageSize,
        &pageSucceeded);
    if (!pageSucceeded) {
        result.error = QStringLiteral("读取本地分钟序列分页失败");
        return result;
    }
    result.rawBarCount = candles.size();
    result.points = aggregate(candles, request.maximumPoints, &result.bucketSize);
    result.downsampled = result.bucketSize > 1;
    result.succeeded = true;
    return result;
}

QVector<LocalCandleSeriesPoint> LocalCandleSeriesQueryService::aggregate(
    const QVector<Candle> &candles, int maximumPoints, int *bucketSize)
{
    QVector<LocalCandleSeriesPoint> points;
    if (candles.isEmpty() || maximumPoints <= 0) {
        if (bucketSize) *bucketSize = 1;
        return points;
    }

    const int groupSize = std::max(1, int(std::ceil(
        double(candles.size()) / double(maximumPoints))));
    if (bucketSize) *bucketSize = groupSize;
    points.reserve((candles.size() + groupSize - 1) / groupSize);
    for (int first = 0; first < candles.size();) {
        int last = std::min(first + groupSize, int(candles.size()));
        const QString segment = aggregationSegment(candles.at(first));
        for (int candidate = first + 1; candidate < last; ++candidate) {
            if (aggregationSegment(candles.at(candidate)) != segment) {
                last = candidate;
                break;
            }
        }
        LocalCandleSeriesPoint point;
        point.candle = candles.at(first);
        point.endTime = candles.at(last - 1).startTime;
        point.sourceCount = last - first;
        point.candle.high = candles.at(first).high;
        point.candle.low = candles.at(first).low;
        point.candle.volume = 0.0;
        point.candle.buyVolume = 0.0;
        point.candle.sellVolume = 0.0;
        point.candle.neutralVolume = 0.0;
        point.candle.directionalVolumeKnown = true;
        const QString firstSource = candles.at(first).source;
        bool sameSource = true;
        for (int index = first; index < last; ++index) {
            const Candle &candle = candles.at(index);
            point.candle.high = std::max(point.candle.high, candle.high);
            point.candle.low = std::min(point.candle.low, candle.low);
            point.candle.volume += candle.volume;
            point.candle.buyVolume += candle.buyVolume;
            point.candle.sellVolume += candle.sellVolume;
            point.candle.neutralVolume += candle.neutralVolume;
            point.candle.directionalVolumeKnown =
                point.candle.directionalVolumeKnown && candle.directionalVolumeKnown;
            sameSource = sameSource && candle.source == firstSource;
        }
        point.candle.close = candles.at(last - 1).close;
        point.candle.source = sameSource ? firstSource : QStringLiteral("mixed_local");
        points.append(point);
        first = last;
    }
    return points;
}

} // namespace pp
