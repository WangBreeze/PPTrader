#pragma once

#include "domain/MarketTypes.h"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

namespace pp {

// 对齐帧：同一时间戳各标的的 K 线快照。某标的该时刻无 bar 时不在 bars 中（absent）。
struct AlignedBarFrame {
    QDateTime timestamp;
    QHash<QString, Candle> bars;

    bool hasBar(const QString &symbol) const { return bars.contains(symbol); }
    const Candle *bar(const QString &symbol) const
    {
        const auto found = bars.constFind(symbol);
        return found == bars.constEnd() ? nullptr : &found.value();
    }
};

// 对齐帧序列：把多标的同周期 K 线按 startTime 对齐成帧序列。
// 单标的（n=1）时退化为单序列 → 单帧流；多标的时同一时刻各标的进入同一帧，
// 某标的该时刻无 bar 则标记 absent（不出现在该帧 bars 中）。
class AlignedBarSeries {
public:
    static AlignedBarSeries build(const QHash<QString, QVector<Candle>> &series,
                                  QString *error = nullptr);

    int frameCount() const { return m_frames.size(); }
    bool isEmpty() const { return m_frames.isEmpty(); }
    int periodSeconds() const { return m_periodSeconds; }

    // 只读访问第 frameIndex 帧；越界返回 nullptr。
    const AlignedBarFrame *frameAt(int frameIndex) const;
    // 逐帧回放语义别名（只读，不推进任何状态）。
    const AlignedBarFrame *replayBar(int frameIndex) const { return frameAt(frameIndex); }

    // 前向填充：返回 symbol 在 frameIndex（含）之前最近 bar 的 close。
    // stale 输出：true 表示该帧该 symbol 无 bar，使用了更早 bar 前向填充。
    // 若 frameIndex 之前无任何 bar，返回 nullopt。
    std::optional<double> forwardFillClose(const QString &symbol, int frameIndex,
                                           bool *stale = nullptr) const;

private:
    QVector<AlignedBarFrame> m_frames;
    int m_periodSeconds = 0;
    // 预计算前向填充表：symbol → 每帧最近 bar 的帧索引（-1 表示该帧之前无 bar）。
    QHash<QString, QVector<int>> m_lastBarFrame;
};

} // namespace pp
