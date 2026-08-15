#include "services/AlignedBarSeries.h"

#include <QList>
#include <QSet>

#include <algorithm>

namespace pp {

AlignedBarSeries AlignedBarSeries::build(const QHash<QString, QVector<Candle>> &series,
                                         QString *error)
{
    AlignedBarSeries result;
    if (series.isEmpty()) {
        if (error) *error = QStringLiteral("没有输入序列");
        return result;
    }

    // 每条腿按时间排序，并建立 timestamp → Candle 索引；同时收集全部时间戳。
    QSet<QDateTime> timestamps;
    QHash<QString, QHash<QDateTime, Candle>> indexBySymbol;
    int periodSeconds = 0;
    for (auto symbolIt = series.constBegin(); symbolIt != series.constEnd(); ++symbolIt) {
        QVector<Candle> candles = symbolIt.value();
        std::sort(candles.begin(), candles.end(),
                  [](const Candle &left, const Candle &right) {
            return left.startTime < right.startTime;
        });
        QHash<QDateTime, Candle> byTime;
        for (const Candle &candle : candles) {
            if (!candle.startTime.isValid()) continue;
            if (periodSeconds == 0) {
                periodSeconds = candle.periodSeconds;
            } else if (candle.periodSeconds != periodSeconds) {
                if (error) *error = QStringLiteral("序列周期不一致，无法对齐：%1")
                                        .arg(symbolIt.key());
                return {};
            }
            byTime.insert(candle.startTime, candle);
            timestamps.insert(candle.startTime);
        }
        indexBySymbol.insert(symbolIt.key(), byTime);
    }

    QList<QDateTime> sortedTimestamps = timestamps.values();
    std::sort(sortedTimestamps.begin(), sortedTimestamps.end());

    result.m_periodSeconds = periodSeconds;
    result.m_frames.reserve(sortedTimestamps.size());
    for (const QDateTime &timestamp : sortedTimestamps) {
        AlignedBarFrame frame;
        frame.timestamp = timestamp;
        for (auto symbolIt = indexBySymbol.constBegin();
             symbolIt != indexBySymbol.constEnd(); ++symbolIt) {
            const auto found = symbolIt.value().constFind(timestamp);
            if (found != symbolIt.value().constEnd()) {
                frame.bars.insert(symbolIt.key(), found.value());
            }
        }
        result.m_frames.append(frame);
    }

    if (error) error->clear();
    return result;
}

const AlignedBarFrame *AlignedBarSeries::frameAt(int frameIndex) const
{
    if (frameIndex < 0 || frameIndex >= m_frames.size()) return nullptr;
    return &m_frames.at(frameIndex);
}

} // namespace pp
