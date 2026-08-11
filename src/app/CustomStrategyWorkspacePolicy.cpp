#include "app/CustomStrategyWorkspacePolicy.h"

#include <algorithm>

namespace pp {

QVariantMap CustomStrategyWorkspacePolicy::localRunAvailability(
    bool definitionValid, bool executable, const QString &strategyId, int version,
    int minimumWarmupBars, const LocalHistoryDataset *dataset,
    int requestedBars, bool backtestRunning)
{
    QString error;
    if (!definitionValid || strategyId.trimmed().isEmpty() || version < 1) {
        error = QStringLiteral("请先选择并校验已保存的自定义策略版本");
    } else if (!executable) {
        error = QStringLiteral("当前定义未达到 backtest_ready，不能运行");
    } else if (backtestRunning) {
        error = QStringLiteral("请等待当前回测结束或先取消任务");
    } else if (!dataset) {
        error = QStringLiteral("请先选择有效的 SQLite 本地回测数据集");
    } else if (!dataset->isBacktestEligible()) {
        error = QStringLiteral("回测只允许使用历史 Tick 派生 K 线；远端/缓存 K 线不可运行");
    } else if (requestedBars < std::max(2, minimumWarmupBars)) {
        error = QStringLiteral("当前配置需要至少 %1 根 K 线完成策略预热")
                    .arg(std::max(2, minimumWarmupBars));
    } else if (dataset->candleCount < requestedBars) {
        error = QStringLiteral("本地 %1 %2 仅 %3 根，当前回测需要 %4 根")
                    .arg(dataset->instrument.symbol, dataset->frequency())
                    .arg(dataset->candleCount).arg(requestedBars);
    }

    return {{QStringLiteral("available"), error.isEmpty()},
            {QStringLiteral("error"), error},
            {QStringLiteral("strategyId"), strategyId},
            {QStringLiteral("version"), version},
            {QStringLiteral("minimumWarmupBars"), std::max(0, minimumWarmupBars)},
            {QStringLiteral("requestedBars"), std::max(0, requestedBars)},
            {QStringLiteral("availableBars"), dataset ? dataset->candleCount : 0},
            {QStringLiteral("symbol"), dataset ? dataset->instrument.symbol : QString{}},
            {QStringLiteral("frequency"), dataset ? dataset->frequency() : QString{}}};
}

bool CustomStrategyWorkspacePolicy::resultMatches(
    const QVariantMap &resultConfiguration, const QString &strategyId, int version)
{
    if (resultConfiguration.isEmpty() || strategyId.isEmpty() || version < 1) return false;
    return resultConfiguration.value(QStringLiteral("strategyId")).toString() == strategyId
        && resultConfiguration.value(QStringLiteral("strategyVersion")).toInt() == version;
}

} // namespace pp
