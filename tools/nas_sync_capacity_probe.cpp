#include "services/NasDataNodeClient.h"
#include "services/SqliteLocalDataStore.h"
#include "services/TickDataCsvImporter.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <memory>

namespace {

class NasSyncCapacityRunner final : public QObject {
    Q_OBJECT

public:
    NasSyncCapacityRunner(QUrl endpoint, QStringList symbols, QVector<int> tiers,
                          QDate tradingDate, QString outputPath, QObject *parent = nullptr)
        : QObject(parent)
        , m_endpoint(std::move(endpoint))
        , m_symbols(std::move(symbols))
        , m_tiers(std::move(tiers))
        , m_tradingDate(tradingDate)
        , m_outputPath(std::move(outputPath))
    {
        connect(&m_client, &pp::NasDataNodeClient::catalogReady,
                this, &NasSyncCapacityRunner::handleManifest);
        connect(&m_client, &pp::NasDataNodeClient::downloadReady,
                this, &NasSyncCapacityRunner::handleDownload);
    }

    bool start(QString *error)
    {
        if (!m_temporaryRoot.isValid()) {
            if (error) *error = QStringLiteral("无法创建容量探针临时目录");
            return false;
        }
        if (!m_client.setEndpoint(m_endpoint, error)) return false;
        QTimer::singleShot(0, this, &NasSyncCapacityRunner::startNextTier);
        return true;
    }

signals:
    void finished(int exitCode);

private:
    void startNextTier()
    {
        if (++m_tierIndex >= m_tiers.size()) {
            QJsonObject report;
            report.insert(QStringLiteral("date"), m_tradingDate.toString(Qt::ISODate));
            report.insert(QStringLiteral("endpoint"), QStringLiteral("loopback-tunnel"));
            report.insert(QStringLiteral("results"), m_results);
            QFile output(m_outputPath);
            if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)
                || output.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) < 0) {
                emit finished(2);
                return;
            }
            emit finished(0);
            return;
        }
        m_requestedSymbols = qMin(m_tiers.at(m_tierIndex), m_symbols.size());
        m_entries.clear();
        m_entryIndex = -1;
        m_failures = 0;
        m_ticks = 0;
        m_bars = 0;
        m_transferMilliseconds = 0;
        m_parseMilliseconds = 0;
        m_rawDatabaseMilliseconds = 0;
        m_candleDatabaseMilliseconds = 0;
        m_runTimer.restart();
        m_manifestTimer.restart();
        const QStringList selected = m_symbols.mid(0, m_requestedSymbols);
        const quint64 requestId = ++m_requestId;
        m_activeRequestId = requestId;
        if (!m_client.requestManifest(requestId, selected, m_tradingDate, m_tradingDate)) {
            emit finished(3);
        }
    }

    void handleManifest(quint64 requestId, const pp::NasCatalogResult &result)
    {
        if (requestId != m_activeRequestId) return;
        m_manifestMilliseconds = m_manifestTimer.elapsed();
        if (!result.succeeded) {
            emit finished(4);
            return;
        }
        m_manifestAvailableSymbols = result.availableSymbols;
        m_manifestMissingSymbols = result.missingSymbols;
        m_sourceBytes = result.totalBytes;
        m_entries = result.entries;
        const QString tierRoot = QDir(m_temporaryRoot.path()).filePath(
            QStringLiteral("tier-%1").arg(m_requestedSymbols));
        QDir().mkpath(tierRoot);
        m_stagingRoot = QDir(tierRoot).filePath(QStringLiteral("staging"));
        QDir().mkpath(m_stagingRoot);
        m_databasePath = QDir(tierRoot).filePath(QStringLiteral("capacity.db"));
        m_store = std::make_unique<pp::SqliteLocalDataStore>();
        if (!m_store->initialize(m_databasePath)) {
            emit finished(5);
            return;
        }
        startNextDownload();
    }

    void startNextDownload()
    {
        if (++m_entryIndex >= m_entries.size()) {
            finishTier();
            return;
        }
        m_transferTimer.restart();
        const quint64 requestId = ++m_requestId;
        m_activeRequestId = requestId;
        if (!m_client.downloadTickFile(
                requestId, m_entries.at(m_entryIndex), m_stagingRoot)) {
            ++m_failures;
            QTimer::singleShot(0, this, &NasSyncCapacityRunner::startNextDownload);
        }
    }

    void handleDownload(quint64 requestId, const pp::NasDownloadResult &download)
    {
        if (requestId != m_activeRequestId) return;
        m_transferMilliseconds += m_transferTimer.elapsed();
        if (!download.succeeded) {
            ++m_failures;
            QTimer::singleShot(0, this, &NasSyncCapacityRunner::startNextDownload);
            return;
        }
        pp::Instrument instrument;
        instrument.symbol = download.entry.symbol;
        instrument.name = download.entry.symbol;
        instrument.market = pp::Market::AShare;
        instrument.venue = instrument.symbol.startsWith(QLatin1Char('6'))
            ? QStringLiteral("XSHG") : QStringLiteral("XSHE");
        pp::TickDataImportRequest request;
        request.rootDirectory = download.localRoot;
        request.instrument = instrument;
        request.tradingDate = download.entry.tradingDate;
        request.format = pp::TickDataCsvFormat::Tick;
        request.periodSeconds = 60;

        qint64 rawDatabaseNanoseconds = 0;
        qint64 rawSinkNanoseconds = 0;
        QElapsedTimer databaseTimer;
        databaseTimer.start();
        const bool transactionReady = m_store->beginRawTradeTickImport(
            instrument, request.tradingDate, QStringLiteral("tickdata_csv_tick"));
        rawDatabaseNanoseconds += databaseTimer.nsecsElapsed();
        const auto sink = [this, transactionReady, &rawDatabaseNanoseconds,
                           &rawSinkNanoseconds](const QVector<pp::RawTradeTick> &ticks) {
            QElapsedTimer timer;
            timer.start();
            const bool succeeded = transactionReady && m_store->appendRawTradeTicks(ticks);
            const qint64 elapsed = timer.nsecsElapsed();
            rawDatabaseNanoseconds += elapsed;
            rawSinkNanoseconds += elapsed;
            return succeeded;
        };
        QElapsedTimer parseTimer;
        parseTimer.start();
        pp::TickDataImportResult imported = pp::TickDataCsvImporter::parse(request, {}, sink);
        const qint64 parseAndSinkNanoseconds = parseTimer.nsecsElapsed();
        bool committed = false;
        if (transactionReady && imported.succeeded) {
            databaseTimer.restart();
            committed = m_store->commitRawTradeTickImport();
            rawDatabaseNanoseconds += databaseTimer.nsecsElapsed();
        } else if (transactionReady) {
            m_store->rollbackRawTradeTickImport();
        }
        if (!committed) {
            ++m_failures;
        } else {
            databaseTimer.restart();
            const bool candlesStored = m_store->upsertCandles(imported.candles);
            m_candleDatabaseMilliseconds += databaseTimer.elapsed();
            if (!candlesStored) {
                ++m_failures;
            } else {
                m_ticks += imported.validRecords;
                m_bars += imported.candles.size();
            }
        }
        m_parseMilliseconds += qRound64(static_cast<double>(qMax<qint64>(
            0, parseAndSinkNanoseconds - rawSinkNanoseconds)) / 1000000.0);
        m_rawDatabaseMilliseconds += qRound64(
            static_cast<double>(rawDatabaseNanoseconds) / 1000000.0);
        QTimer::singleShot(0, this, &NasSyncCapacityRunner::startNextDownload);
    }

    void finishTier()
    {
        const qint64 elapsed = m_runTimer.elapsed();
        const qint64 databaseBytes = QFileInfo(m_databasePath).size()
            + QFileInfo(m_databasePath + QStringLiteral("-wal")).size()
            + QFileInfo(m_databasePath + QStringLiteral("-shm")).size();
        QJsonObject result;
        result.insert(QStringLiteral("requestedSymbols"), m_requestedSymbols);
        result.insert(QStringLiteral("availableSymbols"), m_manifestAvailableSymbols);
        result.insert(QStringLiteral("missingSymbols"), m_manifestMissingSymbols);
        result.insert(QStringLiteral("files"), m_entries.size());
        result.insert(QStringLiteral("failedFiles"), m_failures);
        result.insert(QStringLiteral("sourceBytes"), static_cast<double>(m_sourceBytes));
        result.insert(QStringLiteral("databaseBytes"), static_cast<double>(databaseBytes));
        result.insert(QStringLiteral("rawTicksWritten"), static_cast<double>(m_ticks));
        result.insert(QStringLiteral("candleBarsWritten"), static_cast<double>(m_bars));
        result.insert(QStringLiteral("manifestMilliseconds"), m_manifestMilliseconds);
        result.insert(QStringLiteral("transferMilliseconds"), m_transferMilliseconds);
        result.insert(QStringLiteral("parseMilliseconds"), m_parseMilliseconds);
        result.insert(QStringLiteral("rawDatabaseMilliseconds"), m_rawDatabaseMilliseconds);
        result.insert(QStringLiteral("candleDatabaseMilliseconds"), m_candleDatabaseMilliseconds);
        result.insert(QStringLiteral("elapsedMilliseconds"), elapsed);
        m_results.append(result);
        QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
        m_store.reset();
        QTimer::singleShot(0, this, &NasSyncCapacityRunner::startNextTier);
    }

    QUrl m_endpoint;
    QStringList m_symbols;
    QVector<int> m_tiers;
    QDate m_tradingDate;
    QString m_outputPath;
    QTemporaryDir m_temporaryRoot;
    pp::NasDataNodeClient m_client;
    std::unique_ptr<pp::SqliteLocalDataStore> m_store;
    QVector<pp::NasCatalogEntry> m_entries;
    QJsonArray m_results;
    int m_tierIndex = -1;
    int m_entryIndex = -1;
    int m_requestedSymbols = 0;
    int m_manifestAvailableSymbols = 0;
    int m_manifestMissingSymbols = 0;
    int m_failures = 0;
    quint64 m_requestId = 0;
    quint64 m_activeRequestId = 0;
    qint64 m_sourceBytes = 0;
    qint64 m_ticks = 0;
    qint64 m_bars = 0;
    qint64 m_manifestMilliseconds = 0;
    qint64 m_transferMilliseconds = 0;
    qint64 m_parseMilliseconds = 0;
    qint64 m_rawDatabaseMilliseconds = 0;
    qint64 m_candleDatabaseMilliseconds = 0;
    QString m_stagingRoot;
    QString m_databasePath;
    QElapsedTimer m_manifestTimer;
    QElapsedTimer m_transferTimer;
    QElapsedTimer m_runTimer;
};

QStringList loadSymbols(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法读取股票池缓存");
        return {};
    }
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    QStringList symbols;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        const QString market = object.value(QStringLiteral("market")).toString();
        const QString symbol = object.value(QStringLiteral("code")).toString();
        const QString prefix = symbol.left(3);
        const bool validShanghai = (prefix == QStringLiteral("600")
            || prefix == QStringLiteral("601") || prefix == QStringLiteral("603")
            || prefix == QStringLiteral("605") || prefix == QStringLiteral("688"))
            && market.contains(QStringLiteral("上海"));
        const bool validShenzhen = (prefix == QStringLiteral("000")
            || prefix == QStringLiteral("001") || prefix == QStringLiteral("002")
            || prefix == QStringLiteral("003") || prefix == QStringLiteral("300")
            || prefix == QStringLiteral("301"))
            && market.contains(QStringLiteral("深圳"));
        if (!validShanghai && !validShenzhen) continue;
        if (symbol.size() == 6 && !symbols.contains(symbol)) symbols.append(symbol);
    }
    if (symbols.isEmpty() && error) *error = QStringLiteral("股票池没有沪深 A 股代码");
    return symbols;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("e"), QStringLiteral("endpoint")}, QStringLiteral("回环 NAS 端点"), QStringLiteral("url")},
        {{QStringLiteral("u"), QStringLiteral("universe")}, QStringLiteral("A 股股票池 JSON"), QStringLiteral("path")},
        {{QStringLiteral("d"), QStringLiteral("date")}, QStringLiteral("交易日 yyyy-MM-dd"), QStringLiteral("date")},
        {{QStringLiteral("t"), QStringLiteral("tiers")}, QStringLiteral("逗号分隔档位"), QStringLiteral("counts"), QStringLiteral("10,100,500")},
        {{QStringLiteral("o"), QStringLiteral("output")}, QStringLiteral("结果 JSON"), QStringLiteral("path")},
    });
    parser.process(application);
    QString error;
    const QStringList symbols = loadSymbols(parser.value(QStringLiteral("universe")), &error);
    QVector<int> tiers;
    for (const QString &value : parser.value(QStringLiteral("tiers")).split(',')) {
        bool ok = false;
        const int tier = value.trimmed().toInt(&ok);
        if (ok && tier > 0) tiers.append(tier);
    }
    const QDate date = QDate::fromString(parser.value(QStringLiteral("date")), Qt::ISODate);
    const QUrl endpoint(parser.value(QStringLiteral("endpoint")));
    if (!error.isEmpty() || tiers.isEmpty() || !date.isValid() || endpoint.isEmpty()
        || parser.value(QStringLiteral("output")).isEmpty()) {
        QTextStream(stderr) << (error.isEmpty() ? QStringLiteral("容量探针参数无效") : error)
                            << Qt::endl;
        return 1;
    }
    NasSyncCapacityRunner runner(endpoint, symbols, tiers, date,
                                 parser.value(QStringLiteral("output")));
    QObject::connect(&runner, &NasSyncCapacityRunner::finished,
                     &application, [&application](int exitCode) { application.exit(exitCode); });
    if (!runner.start(&error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 1;
    }
    return application.exec();
}

#include "nas_sync_capacity_probe.moc"
