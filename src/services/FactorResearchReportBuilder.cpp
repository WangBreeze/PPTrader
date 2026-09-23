#include "services/FactorResearchReportBuilder.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace pp {

namespace {

QString markdownCell(QString value)
{
    value.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    value.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    value.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    value.replace(QLatin1Char('`'), QStringLiteral("&#96;"));
    value.replace(QLatin1Char('|'), QStringLiteral("\\|"));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return value;
}

QString htmlText(QString value)
{
    value.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    value.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    value.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return value;
}

QString decimal(double value, int precision = 4)
{
    return std::isfinite(value) ? QString::number(value, 'f', precision)
                                : QStringLiteral("--");
}

QString dateText(const QDate &date)
{
    return date.isValid() ? date.toString(Qt::ISODate) : QStringLiteral("--");
}

QString timeText(const QDateTime &time)
{
    return time.isValid()
        ? time.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral("--");
}

QString polylinePoints(const QVector<FactorResearchPoint> &series,
                       bool rank, double width, double height)
{
    if (series.isEmpty()) return {};
    QStringList points;
    points.reserve(series.size());
    for (int index = 0; index < series.size(); ++index) {
        const double value = std::clamp(
            rank ? series.at(index).rankIc : series.at(index).ic, -1.0, 1.0);
        const double x = series.size() == 1
            ? width / 2.0
            : static_cast<double>(index) * width
                  / static_cast<double>(series.size() - 1);
        const double y = (1.0 - (value + 1.0) / 2.0) * height;
        points.append(QStringLiteral("%1,%2")
                          .arg(decimal(x, 2), decimal(y, 2)));
    }
    return points.join(QLatin1Char(' '));
}

QString icChart(const FactorResearchSummary &summary)
{
    constexpr double PlotWidth = 680.0;
    constexpr double PlotHeight = 160.0;
    QString svg = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"720\" height=\"210\" "
        "viewBox=\"0 0 720 210\" role=\"img\" aria-label=\"IC 与 Rank IC 时间序列\">\n"
        "<rect width=\"720\" height=\"210\" fill=\"#101820\" rx=\"8\"/>\n"
        "<line x1=\"20\" y1=\"100\" x2=\"700\" y2=\"100\" stroke=\"#4b5b66\"/>\n");
    if (summary.timeSeries.isEmpty()) {
        svg += QStringLiteral(
            "<text x=\"360\" y=\"108\" text-anchor=\"middle\" "
            "fill=\"#91a1ad\" font-size=\"13\">无有效 IC 序列</text>\n");
    } else {
        svg += QStringLiteral(
                   "<polyline fill=\"none\" stroke=\"#21c7d9\" stroke-width=\"2\" "
                   "points=\"%1\" transform=\"translate(20 20)\"/>\n")
                   .arg(polylinePoints(summary.timeSeries, false,
                                       PlotWidth, PlotHeight));
        svg += QStringLiteral(
                   "<polyline fill=\"none\" stroke=\"#a678ff\" stroke-width=\"2\" "
                   "points=\"%1\" transform=\"translate(20 20)\"/>\n")
                   .arg(polylinePoints(summary.timeSeries, true,
                                       PlotWidth, PlotHeight));
    }
    svg += QStringLiteral(
        "<text x=\"22\" y=\"202\" fill=\"#21c7d9\" font-size=\"11\">IC</text>"
        "<text x=\"52\" y=\"202\" fill=\"#a678ff\" font-size=\"11\">Rank IC</text>\n"
        "</svg>\n");
    return svg;
}

QString quantileChart(const FactorResearchSummary &summary)
{
    QString svg = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"520\" height=\"210\" "
        "viewBox=\"0 0 520 210\" role=\"img\" aria-label=\"Q1 到 Q5 成本后分层收益\">\n"
        "<rect width=\"520\" height=\"210\" fill=\"#101820\" rx=\"8\"/>\n"
        "<line x1=\"20\" y1=\"100\" x2=\"500\" y2=\"100\" stroke=\"#4b5b66\"/>\n");
    double maximum = 0.0;
    for (const FactorQuantileReturn &quantile : summary.quantileReturns)
        maximum = std::max(maximum, std::abs(quantile.meanNetForwardReturn));
    maximum = std::max(maximum, 1e-12);
    for (int index = 0; index < summary.quantileReturns.size(); ++index) {
        const FactorQuantileReturn &quantile = summary.quantileReturns.at(index);
        const double value = quantile.meanNetForwardReturn;
        const double barHeight = std::min(78.0, std::abs(value) / maximum * 78.0);
        const double x = 35.0 + index * 94.0;
        const double y = value >= 0.0 ? 100.0 - barHeight : 100.0;
        svg += QStringLiteral(
                   "<rect x=\"%1\" y=\"%2\" width=\"54\" height=\"%3\" "
                   "fill=\"%4\"/>\n"
                   "<text x=\"%5\" y=\"192\" text-anchor=\"middle\" "
                   "fill=\"#c6d0d8\" font-size=\"11\">Q%6</text>\n")
                   .arg(decimal(x, 1), decimal(y, 1), decimal(barHeight, 1),
                        value >= 0.0 ? QStringLiteral("#31d17c")
                                     : QStringLiteral("#ff6673"),
                        decimal(x + 27.0, 1))
                   .arg(quantile.quantile);
    }
    svg += QStringLiteral("</svg>\n");
    return svg;
}

bool validateArchive(const PortfolioArchiveRecord &record,
                     QVariantMap *factorIdentity,
                     QString *error)
{
    const QVariantMap identity = record.result.immutableIdentity
        .value(QStringLiteral("factorResearch")).toMap();
    const FactorResearchSummary &summary = record.result.factorResearchSummary;
    if (record.resultId.trimmed().isEmpty() || !record.completedAt.isValid()
        || record.result.resultKind != QStringLiteral("cross_sectional")
        || identity.isEmpty() || summary.factorId.trimmed().isEmpty()
        || identity.value(QStringLiteral("factorId")).toString()
               != summary.factorId
        || identity.value(QStringLiteral("factorVersion")).toInt()
               != summary.factorVersion
        || identity.value(QStringLiteral("alias")).toString()
               != summary.factorAlias) {
        if (error) *error = QStringLiteral("归档没有完整的不可变因子研究身份");
        return false;
    }
    if (factorIdentity) *factorIdentity = identity;
    if (error) error->clear();
    return true;
}

} // namespace

QString FactorResearchReportBuilder::buildMarkdown(
    const PortfolioArchiveRecord &record, QString *error)
{
    QVariantMap factorIdentity;
    if (!validateArchive(record, &factorIdentity, error)) return {};
    const PortfolioResult &result = record.result;
    const FactorResearchSummary &summary = result.factorResearchSummary;
    const QVariantMap immutable = result.immutableIdentity;
    const QString strategyId = immutable.value(QStringLiteral("strategyId")).toString();
    const int strategyVersion = immutable.value(QStringLiteral("strategyVersion")).toInt();
    QString report;
    report += QStringLiteral("# 因子研究报告\n\n");
    report += QStringLiteral("> 本报告只解释已归档的不可变历史结果，不重新运行策略、不访问网络，也不构成投资建议。\n\n");
    report += QStringLiteral("## 归档身份\n\n");
    report += QStringLiteral("- 结果 ID：`%1`\n").arg(markdownCell(record.resultId));
    report += QStringLiteral("- 完成时间（UTC）：`%1`\n").arg(timeText(record.completedAt));
    report += QStringLiteral("- 因子：`%1 · v%2`（`%3`）\n")
                  .arg(markdownCell(summary.factorAlias))
                  .arg(summary.factorVersion)
                  .arg(markdownCell(summary.factorId));
    report += QStringLiteral("- 策略：`%1 · v%2`\n")
                  .arg(markdownCell(strategyId)).arg(strategyVersion);
    report += QStringLiteral("- 因子定义 SHA-256：`%1`\n")
                  .arg(markdownCell(factorIdentity
                      .value(QStringLiteral("definitionSha256")).toString()));
    report += QStringLiteral("- 研究方法：`%1`\n\n")
                  .arg(markdownCell(factorIdentity
                      .value(QStringLiteral("researchMethodVersion")).toString()));

    report += QStringLiteral("## 数据集身份\n\n");
    report += QStringLiteral("| 标的 | 周期（秒） | 来源 | 日期范围 | 原始成交 | 派生 K 线 | 缺口日 | 数据版本 |\n");
    report += QStringLiteral("| --- | ---: | --- | --- | ---: | ---: | ---: | --- |\n");
    for (const BacktestDatasetIdentity &dataset : result.datasetIdentities) {
        report += QStringLiteral("| %1 | %2 | %3 | %4 → %5 | %6 | %7 | %8 | `%9` |\n")
                      .arg(markdownCell(dataset.instrument.symbol))
                      .arg(dataset.periodSeconds)
                      .arg(markdownCell(dataset.source))
                      .arg(dateText(dataset.firstTradingDate),
                           dateText(dataset.lastTradingDate))
                      .arg(dataset.rawTickTotal())
                      .arg(dataset.derivedBarTotal())
                      .arg(dataset.gapDayCount)
                      .arg(markdownCell(dataset.datasetVersion));
    }
    report += QLatin1Char('\n');

    report += QStringLiteral("## IC 与统计推断\n\n");
    report += QStringLiteral("| 指标 | 均值 | 样本标准差 | ICIR（不年化） | 95% HAC CI | 双侧 p 值 |\n");
    report += QStringLiteral("| --- | ---: | ---: | ---: | --- | ---: |\n");
    const QString icInterval = summary.icInferenceAvailable
        ? QStringLiteral("[%1, %2]").arg(decimal(summary.icConfidenceLower),
                                          decimal(summary.icConfidenceUpper))
        : QStringLiteral("不可用");
    const QString rankInterval = summary.rankIcInferenceAvailable
        ? QStringLiteral("[%1, %2]").arg(decimal(summary.rankIcConfidenceLower),
                                          decimal(summary.rankIcConfidenceUpper))
        : QStringLiteral("不可用");
    report += QStringLiteral("| IC | %1 | %2 | %3 | %4 | %5 |\n")
                  .arg(decimal(summary.meanIc), decimal(summary.icStandardDeviation),
                       summary.icirAvailable ? decimal(summary.icInformationRatio)
                                             : QStringLiteral("不可用"),
                       icInterval,
                       summary.icInferenceAvailable
                           ? decimal(summary.icTwoSidedPValue)
                           : QStringLiteral("不可用"));
    report += QStringLiteral("| Rank IC | %1 | %2 | %3 | %4 | %5 |\n\n")
                  .arg(decimal(summary.meanRankIc),
                       decimal(summary.rankIcStandardDeviation),
                       summary.rankIcirAvailable
                           ? decimal(summary.rankIcInformationRatio)
                           : QStringLiteral("不可用"),
                       rankInterval,
                       summary.rankIcInferenceAvailable
                           ? decimal(summary.rankIcTwoSidedPValue)
                           : QStringLiteral("不可用"));
    report += QStringLiteral("- 有效帧：%1；覆盖率：%2%；HAC 滞后阶数：%3。\n")
                  .arg(summary.evaluatedFrameCount)
                  .arg(decimal(summary.coverageRatio * 100.0, 2))
                  .arg(summary.hacLag);
    report += QStringLiteral("- 推断方法：`%1`；置信水平：%2。\n\n")
                  .arg(markdownCell(factorIdentity
                      .value(QStringLiteral("inferenceMethodVersion")).toString()),
                       decimal(factorIdentity
                           .value(QStringLiteral("confidenceLevel")).toDouble(), 2));
    report += icChart(summary) + QLatin1Char('\n');

    report += QStringLiteral("## 时间有序样本外验证\n\n");
    const FactorOutOfSampleSummary &outOfSample = summary.outOfSample;
    const QString outOfSampleMethod = factorIdentity
        .value(QStringLiteral("outOfSampleMethodVersion")).toString();
    if (outOfSampleMethod != QStringLiteral("expanding_oos_v1")) {
        report += QStringLiteral(
            "该旧归档未包含样本外方法身份；不回填或推测结果。\n\n");
    } else if (!outOfSample.available) {
        report += QStringLiteral("- 方法：`%1`；策略：`%2`。\n")
                      .arg(markdownCell(outOfSampleMethod),
                           markdownCell(outOfSample.policy.method));
        report += QStringLiteral("- 不可用：%1\n\n")
                      .arg(markdownCell(outOfSample.availabilityReason));
    } else {
        report += QStringLiteral(
            "- 方法：`%1`；策略：`%2`；初始训练 %3 帧，测试窗 %4 帧，步长 %5 帧。\n")
                      .arg(markdownCell(outOfSampleMethod),
                           markdownCell(outOfSample.policy.method))
                      .arg(outOfSample.policy.initialTrainingFrames)
                      .arg(outOfSample.policy.evaluationFrames)
                      .arg(outOfSample.policy.stepFrames);
        report += QStringLiteral(
            "- 测试帧：%1；样本外 IC：%2；Rank IC：%3；HAC 滞后：%4。\n")
                      .arg(outOfSample.evaluationFrameCount)
                      .arg(decimal(outOfSample.meanIc),
                           decimal(outOfSample.meanRankIc))
                      .arg(outOfSample.hacLag);
        report += QStringLiteral("- 样本外 IC 95% HAC CI：%1；双侧 p：%2。\n\n")
                      .arg(outOfSample.icInferenceAvailable
                               ? QStringLiteral("[%1, %2]")
                                     .arg(decimal(outOfSample.icConfidenceLower),
                                          decimal(outOfSample.icConfidenceUpper))
                               : QStringLiteral("不可用"),
                           outOfSample.icInferenceAvailable
                               ? decimal(outOfSample.icTwoSidedPValue)
                               : QStringLiteral("不可用"));
        report += QStringLiteral(
            "| 窗口 | 训练帧 | 测试帧 | 训练截止（UTC） | 测试范围（UTC） | 测试 IC |\n");
        report += QStringLiteral(
            "| ---: | ---: | ---: | --- | --- | ---: |\n");
        for (const FactorOutOfSampleWindow &window : outOfSample.windows) {
            report += QStringLiteral("| %1 | %2 | %3 | %4 | %5 → %6 | %7 |\n")
                          .arg(window.window)
                          .arg(window.trainingFrameCount)
                          .arg(window.evaluationFrameCount)
                          .arg(timeText(window.trainingToFactorTimestamp),
                               timeText(window.evaluationFromFactorTimestamp),
                               timeText(window.evaluationToFactorTimestamp),
                               decimal(window.meanIc));
        }
        report += QLatin1Char('\n');
    }
    report += QStringLiteral(
        "> 多重检验校正依赖当前显式比较候选集，不写入单个归档报告；候选集变化时 Holm/BH 结果也会变化。\n\n");

    report += QStringLiteral("## 样本内三段稳定性\n\n");
    if (summary.temporalStabilityAvailable) {
        report += QStringLiteral("| 区段 | 因子时刻范围（UTC） | 帧数 | IC 均值 | Rank IC 均值 |\n");
        report += QStringLiteral("| --- | --- | ---: | ---: | ---: |\n");
        for (const FactorTemporalSegment &segment : summary.temporalSegments) {
            report += QStringLiteral("| S%1 | %2 → %3 | %4 | %5 | %6 |\n")
                          .arg(segment.segment)
                          .arg(timeText(segment.fromFactorTimestamp),
                               timeText(segment.toFactorTimestamp))
                          .arg(segment.frameCount)
                          .arg(decimal(segment.meanIc),
                               decimal(segment.meanRankIc));
        }
        report += QStringLiteral("\n- IC 三段：%1；Rank IC 三段：%2。\n\n")
                      .arg(summary.icSameSignAcrossSegments
                               ? QStringLiteral("同号") : QStringLiteral("存在变号"),
                           summary.rankIcSameSignAcrossSegments
                               ? QStringLiteral("同号") : QStringLiteral("存在变号"));
    } else {
        report += QStringLiteral("有效帧不足，未提供样本内三段结果。\n\n");
    }

    report += QStringLiteral("## 五分层与成本\n\n");
    report += QStringLiteral("| 分层 | 有效帧 | 毛收益 | 成本后收益 | 平均单边换手 | 平均假设成本 |\n");
    report += QStringLiteral("| --- | ---: | ---: | ---: | ---: | ---: |\n");
    for (const FactorQuantileReturn &quantile : summary.quantileReturns) {
        report += QStringLiteral("| Q%1 | %2 | %3 | %4 | %5 | %6 |\n")
                      .arg(quantile.quantile)
                      .arg(quantile.evaluatedFrameCount)
                      .arg(decimal(quantile.meanForwardReturn),
                           decimal(quantile.meanNetForwardReturn),
                           decimal(quantile.meanOneWayTurnover),
                           decimal(quantile.meanTransactionCost));
    }
    report += QStringLiteral("\n- Q5−Q1 毛收益：%1；成本后收益：%2；单边成本假设：%3 bps。\n\n")
                  .arg(decimal(summary.topMinusBottomMeanReturn),
                       decimal(summary.topMinusBottomMeanNetReturn),
                       decimal(summary.costPolicy.oneWayCostBps, 2));
    report += quantileChart(summary) + QLatin1Char('\n');

    report += QStringLiteral("## 结果与边界\n\n");
    report += QStringLiteral("- 组合总收益：%1%；最大回撤：%2%；成交数：%3。\n")
                  .arg(decimal(result.summary.totalReturnPercent, 2),
                       decimal(result.summary.maxDrawdownPercent, 2))
                  .arg(result.summary.totalTrades);
    report += QStringLiteral("- IC 使用当前因子帧到下一可用对齐帧的收盘收益；ICIR 不年化。\n");
    report += QStringLiteral("- HAC 区间修正序列自相关，但不证明因果、样本外有效或未来收益。\n");
    report += outOfSample.available
        ? QStringLiteral("- 样本外结果按不可变扩展窗身份计算，但仍不证明未来收益；单归档不包含多重检验校正。\n")
        : QStringLiteral("- 三段结果均属于同一回测样本；当前归档没有可用的时间有序样本外结果。\n");
    report += QStringLiteral("- 分层成本是研究假设，不等于真实成交费用或市场冲击。\n\n");

    report += QStringLiteral(
        "## 不可变身份原文\n\n<details><summary>展开 JSON 身份</summary><pre><code>");
    report += htmlText(QString::fromUtf8(
        QJsonDocument::fromVariant(result.immutableIdentity)
            .toJson(QJsonDocument::Indented)));
    report += QStringLiteral("</code></pre></details>\n");
    if (error) error->clear();
    return report;
}

bool FactorResearchReportBuilder::writeMarkdownAtomically(
    const PortfolioArchiveRecord &record, const QString &filePath,
    QString *error)
{
    const QString normalizedPath = QDir::cleanPath(filePath.trimmed());
    const QFileInfo target(normalizedPath);
    if (normalizedPath.isEmpty() || !target.isAbsolute()
        || target.fileName().isEmpty()) {
        if (error) *error = QStringLiteral("请选择有效的本地绝对文件路径");
        return false;
    }
    QString buildError;
    const QString report = buildMarkdown(record, &buildError);
    if (report.isEmpty()) {
        if (error) *error = buildError;
        return false;
    }
    QSaveFile file(normalizedPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("无法打开报告文件：%1")
                         .arg(file.errorString());
        }
        return false;
    }
    const QByteArray bytes = report.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) {
            *error = QStringLiteral("报告原子保存失败：%1")
                         .arg(file.errorString());
        }
        file.cancelWriting();
        return false;
    }
    if (error) error->clear();
    return true;
}

} // namespace pp
