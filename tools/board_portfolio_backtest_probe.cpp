#include "services/BoardPortfolioBacktestEngine.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>

namespace {

QString sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject tradeObject(const pp::PortfolioTrade &trade)
{
    return {{QStringLiteral("symbol"), trade.instrument.symbol},
            {QStringLiteral("timestampUtc"),
             trade.timestamp.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("side"), trade.side},
            {QStringLiteral("quantity"), trade.quantity},
            {QStringLiteral("price"), trade.price},
            {QStringLiteral("commission"), trade.commission},
            {QStringLiteral("cashAmount"), trade.amount},
            {QStringLiteral("reason"), trade.reason}};
}

bool validatePriorRuns(const QStringList &paths, const QString &databaseHash,
                       QSet<QString> *symbols, QSet<QString> *eligibleEntries,
                       int *caseCount, QString *error)
{
    if (paths.isEmpty()) {
        *error = QStringLiteral("共享资金研究必须指定前序逐窗严格准入摘要");
        return false;
    }
    for (const QString &path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("无法读取严格准入摘要：%1").arg(path);
            return false;
        }
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject()) {
            *error = QStringLiteral("严格准入摘要不是 JSON 对象：%1").arg(path);
            return false;
        }
        const QJsonObject object = document.object();
        if (!object.value(QStringLiteral("strictDataQualityGate")).toBool()
            || !object.value(QStringLiteral("archiveSkipped")).toBool()) {
            *error = QStringLiteral("前序回测未启用严格准入与研究隔离：%1").arg(path);
            return false;
        }
        const QJsonArray cases = object.value(QStringLiteral("cases")).toArray();
        if (cases.isEmpty()) {
            *error = QStringLiteral("前序摘要没有有效事件：%1").arg(path);
            return false;
        }
        for (const QJsonValue &value : cases) {
            const QJsonObject item = value.toObject();
            if (item.value(QStringLiteral("succeeded")).toInt() != 1
                || item.value(QStringLiteral("completed")).toInt() != 1
                || item.value(QStringLiteral("processExitCode")).toInt(-1) != 0
                || !item.value(QStringLiteral("errors")).toArray().isEmpty()) {
                *error = QStringLiteral("前序严格准入事件失败：%1 %2")
                    .arg(item.value(QStringLiteral("symbol")).toString(),
                         item.value(QStringLiteral("targetDate")).toString());
                return false;
            }
            symbols->insert(item.value(QStringLiteral("symbol")).toString());
            const QString eventKey = item.value(QStringLiteral("symbol")).toString()
                + QLatin1Char('|')
                + item.value(QStringLiteral("targetDate")).toString()
                + QLatin1Char('|')
                + item.value(QStringLiteral("strategyId")).toString();
            eligibleEntries->insert(eventKey);
            ++*caseCount;
        }
    }
    // 最后一份摘要应在所有增量导入后生成，防止回测输入被换掉。
    QFile latest(paths.last());
    if (!latest.open(QIODevice::ReadOnly)
        || QJsonDocument::fromJson(latest.readAll()).object()
               .value(QStringLiteral("databaseSha256After")).toString()
            != databaseHash) {
        *error = QStringLiteral("隔离数据库与最后一份严格准入摘要哈希不一致");
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {QStringLiteral("existing-db"), QStringLiteral("只读原始 Tick 隔离库"),
         QStringLiteral("path")},
        {QStringLiteral("validated-summaries"),
         QStringLiteral("按导入顺序列出此前严格准入的 JSON 摘要，逗号分隔"),
         QStringLiteral("paths")},
        {QStringLiteral("output"), QStringLiteral("研究 JSON 输出"),
         QStringLiteral("path")},
        {QStringLiteral("initial-capital"), QStringLiteral("唯一共享资金仓初始现金"),
         QStringLiteral("amount"), QStringLiteral("1000000")},
        {QStringLiteral("maximum-holdings"), QStringLiteral("最多同时持有的股票数"),
         QStringLiteral("count"), QStringLiteral("20")},
    });
    parser.process(application);
    const QString databasePath = parser.value(QStringLiteral("existing-db"));
    const QString outputPath = parser.value(QStringLiteral("output"));
    QStringList summaryPaths;
    for (const QString &path : parser.value(QStringLiteral("validated-summaries"))
                                   .split(QLatin1Char(','))) {
        if (!path.trimmed().isEmpty()) summaryPaths.append(path.trimmed());
    }
    bool capitalOk = false;
    bool holdingsOk = false;
    pp::BoardPortfolioConfig config;
    config.initialCapital = parser.value(QStringLiteral("initial-capital"))
                                .toDouble(&capitalOk);
    config.maximumHoldingSymbols = parser.value(QStringLiteral("maximum-holdings"))
                                       .toInt(&holdingsOk);
    if (!QFileInfo::exists(databasePath) || outputPath.isEmpty()
        || !capitalOk || config.initialCapital <= 0.0 || !holdingsOk
        || config.maximumHoldingSymbols <= 0) {
        QTextStream(stderr) << QStringLiteral("共享资金回测参数无效") << Qt::endl;
        return 1;
    }
    const QString databaseHash = sha256File(databasePath);
    QSet<QString> validatedSymbols;
    int validatedCalls = 0;
    QString error;
    if (!validatePriorRuns(summaryPaths, databaseHash, &validatedSymbols,
                           &config.eligibleEntries, &validatedCalls, &error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 2;
    }

    pp::BoardPortfolioBacktestEngine engine(config);
    int resultCode = 0;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("board_portfolio_readonly"));
        database.setDatabaseName(databasePath);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!database.open()) {
            QTextStream(stderr) << database.lastError().text() << Qt::endl;
            return 3;
        }
        {
            QSqlQuery integrity(database);
            if (!integrity.exec(QStringLiteral("PRAGMA integrity_check"))
                || !integrity.next() || integrity.value(0).toString() != QStringLiteral("ok")) {
                QTextStream(stderr) << QStringLiteral("隔离库完整性校验失败") << Qt::endl;
                resultCode = 4;
            }
        }
        if (resultCode == 0) {
            QSqlQuery query(database);
            query.setForwardOnly(true);
            if (!query.exec(QStringLiteral(
                    "select symbol, name, venue, trading_date, timestamp_utc, "
                    "source_line, price, volume_shares, amount, direction, source "
                    "from raw_trade_ticks "
                    "order by timestamp_utc, symbol, source_line, id"))) {
                QTextStream(stderr) << query.lastError().text() << Qt::endl;
                resultCode = 5;
            } else {
                while (query.next()) {
                    pp::RawTradeTick tick;
                    tick.instrument.symbol = query.value(0).toString();
                    tick.instrument.name = query.value(1).toString();
                    tick.instrument.venue = query.value(2).toString();
                    tick.instrument.market = pp::Market::AShare;
                    tick.tradingDate = QDate::fromString(query.value(3).toString(),
                                                         Qt::ISODate);
                    tick.timestamp = QDateTime::fromString(query.value(4).toString(),
                                                            Qt::ISODateWithMs);
                    tick.sourceLine = query.value(5).toLongLong();
                    tick.price = query.value(6).toDouble();
                    tick.volumeShares = query.value(7).toDouble();
                    tick.amount = query.value(8).toDouble();
                    tick.direction = static_cast<pp::RawTradeDirection>(
                        query.value(9).toInt());
                    tick.source = query.value(10).toString();
                    if (!validatedSymbols.contains(tick.instrument.symbol)
                        || !engine.accept(tick, &error)) {
                        QTextStream(stderr) << QStringLiteral("未验证的标的或输入错误：")
                                            << tick.instrument.symbol << QLatin1Char(' ')
                                            << error << Qt::endl;
                        resultCode = 6;
                        break;
                    }
                }
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("board_portfolio_readonly"));
    if (resultCode != 0) return resultCode;

    const pp::BoardPortfolioReport report = engine.finish();
    if (report.observedSymbols != validatedSymbols.size()) {
        QTextStream(stderr) << QStringLiteral("验证事件与数据库标的集合不一致")
                            << Qt::endl;
        return 7;
    }
    QJsonArray trades;
    for (const pp::PortfolioTrade &trade : report.trades) {
        trades.append(tradeObject(trade));
    }
    QJsonArray holdings;
    for (const pp::PortfolioPosition &position : report.openPositions) {
        holdings.append(QJsonObject{
            {QStringLiteral("symbol"), position.instrument.symbol},
            {QStringLiteral("quantity"), position.quantity},
            {QStringLiteral("lastPrice"), position.lastPrice},
            {QStringLiteral("marketValue"), position.marketValue},
            {QStringLiteral("weight"), position.weight},
            {QStringLiteral("priceStale"), position.priceStale},
            {QStringLiteral("marketStatus"), position.marketStatus}});
    }
    QJsonObject output{
        {QStringLiteral("engine"),
         QStringLiteral("PPTrader BoardPortfolioBacktestEngine / one cash book")},
        {QStringLiteral("scope"), QStringLiteral("validated sparse research sample")},
        {QStringLiteral("databaseSha256"), databaseHash},
        {QStringLiteral("validatedCallsIncludingDuplicates"), validatedCalls},
        {QStringLiteral("validatedUniqueEvents"), config.eligibleEntries.size()},
        {QStringLiteral("observedSymbols"), report.observedSymbols},
        {QStringLiteral("observations"), double(report.observations)},
        {QStringLiteral("firstObservationUtc"),
         report.firstObservation.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("lastObservationUtc"),
         report.lastObservation.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("initialCapital"), report.initialCapital},
        {QStringLiteral("cash"), report.cash},
        {QStringLiteral("equityMarkToLastTick"), report.equity},
        {QStringLiteral("peakEquity"), report.peakEquity},
        {QStringLiteral("maximumDrawdownPercent"), report.maximumDrawdownPercent},
        {QStringLiteral("maximumHoldingSymbols"), config.maximumHoldingSymbols},
        {QStringLiteral("maximumObservedHoldingSymbols"),
         report.maximumObservedHoldingSymbols},
        {QStringLiteral("staleOpenPositions"), report.staleOpenPositions},
        {QStringLiteral("buySignals"), report.buySignals},
        {QStringLiteral("sellSignals"), report.sellSignals},
        {QStringLiteral("buys"), report.buys},
        {QStringLiteral("sells"), report.sells},
        {QStringLiteral("rejectedBuys"), report.rejectedBuys},
        {QStringLiteral("rejectedSells"), report.rejectedSells},
        {QStringLiteral("rejectedForCash"), report.rejectedForCash},
        {QStringLiteral("rejectedForHoldingLimit"), report.rejectedForHoldingLimit},
        {QStringLiteral("rejectedForPositionLimit"), report.rejectedForPositionLimit},
        {QStringLiteral("ignoredOutOfScopeSignals"),
         report.ignoredOutOfScopeSignals},
        {QStringLiteral("entryDayOutcomeKnownSignals"),
         report.entryDayOutcomeKnownSignals},
        {QStringLiteral("entryDayClosedBoardSignals"),
         report.entryDayClosedBoardSignals},
        {QStringLiteral("openPositions"), holdings},
        {QStringLiteral("trades"), trades},
        {QStringLiteral("archiveWritten"), false},
    };
    const QByteArray payload = QJsonDocument(output).toJson(QJsonDocument::Indented);
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(payload) != payload.size()) {
        QTextStream(stderr) << QStringLiteral("组合报告写入失败") << Qt::endl;
        return 8;
    }
    QTextStream(stdout) << QString::fromUtf8(payload) << Qt::endl;
    return 0;
}
