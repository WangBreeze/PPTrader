#pragma once

#include <QTime>

#include <optional>

namespace pp {

// 项目授权的历史 Tick 数据窗口。该窗口允许收盘后数据延伸到 15:30，
// 不代表交易所连续竞价或委托门禁延长到 15:30。
class HistoricalTickSessionRules final {
public:
    static bool accepts(const QTime &time)
    {
        return (time >= QTime(9, 25) && time <= QTime(11, 30))
            || (time >= QTime(13, 0) && time <= QTime(15, 30));
    }

    static std::optional<QTime> bucketStart(const QTime &time, int periodSeconds)
    {
        if (periodSeconds < 1 || periodSeconds > 86400 || !accepts(time))
            return std::nullopt;
        if (periodSeconds >= 14400) return QTime(9, 25);
        if (time >= QTime(9, 25) && time < QTime(9, 30)) return QTime(9, 25);

        struct Session { QTime start; QTime close; };
        const Session sessions[] {{QTime(9, 30), QTime(11, 30)},
                                  {QTime(13, 0), QTime(15, 30)}};
        for (const Session &session : sessions) {
            if (time < session.start || time > session.close) continue;
            const int duration = session.start.secsTo(session.close);
            const int elapsed = time == session.close
                ? (duration > 0 ? duration - 1 : 0)
                : session.start.secsTo(time);
            return session.start.addSecs((elapsed / periodSeconds) * periodSeconds);
        }
        return std::nullopt;
    }

    static bool validFiveMinuteLabel(const QTime &time)
    {
        if (time == QTime(9, 25) || time == QTime(11, 30)
            || time == QTime(15, 30)) {
            return true;
        }
        if (time >= QTime(9, 30) && time < QTime(11, 30))
            return QTime(9, 30).secsTo(time) % 300 == 0;
        if (time >= QTime(13, 0) && time < QTime(15, 30))
            return QTime(13, 0).secsTo(time) % 300 == 0;
        return false;
    }

    static int expectedContinuousBars(int periodSeconds)
    {
        constexpr int ContinuousSeconds = (120 + 150) * 60;
        return periodSeconds > 0 ? ContinuousSeconds / periodSeconds : 0;
    }
};

} // namespace pp
