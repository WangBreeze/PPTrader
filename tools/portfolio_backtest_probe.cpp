#include "services/PortfolioBacktestEngine.h"
#include "services/SqliteLocalDataStore.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QUuid>

namespace {

pp::Instrument instrumentFor(const QString &symbol)
{
    pp::Instrument instrument;
    instrument.symbol = symbol;
    instrument.name = symbol;
    instrument.market = pp::Market::AShare;
    instrument.venue = symbol.startsWith(QLatin1Char('6'))
        ? QStringLiteral("XSHG") : QStringLiteral("XSHE");
    return instrument;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("d"), QStringLiteral("database")},
         QStringLiteral("装载后的 SQLite 路径"), QStringLiteral("path")},
        {{QStringLiteral("m"), QStringLiteral("symbols")},
         QStringLiteral("逗号分隔腿代码"), QStringLiteral("codes")},
        {{QStringLiteral("b"), QStringLiteral("bars")},
         QStringLiteral("回测根数"), QStringLiteral("count")},
        {{QStringLiteral("p"), QStringLiteral("period")},
         QStringLiteral("周期秒数"), QStringLiteral("seconds"), QStringLiteral("60")},
        {{QStringLiteral("o"), QStringLiteral("output")},
         QStringLiteral("结果 JSON"), QStringLiteral("path")},
    });
    parser.process(application);

    const QString databasePath = parser.value(QStringLiteral("database"));
    const QString outputPath = parser.value(QStringLiteral("output"));
    QStringList symbols;
    for (const QString &value : parser.value(QStringLiteral("symbols")).split(',')) {
        const QString symbol = value.trimmed();
        if (symbol.size() == 6 && !symbols.contains(symbol)) symbols.append(symbol);
    }
    bool barsOk = false;
    const int bars = parser.value(QStringLiteral("bars")).toInt(&barsOk);
    bool periodOk = false;
    const int periodSeconds =
        parser.value(QStringLiteral("period")).toInt(&periodOk);
    if (databasePath.isEmpty() || outputPath.isEmpty() || symbols.size() < 2
        || !barsOk || bars < 2 || !periodOk || periodSeconds <= 0
        || !QFileInfo::exists(databasePath)) {
        QTextStream(stderr) << QStringLiteral(
            "参数无效：需要 --database/--symbols(>=2)/--bars/--output") << Qt::endl;
        return 1;
    }

    pp::SqliteLocalDataStore store;
    if (!store.initialize(databasePath)) return 2;

    QJsonArray legResults;
    QHash<QString, QVector<pp::Candle>> series;
    QStringList datasetVersions;
    QString failure;
    for (const QString &symbol : symbols) {
        const pp::Instrument instrument = instrumentFor(symbol);
        const auto identity =
            store.loadBacktestDatasetIdentity(instrument, periodSeconds);
        if (!identity || identity->isEmpty()) {
            failure = QStringLiteral("组合腿缺少数据集身份：%1").arg(symbol);
            break;
        }
        if (identity->staleDayCount > 0) {
            failure = QStringLiteral("组合腿派生缓存过期：%1").arg(symbol);
            break;
        }
        QVector<pp::Candle> candles = store.loadRecentCandlesFromSource(
            instrument, periodSeconds, bars, QStringLiteral("raw_tick_cache_v1"));
        if (candles.size() < bars) {
            failure = QStringLiteral("组合腿 K 线不足：%1（%2/%3）")
                          .arg(symbol).arg(candles.size()).arg(bars);
            break;
        }
        series.insert(symbol, candles);
        datasetVersions.append(identity->datasetVersion);
        QJsonObject legResult;
        legResult.insert(QStringLiteral("symbol"), symbol);
        legResult.insert(QStringLiteral("bars"), candles.size());
        legResult.insert(QStringLiteral("datasetVersion"),
                         identity->datasetVersion);
        legResult.insert(QStringLiteral("rawTickCount"),
                         identity->rawTickTotal());
        legResult.insert(QStringLiteral("dayCount"), identity->days.size());
        legResults.append(legResult);
    }
    if (!failure.isEmpty()) {
        QTextStream(stderr) << failure << Qt::endl;
        return 3;
    }

    pp::PortfolioDefinition definition;
    definition.portfolioId = QStringLiteral("portfolio_probe");
    definition.name = QStringLiteral("组合回测探针");
    definition.periodSeconds = periodSeconds;
    definition.initialCapital = 1000000.0;
    definition.rebalanceEveryTradingDays = 1;
    for (const QString &symbol : symbols) {
        definition.legs.append({instrumentFor(symbol), 0.0});
    }

    QElapsedTimer engineTimer;
    engineTimer.start();
    QString engineError;
    pp::PortfolioResult result =
        pp::PortfolioBacktestEngine::run(definition, series, &engineError);
    const qint64 engineElapsedMilliseconds = engineTimer.elapsed();
    if (result.equityCurve.isEmpty() || !engineError.isEmpty()) {
        QTextStream(stderr) << (engineError.isEmpty()
            ? QStringLiteral("组合回测无结果") : engineError) << Qt::endl;
        return 4;
    }
    result.datasetVersions = datasetVersions;

    pp::PortfolioArchiveRecord archive;
    archive.resultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    archive.completedAt = QDateTime::currentDateTimeUtc();
    archive.result = result;
    QElapsedTimer archiveTimer;
    archiveTimer.start();
    const bool archived = store.savePortfolioBacktestArchive(archive);
    const qint64 archiveElapsedMilliseconds = archiveTimer.elapsed();

    QJsonObject report;
    report.insert(QStringLiteral("date"),
                  QDate::currentDate().toString(Qt::ISODate));
    report.insert(QStringLiteral("legs"), legResults);
    report.insert(QStringLiteral("periodSeconds"), periodSeconds);
    report.insert(QStringLiteral("bars"), bars);
    report.insert(QStringLiteral("engineElapsedMilliseconds"),
                  static_cast<double>(engineElapsedMilliseconds));
    report.insert(QStringLiteral("archiveElapsedMilliseconds"),
                  static_cast<double>(archiveElapsedMilliseconds));
    report.insert(QStringLiteral("archived"), archived);
    report.insert(QStringLiteral("resultId"), archive.resultId);
    report.insert(QStringLiteral("equityPoints"),
                  result.equityCurve.size());
    report.insert(QStringLiteral("trades"), result.trades.size());
    report.insert(QStringLiteral("finalEquity"), result.summary.finalEquity);
    report.insert(QStringLiteral("totalReturnPercent"),
                  result.summary.totalReturnPercent);
    report.insert(QStringLiteral("maxDrawdownPercent"),
                  result.summary.maxDrawdownPercent);
    report.insert(QStringLiteral("sharpeRatio"),
                  result.summary.sharpeRatio);
    report.insert(QStringLiteral("totalCommission"),
                  result.summary.totalCommission);

    const QByteArray payload = QJsonDocument(report).toJson(QJsonDocument::Indented);
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || output.write(payload) < 0) {
        return 5;
    }
    QTextStream(stdout) << QString::fromUtf8(payload) << Qt::endl;
    return 0;
}
