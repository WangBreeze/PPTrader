#include "services/SqliteLocalDataStore.h"

#include "services/BacktestDataVersion.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QStringList>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace pp {

namespace {

constexpr int kCurrentDatabaseSchemaVersion = 3;
const QString kApplicationSchemaVersion = QStringLiteral("0.1.0");

QString marketCode(Market market)
{
    switch (market) {
    case Market::AShare:
        return QStringLiteral("A股");
    case Market::USStock:
        return QStringLiteral("美股");
    case Market::Crypto:
        return QStringLiteral("加密货币");
    case Market::HongKong:
        return QStringLiteral("港股");
    }

    return QStringLiteral("未知");
}

Market marketFromCode(const QString &value, Market fallback = Market::AShare)
{
    if (value == QStringLiteral("A股") || value == QStringLiteral("0")) return Market::AShare;
    if (value == QStringLiteral("美股") || value == QStringLiteral("1")) return Market::USStock;
    if (value == QStringLiteral("加密货币") || value == QStringLiteral("2")) return Market::Crypto;
    if (value == QStringLiteral("港股") || value == QStringLiteral("3")) return Market::HongKong;
    return fallback;
}

Candle candleFromQuery(QSqlQuery &query, const Instrument &instrument, int periodSeconds)
{
    Candle candle;
    candle.instrument = instrument;
    candle.startTime = QDateTime::fromString(query.value(0).toString(), Qt::ISODateWithMs);
    candle.periodSeconds = periodSeconds;
    candle.open = query.value(1).toDouble();
    candle.high = query.value(2).toDouble();
    candle.low = query.value(3).toDouble();
    candle.close = query.value(4).toDouble();
    candle.volume = query.value(5).toDouble();
    candle.buyVolume = query.value(6).toDouble();
    candle.sellVolume = query.value(7).toDouble();
    candle.neutralVolume = query.value(8).toDouble();
    candle.directionalVolumeKnown = query.value(9).toBool();
    candle.source = query.value(10).toString();
    candle.adjustment = query.value(11).toString();
    candle.downloadedAt = QDateTime::fromString(query.value(12).toString(), Qt::ISODateWithMs);
    return candle;
}

QString compactJson(const QVariantMap &value)
{
    return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
}

QVariantMap variantMapFromJson(const QString &value)
{
    const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8());
    return document.isObject() ? document.object().toVariantMap() : QVariantMap{};
}

bool isSha256(const QString &value)
{
    const QString digest = value.startsWith(QStringLiteral("sha256:")) ? value.mid(7) : value;
    if (digest.size() != 64) return false;
    for (const QChar character : digest) {
        const ushort code = character.unicode();
        if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f')
              || (code >= 'A' && code <= 'F'))) return false;
    }
    return true;
}

} // 匿名命名空间

SqliteLocalDataStore::SqliteLocalDataStore(QObject *parent)
    : LocalDataStore(parent)
    , m_connectionName(QStringLiteral("pptrader_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

SqliteLocalDataStore::~SqliteLocalDataStore()
{
    close();
    m_database = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool SqliteLocalDataStore::initialize(const QString &databasePath)
{
    close();
    m_databasePath = databasePath;
    QDir().mkpath(QFileInfo(databasePath).absolutePath());

    if (!m_database.isValid())
        m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(databasePath);

    if (!m_database.open()) {
        emitSqlError(QStringLiteral("打开本地数据库"));
        return false;
    }

    const QStringList pragmas {
        QStringLiteral("pragma busy_timeout=5000"),
        QStringLiteral("pragma journal_mode=WAL"),
        QStringLiteral("pragma synchronous=NORMAL"),
        QStringLiteral("pragma foreign_keys=ON")
    };
    for (const QString &statement : pragmas) {
        QSqlQuery pragmaQuery(m_database);
        if (!pragmaQuery.exec(statement)) {
            emitSqlError(QStringLiteral("配置 SQLite 并发模式"));
            return false;
        }
    }

    return migrateSchema();
}

void SqliteLocalDataStore::close()
{
    if (!m_database.isValid() || !m_database.isOpen()) return;
    if (m_rawTradeImportActive) rollbackRawTradeTickImport();
    QSqlQuery checkpoint(m_database);
    checkpoint.exec(QStringLiteral("pragma wal_checkpoint(TRUNCATE)"));
    checkpoint.finish();
    m_database.close();
    m_schemaVersion = 0;
    m_databaseUuid.clear();
}

QString SqliteLocalDataStore::databasePath() const
{
    return m_databasePath;
}

int SqliteLocalDataStore::schemaVersion() const { return m_schemaVersion; }
QString SqliteLocalDataStore::databaseUuid() const { return m_databaseUuid; }
QString SqliteLocalDataStore::lastMigrationBackupPath() const
{
    return m_lastMigrationBackupPath;
}

bool SqliteLocalDataStore::hasUserSchema() const
{
    QSqlQuery query(m_database);
    return query.exec(QStringLiteral(
               "select 1 from sqlite_master where type = 'table' "
               "and name not like 'sqlite_%' limit 1")) && query.next();
}

bool SqliteLocalDataStore::createMigrationBackup(int fromVersion, QString *backupPath)
{
    if (backupPath) backupPath->clear();
    const QString suffix = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString path = QStringLiteral("%1.pre-v%2-%3.bak")
                             .arg(m_databasePath).arg(fromVersion).arg(suffix);
    QString escaped = QDir::toNativeSeparators(path);
    escaped.replace(QChar('\''), QStringLiteral("''"));
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("vacuum into '%1'").arg(escaped))) {
        const QString message = QStringLiteral("创建数据库迁移备份失败：%1")
                                    .arg(query.lastError().text());
        qWarning().noquote() << message;
        emit writeFailed(message);
        return false;
    }
    if (backupPath) *backupPath = path;
    return true;
}

bool SqliteLocalDataStore::verifyRequiredTables(QString *error) const
{
    if (error) error->clear();
    const QStringList required {
        QStringLiteral("ticks"), QStringLiteral("raw_trade_ticks"),
        QStringLiteral("candles"), QStringLiteral("market_snapshots"),
        QStringLiteral("historical_dataset_versions"),
        QStringLiteral("backtest_results"), QStringLiteral("schema_migration_history"),
        QStringLiteral("app_database_metadata")
    };
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select count(*) from sqlite_master where type = 'table' and name = ?"));
    for (const QString &table : required) {
        query.bindValue(0, table);
        if (!query.exec() || !query.next() || query.value(0).toInt() != 1) {
            if (error) *error = QStringLiteral("缺少必需业务表：%1").arg(table);
            return false;
        }
        query.finish();
    }
    return true;
}

bool SqliteLocalDataStore::verifyIntegrity(QString *error) const
{
    if (error) error->clear();
    QSqlQuery integrity(m_database);
    if (!integrity.exec(QStringLiteral("pragma integrity_check")) || !integrity.next()
        || integrity.value(0).toString().compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
        if (error) *error = integrity.isValid()
            ? QStringLiteral("SQLite 完整性检查失败：%1").arg(integrity.value(0).toString())
            : QStringLiteral("无法执行 SQLite 完整性检查");
        return false;
    }
    QSqlQuery foreignKeys(m_database);
    if (!foreignKeys.exec(QStringLiteral("pragma foreign_key_check"))) {
        if (error) *error = QStringLiteral("无法执行外键完整性检查");
        return false;
    }
    if (foreignKeys.next()) {
        if (error) *error = QStringLiteral("数据库存在外键完整性错误");
        return false;
    }
    return verifyRequiredTables(error);
}

bool SqliteLocalDataStore::migrateSchema()
{
    m_lastMigrationBackupPath.clear();
    QSqlQuery versionQuery(m_database);
    if (!versionQuery.exec(QStringLiteral("pragma user_version")) || !versionQuery.next()) {
        emitSqlError(QStringLiteral("读取全库结构版本"));
        return false;
    }
    const int fromVersion = versionQuery.value(0).toInt();
    versionQuery.finish();
    if (fromVersion > kCurrentDatabaseSchemaVersion) {
        emit writeFailed(QStringLiteral("数据库结构版本 %1 高于程序支持版本 %2")
                             .arg(fromVersion).arg(kCurrentDatabaseSchemaVersion));
        return false;
    }
    const bool needsMigration = fromVersion < kCurrentDatabaseSchemaVersion;
    if (needsMigration && hasUserSchema()
        && !createMigrationBackup(fromVersion, &m_lastMigrationBackupPath)) return false;

    if (!m_database.transaction()) {
        emitSqlError(QStringLiteral("开始数据库结构迁移事务"));
        return false;
    }
    if (!ensureSchema()) {
        m_database.rollback();
        return false;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QSqlQuery metadata(m_database);
    metadata.prepare(QStringLiteral(
        "insert into app_database_metadata(singleton_id, database_uuid, schema_version, "
        "created_at_utc, migrated_at_utc, application_version) values(1, ?, ?, ?, ?, ?) "
        "on conflict(singleton_id) do update set schema_version=excluded.schema_version, "
        "migrated_at_utc=excluded.migrated_at_utc, application_version=excluded.application_version"));
    metadata.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    metadata.addBindValue(kCurrentDatabaseSchemaVersion);
    metadata.addBindValue(now);
    metadata.addBindValue(now);
    metadata.addBindValue(kApplicationSchemaVersion);
    if (!metadata.exec()) {
        m_database.rollback();
        emitSqlError(QStringLiteral("写入数据库身份"));
        return false;
    }

    if (needsMigration) {
        QSqlQuery history(m_database);
        if (fromVersion < 1) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(1, 'global_schema_baseline', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-global-schema-v1"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v1 迁移历史"));
                return false;
            }
            history.finish();
        }
        if (fromVersion < 2) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(2, 'historical_dataset_identity', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-historical-dataset-v2"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v2 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 3) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(3, 'backtest_dataset_identity', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-backtest-dataset-v3"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v3 迁移历史"));
                return false;
            }
        }
        QSqlQuery setVersion(m_database);
        if (!setVersion.exec(QStringLiteral("pragma user_version=%1")
                                 .arg(kCurrentDatabaseSchemaVersion))) {
            m_database.rollback();
            emitSqlError(QStringLiteral("更新全库结构版本"));
            return false;
        }
    }

    QString validationError;
    if (!verifyIntegrity(&validationError)) {
        m_database.rollback();
        emit writeFailed(QStringLiteral("数据库迁移校验失败：%1").arg(validationError));
        return false;
    }
    if (!m_database.commit()) {
        emitSqlError(QStringLiteral("提交数据库结构迁移"));
        return false;
    }

    QSqlQuery identity(m_database);
    if (!identity.exec(QStringLiteral(
            "select database_uuid, schema_version from app_database_metadata where singleton_id = 1"))
        || !identity.next()) {
        emitSqlError(QStringLiteral("读取数据库身份"));
        return false;
    }
    m_databaseUuid = identity.value(0).toString();
    m_schemaVersion = identity.value(1).toInt();
    return true;
}

bool SqliteLocalDataStore::appendTick(const Tick &tick)
{
    return appendTicks({tick});
}

bool SqliteLocalDataStore::appendTicks(const QVector<Tick> &ticks)
{
    m_lastInsertedTickCount = 0;
    if (ticks.isEmpty()) return true;
    QString batchId = ticks.first().batchId;
    bool isSingleBatch = !batchId.isEmpty();
    for (const Tick &tick : ticks) {
        if (tick.batchId != batchId) {
            isSingleBatch = false;
            break;
        }
    }
    const auto batchTickCount = [this](const QString &id) {
        QSqlQuery countQuery(m_database);
        countQuery.prepare(QStringLiteral("select count(*) from ticks where batch_id = ?"));
        countQuery.addBindValue(id);
        return countQuery.exec() && countQuery.next() ? countQuery.value(0).toInt() : 0;
    };
    const int batchCountBefore = isSingleBatch ? batchTickCount(batchId) : 0;
    if (!m_database.transaction()) {
        emitSqlError(QStringLiteral("开始批量写入 tick"));
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert or ignore into ticks(symbol, name, market, venue, timestamp_utc, batch_id, price, "
        "previous_close, open, high, low, volume, amount, bid, ask) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const Tick &tick : ticks) {
        query.bindValue(0, tick.instrument.symbol);
        query.bindValue(1, tick.instrument.name.isNull()
            ? QStringLiteral("") : tick.instrument.name);
        query.bindValue(2, marketCode(tick.instrument.market));
        query.bindValue(3, tick.instrument.venue.isNull()
            ? QStringLiteral("") : tick.instrument.venue);
        query.bindValue(4, tick.timestamp.toUTC().toString(Qt::ISODateWithMs));
        query.bindValue(5, tick.batchId.isNull() ? QStringLiteral("") : tick.batchId);
        query.bindValue(6, tick.price);
        query.bindValue(7, tick.previousClose);
        query.bindValue(8, tick.open);
        query.bindValue(9, tick.high);
        query.bindValue(10, tick.low);
        query.bindValue(11, tick.volume);
        query.bindValue(12, tick.amount);
        query.bindValue(13, tick.bid);
        query.bindValue(14, tick.ask);
        if (!query.exec()) {
            m_database.rollback();
            emitSqlError(QStringLiteral("批量写入 tick"));
            return false;
        }
        query.finish();
    }
    if (!m_database.commit()) {
        emitSqlError(QStringLiteral("提交批量 tick"));
        return false;
    }
    m_lastInsertedTickCount = isSingleBatch
        ? std::max(0, batchTickCount(batchId) - batchCountBefore)
        : ticks.size();
    return true;
}

bool SqliteLocalDataStore::appendMarketSnapshot(const MarketSnapshot &snapshot)
{
    if (snapshot.batchId.trimmed().isEmpty() || !snapshot.timestamp.isValid()) {
        emit writeFailed(QStringLiteral("保存市场快照失败：批次号或快照时刻无效"));
        return false;
    }

    QVector<Tick> ticks;
    ticks.reserve(snapshot.quotes.size());
    for (const CrossSectionalQuote &quote : snapshot.quotes) {
        Tick tick;
        tick.instrument = quote.instrument;
        tick.timestamp = quote.timestamp.isValid() ? quote.timestamp : snapshot.timestamp;
        tick.batchId = snapshot.batchId;
        tick.price = quote.price;
        tick.previousClose = quote.previousClose;
        tick.open = quote.open;
        tick.high = quote.high;
        tick.low = quote.low;
        tick.volume = quote.volume;
        tick.amount = quote.amount;
        tick.bid = quote.bid;
        tick.ask = quote.ask;
        ticks.append(tick);
    }
    if (!appendTicks(ticks)) return false;

    const QDateTime completedAt = snapshot.completedAt.isValid()
        ? snapshot.completedAt : snapshot.timestamp;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into market_snapshots(batch_id, market, snapshot_time_utc, completed_at_utc, "
        "expected_count, received_count) values(?, ?, ?, ?, ?, ?) "
        "on conflict(batch_id) do update set market=excluded.market, "
        "snapshot_time_utc=excluded.snapshot_time_utc, completed_at_utc=excluded.completed_at_utc, "
        "expected_count=excluded.expected_count, received_count=excluded.received_count"));
    query.addBindValue(snapshot.batchId);
    query.addBindValue(static_cast<int>(snapshot.market));
    query.addBindValue(snapshot.timestamp.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(completedAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(std::max(0, snapshot.expectedCount));
    query.addBindValue(snapshot.quotes.size());
    if (!query.exec()) {
        emitSqlError(QStringLiteral("保存市场快照元数据"));
        return false;
    }
    return true;
}

std::optional<MarketSnapshot> SqliteLocalDataStore::loadMarketSnapshotAtOrBefore(
    const QDateTime &requestedTime)
{
    if (!requestedTime.isValid()) return std::nullopt;
    const QString requestedUtc = requestedTime.toUTC().toString(Qt::ISODateWithMs);
    QSqlQuery metadataQuery(m_database);
    metadataQuery.prepare(QStringLiteral(
        "select batch_id, market, snapshot_time_utc, completed_at_utc, expected_count "
        "from market_snapshots where snapshot_time_utc <= ? and completed_at_utc <= ? "
        "order by snapshot_time_utc desc, completed_at_utc desc limit 1"));
    metadataQuery.addBindValue(requestedUtc);
    metadataQuery.addBindValue(requestedUtc);
    if (!metadataQuery.exec()) {
        emitSqlError(QStringLiteral("查询最近已完成市场快照"));
        return std::nullopt;
    }
    if (!metadataQuery.next()) return std::nullopt;

    MarketSnapshot snapshot;
    snapshot.batchId = metadataQuery.value(0).toString();
    const int marketValue = metadataQuery.value(1).toInt();
    snapshot.market = marketValue >= static_cast<int>(Market::AShare)
            && marketValue <= static_cast<int>(Market::HongKong)
        ? static_cast<Market>(marketValue) : Market::AShare;
    snapshot.timestamp = QDateTime::fromString(metadataQuery.value(2).toString(),
                                               Qt::ISODateWithMs);
    snapshot.completedAt = QDateTime::fromString(metadataQuery.value(3).toString(),
                                                 Qt::ISODateWithMs);
    snapshot.expectedCount = metadataQuery.value(4).toInt();

    QSqlQuery tickQuery(m_database);
    tickQuery.prepare(QStringLiteral(
        "select symbol, name, market, venue, timestamp_utc, price, previous_close, open, high, "
        "low, volume, amount, bid, ask from ticks where batch_id = ? order by symbol asc"));
    tickQuery.addBindValue(snapshot.batchId);
    if (!tickQuery.exec()) {
        emitSqlError(QStringLiteral("读取市场快照明细"));
        return std::nullopt;
    }
    while (tickQuery.next()) {
        CrossSectionalQuote quote;
        quote.instrument.symbol = tickQuery.value(0).toString();
        quote.instrument.name = tickQuery.value(1).toString();
        quote.instrument.market = marketFromCode(tickQuery.value(2).toString(), snapshot.market);
        quote.instrument.venue = tickQuery.value(3).toString();
        quote.timestamp = QDateTime::fromString(tickQuery.value(4).toString(), Qt::ISODateWithMs);
        quote.price = tickQuery.value(5).toDouble();
        quote.previousClose = tickQuery.value(6).toDouble();
        quote.open = tickQuery.value(7).toDouble();
        quote.high = tickQuery.value(8).toDouble();
        quote.low = tickQuery.value(9).toDouble();
        quote.volume = tickQuery.value(10).toDouble();
        quote.amount = tickQuery.value(11).toDouble();
        quote.bid = tickQuery.value(12).toDouble();
        quote.ask = tickQuery.value(13).toDouble();
        snapshot.quotes.append(quote);
    }
    return snapshot;
}

int SqliteLocalDataStore::lastInsertedTickCount() const
{
    return m_lastInsertedTickCount;
}

bool SqliteLocalDataStore::appendCandle(const Candle &candle)
{
    return upsertCandles({candle});
}

bool SqliteLocalDataStore::upsertCandles(const QVector<Candle> &candles)
{
    if (candles.isEmpty()) {
        return true;
    }

    if (!m_database.transaction()) {
        emitSqlError(QStringLiteral("开始 K 线写入事务"));
        return false;
    }

    if (!writeCandlesInCurrentTransaction(candles)) {
        m_database.rollback();
        return false;
    }

    if (!m_database.commit()) {
        emitSqlError(QStringLiteral("提交 K 线写入事务"));
        return false;
    }
    return true;
}

bool SqliteLocalDataStore::writeCandlesInCurrentTransaction(
    const QVector<Candle> &candles)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into candles(symbol, name, market, venue, start_time_utc, period_seconds, open, high, low, close, volume, "
        "buy_volume, sell_volume, neutral_volume, directional_volume_known, source, adjustment, downloaded_at_utc) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "on conflict(symbol, period_seconds, start_time_utc) do update set "
        "name=excluded.name, market=excluded.market, venue=excluded.venue, open=excluded.open, high=excluded.high, "
        "low=excluded.low, close=excluded.close, volume=excluded.volume, buy_volume=excluded.buy_volume, "
        "sell_volume=excluded.sell_volume, neutral_volume=excluded.neutral_volume, "
        "directional_volume_known=excluded.directional_volume_known, source=excluded.source, "
        "adjustment=excluded.adjustment, downloaded_at_utc=excluded.downloaded_at_utc"));

    for (const Candle &candle : candles) {
        query.bindValue(0, candle.instrument.symbol);
        query.bindValue(1, candle.instrument.name);
        query.bindValue(2, marketCode(candle.instrument.market));
        query.bindValue(3, candle.instrument.venue);
        query.bindValue(4, candle.startTime.toUTC().toString(Qt::ISODateWithMs));
        query.bindValue(5, candle.periodSeconds);
        query.bindValue(6, candle.open);
        query.bindValue(7, candle.high);
        query.bindValue(8, candle.low);
        query.bindValue(9, candle.close);
        query.bindValue(10, candle.volume);
        query.bindValue(11, candle.buyVolume);
        query.bindValue(12, candle.sellVolume);
        query.bindValue(13, candle.neutralVolume);
        query.bindValue(14, candle.directionalVolumeKnown ? 1 : 0);
        query.bindValue(15, candle.source.isEmpty() ? QStringLiteral("unknown") : candle.source);
        query.bindValue(16, candle.adjustment.isEmpty() ? QStringLiteral("none") : candle.adjustment);
        const QDateTime downloadedAt = candle.downloadedAt.isValid()
            ? candle.downloadedAt : QDateTime::currentDateTimeUtc();
        query.bindValue(17, downloadedAt.toUTC().toString(Qt::ISODateWithMs));

        if (!query.exec()) {
            emitSqlError(QStringLiteral("批量写入 K 线"));
            return false;
        }
        query.finish();
    }

    return true;
}

bool SqliteLocalDataStore::upsertHistoricalDataset(
    const QVector<Candle> &candles, int requestedBars,
    const QString &dataVersion)
{
    if (candles.isEmpty() || requestedBars <= 0
        || !dataVersion.startsWith(QStringLiteral("sha256:"))) return false;
    const Candle &first = candles.first();
    const QString source = first.source.isEmpty() ? QStringLiteral("unknown")
                                                   : first.source;
    const QString adjustment = first.adjustment.isEmpty() ? QStringLiteral("none")
                                                           : first.adjustment;
    for (const Candle &candle : candles) {
        if (candle.instrument.symbol != first.instrument.symbol
            || candle.periodSeconds != first.periodSeconds
            || (candle.source.isEmpty() ? QStringLiteral("unknown") : candle.source) != source
            || (candle.adjustment.isEmpty() ? QStringLiteral("none") : candle.adjustment)
                != adjustment) return false;
    }
    if (!m_database.transaction()) {
        emitSqlError(QStringLiteral("开始历史数据集写入事务"));
        return false;
    }
    if (!writeCandlesInCurrentTransaction(candles)) {
        m_database.rollback();
        return false;
    }
    const auto timeOrder = [](const Candle &left, const Candle &right) {
        return left.startTime < right.startTime;
    };
    const auto [minimum, maximum] = std::minmax_element(
        candles.cbegin(), candles.cend(), timeOrder);
    QDateTime downloadedAt;
    for (const Candle &candle : candles) {
        if (candle.downloadedAt > downloadedAt) downloadedAt = candle.downloadedAt;
    }
    if (!downloadedAt.isValid()) downloadedAt = QDateTime::currentDateTimeUtc();
    QSqlQuery metadata(m_database);
    metadata.prepare(QStringLiteral(
        "insert into historical_dataset_versions(symbol, period_seconds, source, adjustment, "
        "requested_bars, actual_bars, first_time_utc, last_time_utc, data_version, downloaded_at_utc) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "on conflict(symbol, period_seconds, source, adjustment) do update set "
        "requested_bars=excluded.requested_bars, actual_bars=excluded.actual_bars, "
        "first_time_utc=excluded.first_time_utc, last_time_utc=excluded.last_time_utc, "
        "data_version=excluded.data_version, downloaded_at_utc=excluded.downloaded_at_utc"));
    metadata.addBindValue(first.instrument.symbol);
    metadata.addBindValue(first.periodSeconds);
    metadata.addBindValue(source);
    metadata.addBindValue(adjustment);
    metadata.addBindValue(requestedBars);
    metadata.addBindValue(candles.size());
    metadata.addBindValue(minimum->startTime.toUTC().toString(Qt::ISODateWithMs));
    metadata.addBindValue(maximum->startTime.toUTC().toString(Qt::ISODateWithMs));
    metadata.addBindValue(dataVersion);
    metadata.addBindValue(downloadedAt.toUTC().toString(Qt::ISODateWithMs));
    if (!metadata.exec()) {
        m_database.rollback();
        emitSqlError(QStringLiteral("记录历史数据集身份"));
        return false;
    }
    if (!m_database.commit()) {
        emitSqlError(QStringLiteral("提交历史数据集事务"));
        return false;
    }
    return true;
}

bool SqliteLocalDataStore::beginRawTradeTickImport(
    const Instrument &instrument, const QDate &tradingDate, const QString &source)
{
    if (m_rawTradeImportActive || instrument.symbol.isEmpty()
        || !tradingDate.isValid() || source.isEmpty()) return false;
    if (!m_database.transaction()) {
        emitSqlError(QStringLiteral("开始原始逐笔导入事务"));
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "delete from raw_trade_ticks where symbol = ? and trading_date = ? and source = ?"));
    query.addBindValue(instrument.symbol);
    query.addBindValue(tradingDate.toString(Qt::ISODate));
    query.addBindValue(source);
    if (!query.exec()) {
        m_database.rollback();
        emitSqlError(QStringLiteral("替换原始逐笔数据"));
        return false;
    }
    m_rawTradeImportActive = true;
    return true;
}

bool SqliteLocalDataStore::appendRawTradeTicks(const QVector<RawTradeTick> &ticks)
{
    if (!m_rawTradeImportActive || ticks.isEmpty()) return m_rawTradeImportActive;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into raw_trade_ticks(symbol, name, market, venue, trading_date, timestamp_utc, "
        "source_line, price, volume_shares, amount, direction, source, imported_at_utc) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    const QString importedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const RawTradeTick &tick : ticks) {
        query.bindValue(0, tick.instrument.symbol);
        query.bindValue(1, tick.instrument.name);
        query.bindValue(2, marketCode(tick.instrument.market));
        query.bindValue(3, tick.instrument.venue);
        query.bindValue(4, tick.tradingDate.toString(Qt::ISODate));
        query.bindValue(5, tick.timestamp.toUTC().toString(Qt::ISODateWithMs));
        query.bindValue(6, tick.sourceLine);
        query.bindValue(7, tick.price);
        query.bindValue(8, tick.volumeShares);
        query.bindValue(9, tick.amount);
        query.bindValue(10, static_cast<int>(tick.direction));
        query.bindValue(11, tick.source);
        query.bindValue(12, importedAt);
        if (!query.exec()) {
            emitSqlError(QStringLiteral("流式写入原始逐笔数据"));
            return false;
        }
        query.finish();
    }
    return true;
}

RawTradeIncrementalAppendResult SqliteLocalDataStore::appendRawTradeTicksIncremental(
    const QVector<RawTradeTick> &ticks)
{
    RawTradeIncrementalAppendResult result;
    result.receivedCount = ticks.size();
    if (m_rawTradeImportActive) {
        result.error = QStringLiteral("批量替换导入期间不能追加盘中逐笔数据");
        return result;
    }
    if (ticks.isEmpty()) {
        result.error = QStringLiteral("盘中逐笔增量批次为空");
        return result;
    }

    static const QTimeZone shanghai("Asia/Shanghai");
    const RawTradeTick &first = ticks.first();
    result.instrument = first.instrument;
    result.tradingDate = first.tradingDate;
    result.source = first.source.trimmed();
    if (result.instrument.symbol.trimmed().isEmpty() || !result.tradingDate.isValid()
        || result.source.isEmpty()) {
        result.error = QStringLiteral("盘中逐笔批次缺少品种、交易日或来源");
        return result;
    }
    for (const RawTradeTick &tick : ticks) {
        const bool sameIdentity = tick.instrument.symbol == result.instrument.symbol
            && tick.tradingDate == result.tradingDate
            && tick.source.trimmed() == result.source;
        const bool validValue = tick.timestamp.isValid() && tick.sourceLine > 0
            && std::isfinite(tick.price) && tick.price > 0.0
            && std::isfinite(tick.volumeShares) && tick.volumeShares >= 0.0
            && std::isfinite(tick.amount) && tick.amount >= 0.0
            && tick.timestamp.toTimeZone(shanghai).date() == result.tradingDate;
        if (!sameIdentity || !validValue) {
            result.error = QStringLiteral("盘中逐笔批次身份不一致或包含无效字段");
            return result;
        }
    }

    result.invalidatedPeriods = rawTickCachedPeriods(result.instrument, result.tradingDate);
    if (!m_database.transaction()) {
        result.error = QStringLiteral("无法开始盘中逐笔追加事务");
        emitSqlError(result.error);
        return result;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert or ignore into raw_trade_ticks(symbol, name, market, venue, trading_date, "
        "timestamp_utc, source_line, price, volume_shares, amount, direction, source, "
        "imported_at_utc) values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    const QString importedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const RawTradeTick &tick : ticks) {
        query.bindValue(0, tick.instrument.symbol);
        query.bindValue(1, tick.instrument.name);
        query.bindValue(2, marketCode(tick.instrument.market));
        query.bindValue(3, tick.instrument.venue);
        query.bindValue(4, tick.tradingDate.toString(Qt::ISODate));
        query.bindValue(5, tick.timestamp.toUTC().toString(Qt::ISODateWithMs));
        query.bindValue(6, tick.sourceLine);
        query.bindValue(7, tick.price);
        query.bindValue(8, tick.volumeShares);
        query.bindValue(9, tick.amount);
        query.bindValue(10, static_cast<int>(tick.direction));
        query.bindValue(11, result.source);
        query.bindValue(12, importedAt);
        if (!query.exec()) {
            m_database.rollback();
            result.error = QStringLiteral("盘中逐笔增量写入失败，事务已回滚");
            emitSqlError(result.error);
            return result;
        }
        result.insertedCount += query.numRowsAffected();
        query.finish();
    }
    if (!m_database.commit()) {
        result.error = QStringLiteral("盘中逐笔增量提交失败");
        emitSqlError(result.error);
        return result;
    }
    result.factCommitted = true;
    result.duplicateCount = result.receivedCount - result.insertedCount;
    if (result.insertedCount == 0) result.invalidatedPeriods.clear();
    return result;
}

bool SqliteLocalDataStore::commitRawTradeTickImport()
{
    if (!m_rawTradeImportActive) return false;
    const bool succeeded = m_database.commit();
    m_rawTradeImportActive = false;
    if (!succeeded) emitSqlError(QStringLiteral("提交原始逐笔导入事务"));
    return succeeded;
}

void SqliteLocalDataStore::rollbackRawTradeTickImport()
{
    if (!m_rawTradeImportActive) return;
    m_database.rollback();
    m_rawTradeImportActive = false;
}

QVector<QDate> SqliteLocalDataStore::rawTradeTradingDates(const Instrument &instrument)
{
    QVector<QDate> dates;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select distinct trading_date from raw_trade_ticks where symbol = ? "
        "order by trading_date asc"));
    query.addBindValue(instrument.symbol);
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取原始逐笔交易日"));
        return dates;
    }
    while (query.next()) {
        const QDate date = QDate::fromString(query.value(0).toString(), Qt::ISODate);
        if (date.isValid()) dates.append(date);
    }
    return dates;
}

RawTradeFingerprint SqliteLocalDataStore::rawTradeFingerprint(
    const Instrument &instrument, const QDate &tradingDate)
{
    RawTradeFingerprint fingerprint;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select count(*), coalesce(max(id), 0) from raw_trade_ticks "
        "where symbol = ? and trading_date = ?"));
    query.addBindValue(instrument.symbol);
    query.addBindValue(tradingDate.toString(Qt::ISODate));
    if (!query.exec() || !query.next()) {
        emitSqlError(QStringLiteral("读取原始逐笔指纹"));
        return fingerprint;
    }
    fingerprint.count = query.value(0).toLongLong();
    fingerprint.maximumId = query.value(1).toLongLong();
    return fingerprint;
}

QVector<int> SqliteLocalDataStore::rawTickCachedPeriods(
    const Instrument &instrument, const QDate &tradingDate)
{
    QVector<int> periods;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select period_seconds from raw_tick_candle_cache_state "
        "where symbol = ? and trading_date = ? order by period_seconds asc"));
    query.addBindValue(instrument.symbol);
    query.addBindValue(tradingDate.toString(Qt::ISODate));
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取逐笔 K 线缓存周期"));
        return periods;
    }
    while (query.next()) periods.append(query.value(0).toInt());
    return periods;
}

bool SqliteLocalDataStore::forEachRawTradeTick(
    const Instrument &instrument, const QDate &tradingDate,
    const std::function<bool(const RawTradeTick &)> &visitor)
{
    if (!visitor) return false;
    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    query.prepare(QStringLiteral(
        "select timestamp_utc, source_line, price, volume_shares, amount, direction, source, "
        "name, market, venue from raw_trade_ticks where symbol = ? and trading_date = ? "
        "order by timestamp_utc asc, source_line asc"));
    query.addBindValue(instrument.symbol);
    query.addBindValue(tradingDate.toString(Qt::ISODate));
    if (!query.exec()) {
        emitSqlError(QStringLiteral("流式读取原始逐笔数据"));
        return false;
    }
    while (query.next()) {
        RawTradeTick tick;
        tick.instrument = instrument;
        tick.instrument.name = query.value(7).toString();
        tick.instrument.market = marketFromCode(query.value(8).toString(), instrument.market);
        tick.instrument.venue = query.value(9).toString();
        tick.timestamp = QDateTime::fromString(query.value(0).toString(), Qt::ISODateWithMs);
        tick.tradingDate = tradingDate;
        tick.sourceLine = query.value(1).toLongLong();
        tick.price = query.value(2).toDouble();
        tick.volumeShares = query.value(3).toDouble();
        tick.amount = query.value(4).toDouble();
        tick.direction = static_cast<RawTradeDirection>(query.value(5).toInt());
        tick.source = query.value(6).toString();
        if (!visitor(tick)) return false;
    }
    return true;
}

bool SqliteLocalDataStore::rawTickCandleCacheFresh(
    const Instrument &instrument, const QDate &tradingDate, int periodSeconds)
{
    const RawTradeFingerprint fingerprint = rawTradeFingerprint(instrument, tradingDate);
    if (fingerprint.count <= 0) return false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select raw_count, raw_max_id, cache_version from raw_tick_candle_cache_state "
        "where symbol = ? and trading_date = ? and period_seconds = ?"));
    query.addBindValue(instrument.symbol);
    query.addBindValue(tradingDate.toString(Qt::ISODate));
    query.addBindValue(periodSeconds);
    return query.exec() && query.next()
        && query.value(0).toLongLong() == fingerprint.count
        && query.value(1).toLongLong() == fingerprint.maximumId
        && query.value(2).toInt() == 1;
}

bool SqliteLocalDataStore::replaceRawTickCandleCache(
    const Instrument &instrument, const QDate &tradingDate, int periodSeconds,
    const RawTradeFingerprint &fingerprint, const QVector<Candle> &candles)
{
    static const QTimeZone shanghai("Asia/Shanghai");
    const QDateTime from(tradingDate, QTime(0, 0), shanghai);
    const QDateTime to = from.addDays(1);
    if (!m_database.transaction()) {
        emitSqlError(QStringLiteral("开始逐笔 K 线缓存事务"));
        return false;
    }
    QSqlQuery remove(m_database);
    remove.prepare(QStringLiteral(
        "delete from candles where symbol = ? and period_seconds = ? and source = ? "
        "and start_time_utc >= ? and start_time_utc < ?"));
    remove.addBindValue(instrument.symbol);
    remove.addBindValue(periodSeconds);
    remove.addBindValue(QStringLiteral("raw_tick_cache_v1"));
    remove.addBindValue(from.toUTC().toString(Qt::ISODateWithMs));
    remove.addBindValue(to.toUTC().toString(Qt::ISODateWithMs));
    if (!remove.exec()) {
        m_database.rollback();
        emitSqlError(QStringLiteral("清理失效逐笔 K 线缓存"));
        return false;
    }

    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "insert into candles(symbol, name, market, venue, start_time_utc, period_seconds, open, high, low, close, volume, "
        "buy_volume, sell_volume, neutral_volume, directional_volume_known, source, adjustment, downloaded_at_utc) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "on conflict(symbol, period_seconds, start_time_utc) do update set "
        "name=excluded.name, market=excluded.market, venue=excluded.venue, open=excluded.open, high=excluded.high, "
        "low=excluded.low, close=excluded.close, volume=excluded.volume, buy_volume=excluded.buy_volume, "
        "sell_volume=excluded.sell_volume, neutral_volume=excluded.neutral_volume, "
        "directional_volume_known=excluded.directional_volume_known, source=excluded.source, "
        "adjustment=excluded.adjustment, downloaded_at_utc=excluded.downloaded_at_utc"));
    const QString generatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const Candle &candle : candles) {
        insert.bindValue(0, instrument.symbol);
        insert.bindValue(1, instrument.name);
        insert.bindValue(2, marketCode(instrument.market));
        insert.bindValue(3, instrument.venue);
        insert.bindValue(4, candle.startTime.toUTC().toString(Qt::ISODateWithMs));
        insert.bindValue(5, periodSeconds);
        insert.bindValue(6, candle.open);
        insert.bindValue(7, candle.high);
        insert.bindValue(8, candle.low);
        insert.bindValue(9, candle.close);
        insert.bindValue(10, candle.volume);
        insert.bindValue(11, candle.buyVolume);
        insert.bindValue(12, candle.sellVolume);
        insert.bindValue(13, candle.neutralVolume);
        insert.bindValue(14, candle.directionalVolumeKnown ? 1 : 0);
        insert.bindValue(15, QStringLiteral("raw_tick_cache_v1"));
        insert.bindValue(16, QStringLiteral("none"));
        insert.bindValue(17, generatedAt);
        if (!insert.exec()) {
            m_database.rollback();
            emitSqlError(QStringLiteral("写入逐笔派生 K 线缓存"));
            return false;
        }
        insert.finish();
    }
    QSqlQuery state(m_database);
    state.prepare(QStringLiteral(
        "insert into raw_tick_candle_cache_state(symbol, trading_date, period_seconds, raw_count, "
        "raw_max_id, bar_count, generated_at_utc, cache_version) values(?, ?, ?, ?, ?, ?, ?, 1) "
        "on conflict(symbol, trading_date, period_seconds) do update set raw_count=excluded.raw_count, "
        "raw_max_id=excluded.raw_max_id, bar_count=excluded.bar_count, "
        "generated_at_utc=excluded.generated_at_utc, cache_version=excluded.cache_version"));
    state.addBindValue(instrument.symbol);
    state.addBindValue(tradingDate.toString(Qt::ISODate));
    state.addBindValue(periodSeconds);
    state.addBindValue(fingerprint.count);
    state.addBindValue(fingerprint.maximumId);
    state.addBindValue(candles.size());
    state.addBindValue(generatedAt);
    if (!state.exec() || !m_database.commit()) {
        m_database.rollback();
        emitSqlError(QStringLiteral("提交逐笔 K 线缓存"));
        return false;
    }
    return true;
}

bool SqliteLocalDataStore::appendSignal(const TradeSignal &signal)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into signals(symbol, name, market, venue, timestamp_utc, side, price, volume, reason) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(signal.instrument.symbol);
    query.addBindValue(signal.instrument.name);
    query.addBindValue(marketCode(signal.instrument.market));
    query.addBindValue(signal.instrument.venue);
    query.addBindValue(signal.timestamp.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(signalSideName(signal.side));
    query.addBindValue(signal.price);
    query.addBindValue(signal.volume);
    query.addBindValue(signal.reason);

    const bool succeeded = query.exec();
    if (!succeeded) {
        emitSqlError(QStringLiteral("写入交易信号"));
    }
    return succeeded;
}

int SqliteLocalDataStore::tickCount() const
{
    return tableRowCount(QStringLiteral("ticks"));
}

int SqliteLocalDataStore::signalCount() const
{
    return tableRowCount(QStringLiteral("signals"));
}

QVector<Candle> SqliteLocalDataStore::loadCandles(const Instrument &instrument, int periodSeconds, const QDateTime &from, const QDateTime &to)
{
    QVector<Candle> candles;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select start_time_utc, open, high, low, close, volume, buy_volume, sell_volume, neutral_volume, "
        "directional_volume_known, source, adjustment, downloaded_at_utc "
        "from candles where symbol = ? and period_seconds = ? and start_time_utc >= ? and start_time_utc <= ? "
        "order by start_time_utc asc"));
    query.addBindValue(instrument.symbol);
    query.addBindValue(periodSeconds);
    query.addBindValue(from.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(to.toUTC().toString(Qt::ISODateWithMs));

    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取 K 线"));
        return candles;
    }

    while (query.next()) {
        candles.push_back(candleFromQuery(query, instrument, periodSeconds));
    }

    return candles;
}

QVector<Candle> SqliteLocalDataStore::loadRecentCandles(const Instrument &instrument, int periodSeconds, int limit)
{
    QVector<Candle> candles;
    if (limit <= 0) {
        return candles;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select start_time_utc, open, high, low, close, volume, buy_volume, sell_volume, neutral_volume, "
        "directional_volume_known, source, adjustment, downloaded_at_utc "
        "from candles where symbol = ? and period_seconds = ? and source != 'unknown' "
        "order by start_time_utc desc limit ?"));
    query.addBindValue(instrument.symbol);
    query.addBindValue(periodSeconds);
    query.addBindValue(limit);

    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取最近 K 线"));
        return candles;
    }

    while (query.next()) {
        candles.prepend(candleFromQuery(query, instrument, periodSeconds));
    }
    return candles;
}

QVector<Candle> SqliteLocalDataStore::loadRecentCandlesFromSource(
    const Instrument &instrument, int periodSeconds, int limit,
    const QString &source)
{
    QVector<Candle> candles;
    const QString normalizedSource = source.trimmed();
    if (limit <= 0 || normalizedSource.isEmpty()) return candles;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select start_time_utc, open, high, low, close, volume, buy_volume, sell_volume, neutral_volume, "
        "directional_volume_known, source, adjustment, downloaded_at_utc "
        "from candles where symbol = ? and period_seconds = ? and source = ? and close > 0 "
        "order by start_time_utc desc limit ?"));
    query.addBindValue(instrument.symbol);
    query.addBindValue(periodSeconds);
    query.addBindValue(normalizedSource);
    query.addBindValue(limit);

    if (!query.exec()) {
        emitSqlError(QStringLiteral("按来源读取最近 K 线"));
        return candles;
    }
    while (query.next()) {
        candles.prepend(candleFromQuery(query, instrument, periodSeconds));
    }
    return candles;
}

int SqliteLocalDataStore::candleCount(
    const Instrument &instrument, int periodSeconds, bool *ok)
{
    if (ok) *ok = false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select count(*) from candles where symbol = ? and period_seconds = ? "
        "and source != 'unknown' and close > 0"));
    query.addBindValue(instrument.symbol);
    query.addBindValue(periodSeconds);
    if (!query.exec() || !query.next()) {
        emitSqlError(QStringLiteral("统计本地 K 线"));
        return 0;
    }
    if (ok) *ok = true;
    return query.value(0).toInt();
}

QVector<Candle> SqliteLocalDataStore::loadCandlePage(
    const Instrument &instrument, int periodSeconds, int page, int pageSize, bool *ok)
{
    QVector<Candle> candles;
    if (ok) *ok = false;
    if (page < 0 || pageSize <= 0) return candles;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select start_time_utc, open, high, low, close, volume, buy_volume, sell_volume, neutral_volume, "
        "directional_volume_known, source, adjustment, downloaded_at_utc "
        "from candles where symbol = ? and period_seconds = ? and source != 'unknown' and close > 0 "
        "order by start_time_utc desc limit ? offset ?"));
    query.addBindValue(instrument.symbol);
    query.addBindValue(periodSeconds);
    query.addBindValue(pageSize);
    query.addBindValue(page * pageSize);
    if (!query.exec()) {
        emitSqlError(QStringLiteral("分页读取本地 K 线"));
        return candles;
    }
    while (query.next()) {
        candles.prepend(candleFromQuery(query, instrument, periodSeconds));
    }
    if (ok) *ok = true;
    return candles;
}

QVector<LocalHistoryDataset> SqliteLocalDataStore::localHistoryCatalog()
{
    QVector<LocalHistoryDataset> result;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "select symbol, max(name), max(market), max(venue), period_seconds, count(*), "
            "min(start_time_utc), max(start_time_utc), "
            "source, adjustment, "
            "max(downloaded_at_utc) "
            "from candles where source != 'unknown' and close > 0 "
            "group by symbol, period_seconds, source, adjustment "
            "order by symbol asc, period_seconds asc, source asc, adjustment asc"))) {
        emitSqlError(QStringLiteral("读取本地历史数据清单"));
        return result;
    }
    while (query.next()) {
        LocalHistoryDataset item;
        item.instrument.symbol = query.value(0).toString();
        item.instrument.name = query.value(1).toString();
        item.instrument.market = marketFromCode(query.value(2).toString(), Market::AShare);
        item.instrument.venue = query.value(3).toString();
        item.periodSeconds = query.value(4).toInt();
        item.candleCount = query.value(5).toInt();
        item.firstTime = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs);
        item.lastTime = QDateTime::fromString(query.value(7).toString(), Qt::ISODateWithMs);
        item.source = query.value(8).toString();
        item.adjustment = query.value(9).toString();
        item.downloadedAt = QDateTime::fromString(query.value(10).toString(), Qt::ISODateWithMs);
        result.append(item);
    }
    query.finish();
    QSqlQuery identity(m_database);
    identity.prepare(QStringLiteral(
        "select source, adjustment, actual_bars, data_version, downloaded_at_utc "
        "from historical_dataset_versions where symbol = ? and period_seconds = ? "
        "order by downloaded_at_utc desc limit 1"));
    for (LocalHistoryDataset &item : result) {
        identity.bindValue(0, item.instrument.symbol);
        identity.bindValue(1, item.periodSeconds);
        if (identity.exec() && identity.next()
            && identity.value(0).toString() == item.source
            && identity.value(1).toString() == item.adjustment
            && identity.value(2).toInt() == item.candleCount) {
            item.dataVersion = identity.value(3).toString();
            item.downloadedAt = QDateTime::fromString(
                identity.value(4).toString(), Qt::ISODateWithMs);
        }
        identity.finish();
    }
    return result;
}

QVector<BacktestDatasetIdentity> SqliteLocalDataStore::loadBacktestDatasetIdentities()
{
    QVector<BacktestDatasetIdentity> result;
    QHash<QString, QVector<RawTickDayFingerprint>> daysBySymbol;
    QHash<QString, Instrument> instrumentsBySymbol;

    QSqlQuery facts(m_database);
    if (!facts.exec(QStringLiteral(
            "select symbol, max(name), max(market), max(venue), trading_date, count(*), max(id) "
            "from raw_trade_ticks group by symbol, trading_date "
            "order by symbol asc, trading_date asc"))) {
        emitSqlError(QStringLiteral("读取原始逐笔数据集指纹"));
        return result;
    }
    while (facts.next()) {
        const QString symbol = facts.value(0).toString();
        const QDate tradingDate = QDate::fromString(facts.value(4).toString(), Qt::ISODate);
        if (symbol.isEmpty() || !tradingDate.isValid()) continue;
        RawTickDayFingerprint day;
        day.tradingDate = tradingDate;
        day.rawCount = facts.value(5).toLongLong();
        day.rawMaximumId = facts.value(6).toLongLong();
        daysBySymbol[symbol].append(day);

        Instrument instrument;
        instrument.symbol = symbol;
        instrument.name = facts.value(1).toString();
        instrument.market = marketFromCode(facts.value(2).toString(), Market::AShare);
        instrument.venue = facts.value(3).toString();
        instrumentsBySymbol.insert(symbol, instrument);
    }
    facts.finish();

    QHash<QString, QHash<QDate, RawTickDayFingerprint>> cacheByKey;
    QSqlQuery caches(m_database);
    if (!caches.exec(QStringLiteral(
            "select symbol, period_seconds, trading_date, raw_count, raw_max_id, "
            "bar_count, cache_version "
            "from raw_tick_candle_cache_state order by symbol asc, period_seconds asc, "
            "trading_date asc"))) {
        emitSqlError(QStringLiteral("读取逐笔派生缓存状态"));
        return result;
    }
    while (caches.next()) {
        const QString key = QStringLiteral("%1|%2")
            .arg(caches.value(0).toString(), caches.value(1).toString());
        const QDate tradingDate = QDate::fromString(caches.value(2).toString(), Qt::ISODate);
        if (!tradingDate.isValid()) continue;
        RawTickDayFingerprint cached;
        cached.tradingDate = tradingDate;
        cached.rawCount = caches.value(3).toLongLong();
        cached.rawMaximumId = caches.value(4).toLongLong();
        cached.barCount = caches.value(5).toInt();
        cached.cacheVersion = caches.value(6).toInt();
        cacheByKey[key].insert(tradingDate, cached);
    }
    caches.finish();

    QStringList keys = cacheByKey.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys) {
        const QStringList parts = key.split(QLatin1Char('|'));
        if (parts.size() != 2) continue;
        const QString symbol = parts.at(0);
        const int periodSeconds = parts.at(1).toInt();
        if (periodSeconds <= 0 || !instrumentsBySymbol.contains(symbol)) continue;

        BacktestDatasetIdentity identity;
        identity.instrument = instrumentsBySymbol.value(symbol);
        identity.periodSeconds = periodSeconds;
        identity.aggregatorVersion = 1;
        identity.days = daysBySymbol.value(symbol);
        const QHash<QDate, RawTickDayFingerprint> &cacheByDate = cacheByKey.value(key);
        for (RawTickDayFingerprint &day : identity.days) {
            const auto match = cacheByDate.constFind(day.tradingDate);
            if (match != cacheByDate.constEnd()) {
                day.barCount = match->barCount;
                day.cacheVersion = match->cacheVersion;
                if (match->rawCount != day.rawCount
                    || match->rawMaximumId != day.rawMaximumId) {
                    ++identity.staleDayCount;
                }
            } else {
                ++identity.staleDayCount;
            }
        }
        identity.firstTradingDate = identity.days.first().tradingDate;
        identity.lastTradingDate = identity.days.last().tradingDate;
        identity.datasetVersion = computeBacktestDatasetVersion(identity);
        result.append(identity);
    }
    return result;
}

std::optional<BacktestDatasetIdentity> SqliteLocalDataStore::loadBacktestDatasetIdentity(
    const Instrument &instrument, int periodSeconds)
{
    if (instrument.symbol.isEmpty() || periodSeconds <= 0) return std::nullopt;

    BacktestDatasetIdentity identity;
    identity.instrument = instrument;
    identity.periodSeconds = periodSeconds;
    identity.aggregatorVersion = 1;

    QSqlQuery facts(m_database);
    facts.prepare(QStringLiteral(
        "select trading_date, count(*), max(id) from raw_trade_ticks "
        "where symbol = ? group by trading_date order by trading_date asc"));
    facts.addBindValue(instrument.symbol);
    if (!facts.exec()) {
        emitSqlError(QStringLiteral("读取原始逐笔数据集指纹"));
        return std::nullopt;
    }
    while (facts.next()) {
        const QDate tradingDate = QDate::fromString(facts.value(0).toString(), Qt::ISODate);
        if (!tradingDate.isValid()) continue;
        RawTickDayFingerprint day;
        day.tradingDate = tradingDate;
        day.rawCount = facts.value(1).toLongLong();
        day.rawMaximumId = facts.value(2).toLongLong();
        identity.days.append(day);
    }
    facts.finish();
    if (identity.days.isEmpty()) return std::nullopt;

    identity.firstTradingDate = identity.days.first().tradingDate;
    identity.lastTradingDate = identity.days.last().tradingDate;

    QHash<QDate, RawTickDayFingerprint> cacheByDate;
    QSqlQuery caches(m_database);
    caches.prepare(QStringLiteral(
        "select trading_date, raw_count, raw_max_id, bar_count, cache_version "
        "from raw_tick_candle_cache_state "
        "where symbol = ? and period_seconds = ? order by trading_date asc"));
    caches.addBindValue(instrument.symbol);
    caches.addBindValue(periodSeconds);
    if (!caches.exec()) {
        emitSqlError(QStringLiteral("读取逐笔派生缓存状态"));
        return std::nullopt;
    }
    while (caches.next()) {
        const QDate tradingDate = QDate::fromString(caches.value(0).toString(), Qt::ISODate);
        if (!tradingDate.isValid()) continue;
        RawTickDayFingerprint cached;
        cached.tradingDate = tradingDate;
        cached.rawCount = caches.value(1).toLongLong();
        cached.rawMaximumId = caches.value(2).toLongLong();
        cached.barCount = caches.value(3).toInt();
        cached.cacheVersion = caches.value(4).toInt();
        cacheByDate.insert(tradingDate, cached);
    }
    caches.finish();

    for (RawTickDayFingerprint &day : identity.days) {
        const auto match = cacheByDate.constFind(day.tradingDate);
        if (match != cacheByDate.constEnd()) {
            day.barCount = match->barCount;
            day.cacheVersion = match->cacheVersion;
            if (match->rawCount != day.rawCount
                || match->rawMaximumId != day.rawMaximumId) {
                ++identity.staleDayCount;
            }
        } else {
            ++identity.staleDayCount;
        }
    }
    identity.datasetVersion = computeBacktestDatasetVersion(identity);
    return identity;
}

QHash<QDate, int> SqliteLocalDataStore::candleCountsByTradingDate(
    const Instrument &instrument, int periodSeconds,
    const QDate &startDate, const QDate &endDate)
{
    QHash<QDate, int> result;
    if (!startDate.isValid() || !endDate.isValid() || startDate > endDate) return result;
    const QTimeZone shanghai("Asia/Shanghai");
    const QDateTime from(startDate, QTime(0, 0), shanghai);
    const QDateTime to(endDate.addDays(1), QTime(0, 0), shanghai);
    const QVector<Candle> candles = loadCandles(
        instrument, periodSeconds, from.toUTC(), to.toUTC());
    for (const Candle &candle : candles) {
        const QDate date = candle.startTime.toTimeZone(shanghai).date();
        if (date >= startDate && date <= endDate) ++result[date];
    }
    return result;
}

bool SqliteLocalDataStore::upsertTickDataImportRun(
    const TickDataImportRunSummary &summary)
{
    if (summary.runId.trimmed().isEmpty()) return false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into tickdata_import_runs("
        "run_id, created_at_utc, completed_at_utc, symbol, name, market, venue, "
        "start_date, end_date, format, period_seconds, volume_unit, total_files, "
        "completed_files, succeeded_files, failed_files, missing_files, cancelled_files, "
        "quality_warning_files, valid_records, skipped_records, written_bars, status, error, "
        "skipped_existing_files, overlap_files, excluded_holiday_days, calendar_fallback_days, "
        "calendar_source) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?) "
        "on conflict(run_id) do update set "
        "completed_at_utc=excluded.completed_at_utc, completed_files=excluded.completed_files, "
        "succeeded_files=excluded.succeeded_files, failed_files=excluded.failed_files, "
        "missing_files=excluded.missing_files, cancelled_files=excluded.cancelled_files, "
        "quality_warning_files=excluded.quality_warning_files, valid_records=excluded.valid_records, "
        "skipped_records=excluded.skipped_records, written_bars=excluded.written_bars, "
        "status=excluded.status, error=excluded.error, "
        "skipped_existing_files=excluded.skipped_existing_files, "
        "overlap_files=excluded.overlap_files, "
        "excluded_holiday_days=excluded.excluded_holiday_days, "
        "calendar_fallback_days=excluded.calendar_fallback_days, "
        "calendar_source=excluded.calendar_source"));
    query.addBindValue(summary.runId);
    query.addBindValue(summary.createdAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(summary.completedAt.isValid()
                           ? summary.completedAt.toUTC().toString(Qt::ISODateWithMs)
                           : QVariant{});
    query.addBindValue(summary.instrument.symbol);
    query.addBindValue(summary.instrument.name);
    query.addBindValue(marketCode(summary.instrument.market));
    query.addBindValue(summary.instrument.venue);
    query.addBindValue(summary.startDate.toString(Qt::ISODate));
    query.addBindValue(summary.endDate.toString(Qt::ISODate));
    query.addBindValue(static_cast<int>(summary.format));
    query.addBindValue(summary.periodSeconds);
    query.addBindValue(static_cast<int>(summary.fiveMinuteVolumeUnit));
    query.addBindValue(summary.totalFiles);
    query.addBindValue(summary.completedFiles);
    query.addBindValue(summary.succeededFiles);
    query.addBindValue(summary.failedFiles);
    query.addBindValue(summary.missingFiles);
    query.addBindValue(summary.cancelledFiles);
    query.addBindValue(summary.qualityWarningFiles);
    query.addBindValue(summary.validRecords);
    query.addBindValue(summary.skippedRecords);
    query.addBindValue(summary.writtenBars);
    query.addBindValue(summary.status);
    query.addBindValue(summary.error.isNull() ? QStringLiteral("") : summary.error);
    query.addBindValue(summary.skippedExistingFiles);
    query.addBindValue(summary.overlapFiles);
    query.addBindValue(summary.excludedHolidayDays);
    query.addBindValue(summary.calendarFallbackDays);
    query.addBindValue(summary.calendarSource.isNull()
                           ? QStringLiteral("") : summary.calendarSource);
    if (!query.exec()) {
        emit writeFailed(QStringLiteral("保存 TickData 批量导入摘要失败：%1")
                             .arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool SqliteLocalDataStore::upsertTickDataImportRun(
    const TickDataImportRunSummary &summary,
    const QVector<TickDataBatchPlanItem> &items)
{
    if (!m_database.transaction()) {
        emitSqlError(QStringLiteral("开始 TickData 批次明细事务"));
        return false;
    }
    if (!upsertTickDataImportRun(summary)) {
        m_database.rollback();
        return false;
    }
    QSqlQuery remove(m_database);
    remove.prepare(QStringLiteral(
        "delete from tickdata_import_run_items where run_id = ?"));
    remove.addBindValue(summary.runId);
    if (!remove.exec()) {
        emit writeFailed(QStringLiteral("清理 TickData 批次明细失败：%1")
                             .arg(remove.lastError().text()));
        m_database.rollback();
        return false;
    }
    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "insert into tickdata_import_run_items("
        "run_id, trading_date, status, file_size, existing_bars, expected_bars, "
        "coverage_status, lines_read, valid_records, skipped_records, written_bars, "
        "missing_continuous_bars, quality_status, source, error) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const TickDataBatchPlanItem &item : items) {
        insert.addBindValue(summary.runId);
        insert.addBindValue(item.request.tradingDate.toString(Qt::ISODate));
        insert.addBindValue(tickDataBatchItemStatusCode(item.status));
        insert.addBindValue(item.fileSize);
        insert.addBindValue(item.existingBars);
        insert.addBindValue(item.expectedBars);
        insert.addBindValue(item.coverageStatus);
        insert.addBindValue(item.linesRead);
        insert.addBindValue(item.validRecords);
        insert.addBindValue(item.skippedRecords);
        insert.addBindValue(item.writtenBars);
        insert.addBindValue(item.missingContinuousBars);
        insert.addBindValue(item.qualityStatus);
        insert.addBindValue(item.source.isNull() ? QStringLiteral("") : item.source);
        insert.addBindValue(item.error.isNull() ? QStringLiteral("") : item.error);
        if (!insert.exec()) {
            emit writeFailed(QStringLiteral("保存 TickData 批次明细失败：%1")
                                 .arg(insert.lastError().text()));
            m_database.rollback();
            return false;
        }
        insert.finish();
    }
    if (!m_database.commit()) {
        emitSqlError(QStringLiteral("提交 TickData 批次明细事务"));
        m_database.rollback();
        return false;
    }
    return true;
}

QVector<TickDataImportRunSummary> SqliteLocalDataStore::loadTickDataImportRuns(
    int limit, const QString &symbolFilter, const QString &statusFilter)
{
    QVector<TickDataImportRunSummary> result;
    if (limit <= 0) return result;
    QSqlQuery query(m_database);
    QString sql = QStringLiteral(
        "select run_id, created_at_utc, completed_at_utc, symbol, name, market, venue, "
        "start_date, end_date, format, period_seconds, volume_unit, total_files, "
        "completed_files, succeeded_files, failed_files, missing_files, cancelled_files, "
        "quality_warning_files, valid_records, skipped_records, written_bars, status, error, "
        "skipped_existing_files, overlap_files, excluded_holiday_days, calendar_fallback_days, "
        "calendar_source from tickdata_import_runs where 1=1");
    const QString normalizedSymbol = symbolFilter.trimmed();
    const QString normalizedStatus = statusFilter.trimmed();
    if (!normalizedSymbol.isEmpty()) sql += QStringLiteral(" and symbol = ?");
    if (!normalizedStatus.isEmpty()) sql += QStringLiteral(" and status = ?");
    sql += QStringLiteral(" order by created_at_utc desc, run_id desc limit ?");
    query.prepare(sql);
    if (!normalizedSymbol.isEmpty()) query.addBindValue(normalizedSymbol);
    if (!normalizedStatus.isEmpty()) query.addBindValue(normalizedStatus);
    query.addBindValue(limit);
    if (!query.exec()) {
        emit writeFailed(QStringLiteral("读取 TickData 批量导入摘要失败：%1")
                             .arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        TickDataImportRunSummary summary;
        summary.runId = query.value(0).toString();
        summary.createdAt = QDateTime::fromString(query.value(1).toString(), Qt::ISODateWithMs);
        summary.completedAt = QDateTime::fromString(query.value(2).toString(), Qt::ISODateWithMs);
        summary.instrument.symbol = query.value(3).toString();
        summary.instrument.name = query.value(4).toString();
        summary.instrument.market = marketFromCode(query.value(5).toString());
        summary.instrument.venue = query.value(6).toString();
        summary.startDate = QDate::fromString(query.value(7).toString(), Qt::ISODate);
        summary.endDate = QDate::fromString(query.value(8).toString(), Qt::ISODate);
        summary.format = static_cast<TickDataCsvFormat>(query.value(9).toInt());
        summary.periodSeconds = query.value(10).toInt();
        summary.fiveMinuteVolumeUnit = static_cast<FiveMinuteVolumeUnit>(query.value(11).toInt());
        summary.totalFiles = query.value(12).toInt();
        summary.completedFiles = query.value(13).toInt();
        summary.succeededFiles = query.value(14).toInt();
        summary.failedFiles = query.value(15).toInt();
        summary.missingFiles = query.value(16).toInt();
        summary.cancelledFiles = query.value(17).toInt();
        summary.qualityWarningFiles = query.value(18).toInt();
        summary.validRecords = query.value(19).toLongLong();
        summary.skippedRecords = query.value(20).toLongLong();
        summary.writtenBars = query.value(21).toInt();
        summary.status = query.value(22).toString();
        summary.error = query.value(23).toString();
        summary.skippedExistingFiles = query.value(24).toInt();
        summary.overlapFiles = query.value(25).toInt();
        summary.excludedHolidayDays = query.value(26).toInt();
        summary.calendarFallbackDays = query.value(27).toInt();
        summary.calendarSource = query.value(28).toString();
        result.append(summary);
    }
    return result;
}

std::optional<TickDataImportRunSummary> SqliteLocalDataStore::loadTickDataImportRun(
    const QString &runId)
{
    if (runId.trimmed().isEmpty()) return std::nullopt;
    const QVector<TickDataImportRunSummary> candidates = loadTickDataImportRuns(1000);
    for (const TickDataImportRunSummary &summary : candidates) {
        if (summary.runId == runId) return summary;
    }
    return std::nullopt;
}

QVector<TickDataBatchPlanItem> SqliteLocalDataStore::loadTickDataImportRunItems(
    const QString &runId)
{
    QVector<TickDataBatchPlanItem> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select trading_date, status, file_size, existing_bars, expected_bars, "
        "coverage_status, lines_read, valid_records, skipped_records, written_bars, "
        "missing_continuous_bars, quality_status, source, error "
        "from tickdata_import_run_items where run_id = ? order by trading_date"));
    query.addBindValue(runId.trimmed());
    if (!query.exec()) {
        emit writeFailed(QStringLiteral("读取 TickData 批次明细失败：%1")
                             .arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        TickDataBatchPlanItem item;
        item.request.tradingDate = QDate::fromString(query.value(0).toString(), Qt::ISODate);
        const QString status = query.value(1).toString();
        if (status == QStringLiteral("ready")) item.status = TickDataBatchItemStatus::Ready;
        else if (status == QStringLiteral("running")) item.status = TickDataBatchItemStatus::Running;
        else if (status == QStringLiteral("succeeded")) item.status = TickDataBatchItemStatus::Succeeded;
        else if (status == QStringLiteral("failed")) item.status = TickDataBatchItemStatus::Failed;
        else if (status == QStringLiteral("cancelled")) item.status = TickDataBatchItemStatus::Cancelled;
        else if (status == QStringLiteral("skipped_existing")) item.status = TickDataBatchItemStatus::SkippedExisting;
        else item.status = TickDataBatchItemStatus::Missing;
        item.fileSize = query.value(2).toLongLong();
        item.existingBars = query.value(3).toInt();
        item.expectedBars = query.value(4).toInt();
        item.coverageStatus = query.value(5).toString();
        item.linesRead = query.value(6).toLongLong();
        item.validRecords = query.value(7).toLongLong();
        item.skippedRecords = query.value(8).toLongLong();
        item.writtenBars = query.value(9).toInt();
        item.missingContinuousBars = query.value(10).toInt();
        item.qualityStatus = query.value(11).toString();
        item.source = query.value(12).toString();
        item.error = query.value(13).toString();
        result.append(item);
    }
    return result;
}

bool SqliteLocalDataStore::saveBacktestArchive(const BacktestArchiveRecord &record)
{
    const BacktestConfig &config = record.result.config;
    if (record.resultId.trimmed().isEmpty() || config.taskId.trimmed().isEmpty()
        || !record.completedAt.isValid() || config.strategyId.trimmed().isEmpty()
        || config.instrument.symbol.trimmed().isEmpty() || config.frequency.trimmed().isEmpty()
        || config.actualBars <= 0 || record.result.equityCurve.isEmpty()
        || !isSha256(config.dataVersion)
        || (!config.datasetIdentity.isEmpty()
            && !isSha256(config.datasetIdentity.datasetVersion))
        || (config.strategyId.startsWith(QStringLiteral("custom_"))
            && (config.strategyVersion < 1 || config.ruleSnapshot.isEmpty()))) {
        emit writeFailed(QStringLiteral("回测结果身份不完整，未写入归档"));
        return false;
    }
    if (!m_database.transaction()) {
        emitSqlError(QStringLiteral("开始回测结果归档事务"));
        return false;
    }
    auto fail = [this](const QString &operation, const QSqlQuery &query) {
        m_database.rollback();
        emit writeFailed(QStringLiteral("%1失败：%2").arg(operation, query.lastError().text()));
        return false;
    };

    QSqlQuery resultQuery(m_database);
    resultQuery.prepare(QStringLiteral(
        "insert into backtest_results("
        "result_id, task_id, completed_at_utc, strategy_id, strategy_version, "
        "strategy_parameters_json, rule_snapshot_json, data_version, symbol, name, market, venue, "
        "frequency, period_seconds, requested_bars, actual_bars, from_utc, to_utc, "
        "initial_capital, commission_rate, slippage_rate, lot_size, summary_initial_capital, "
        "final_equity, total_return_percent, max_drawdown_percent, win_rate_percent, sharpe_ratio, "
        "total_commission, total_trades, winning_trades, losing_trades, "
        "dataset_version, dataset_identity_json, dataset_raw_tick_count, "
        "dataset_derived_bar_count, dataset_day_count, dataset_first_date, dataset_last_date, "
        "dataset_gap_day_count, dataset_stale_day_count, dataset_calendar_source, "
        "dataset_calendar_coverage_known) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    const PerformanceSummary &summary = record.result.summary;
    const BacktestDatasetIdentity &dataset = config.datasetIdentity;
    const QVariantList values {
        record.resultId, config.taskId, record.completedAt.toUTC().toString(Qt::ISODateWithMs),
        config.strategyId, config.strategyVersion, compactJson(config.strategyParameters),
        compactJson(config.ruleSnapshot), config.dataVersion, config.instrument.symbol,
        config.instrument.name, static_cast<int>(config.instrument.market), config.instrument.venue,
        config.frequency, config.periodSeconds, config.requestedBars, config.actualBars,
        config.from.toUTC().toString(Qt::ISODateWithMs), config.to.toUTC().toString(Qt::ISODateWithMs),
        config.initialCapital, config.commissionRate, config.slippageRate, config.lotSize,
        summary.initialCapital, summary.finalEquity, summary.totalReturnPercent,
        summary.maxDrawdownPercent, summary.winRatePercent, summary.sharpeRatio,
        summary.totalCommission, summary.totalTrades, summary.winningTrades, summary.losingTrades,
        dataset.datasetVersion.isEmpty() ? QStringLiteral("") : dataset.datasetVersion,
        dataset.isEmpty() ? QStringLiteral("") : dataset.toJson(),
        dataset.rawTickTotal(), dataset.derivedBarTotal(), dataset.days.size(),
        dataset.firstTradingDate.isValid()
            ? dataset.firstTradingDate.toString(Qt::ISODate) : QStringLiteral(""),
        dataset.lastTradingDate.isValid()
            ? dataset.lastTradingDate.toString(Qt::ISODate) : QStringLiteral(""),
        dataset.gapDayCount, dataset.staleDayCount,
        dataset.calendarSource.isEmpty() ? QStringLiteral("") : dataset.calendarSource,
        dataset.calendarCoverageKnown ? 1 : 0
    };
    for (const QVariant &value : values) resultQuery.addBindValue(value);
    if (!resultQuery.exec()) return fail(QStringLiteral("写入回测结果身份"), resultQuery);

    QSqlQuery equityQuery(m_database);
    equityQuery.prepare(QStringLiteral(
        "insert into backtest_equity_points(result_id, sequence_no, timestamp_utc, equity, cash, "
        "position_value, drawdown_percent) values(?, ?, ?, ?, ?, ?, ?)"));
    for (int index = 0; index < record.result.equityCurve.size(); ++index) {
        const EquityPoint &point = record.result.equityCurve.at(index);
        equityQuery.bindValue(0, record.resultId);
        equityQuery.bindValue(1, index);
        equityQuery.bindValue(2, point.timestamp.toUTC().toString(Qt::ISODateWithMs));
        equityQuery.bindValue(3, point.equity);
        equityQuery.bindValue(4, point.cash);
        equityQuery.bindValue(5, point.positionValue);
        equityQuery.bindValue(6, point.drawdownPercent);
        if (!equityQuery.exec()) return fail(QStringLiteral("写入回测权益曲线"), equityQuery);
    }

    QSqlQuery tradeQuery(m_database);
    tradeQuery.prepare(QStringLiteral(
        "insert into backtest_trades(result_id, sequence_no, symbol, name, market, venue, "
        "entry_time_utc, exit_time_utc, entry_price, exit_price, quantity, gross_pnl, net_pnl, "
        "commission, return_percent, entry_reason, exit_reason) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (int index = 0; index < record.result.trades.size(); ++index) {
        const BacktestTrade &trade = record.result.trades.at(index);
        const QVariantList tradeValues {
            record.resultId, index, trade.instrument.symbol, trade.instrument.name,
            static_cast<int>(trade.instrument.market), trade.instrument.venue,
            trade.entryTime.toUTC().toString(Qt::ISODateWithMs),
            trade.exitTime.toUTC().toString(Qt::ISODateWithMs), trade.entryPrice, trade.exitPrice,
            trade.quantity, trade.grossPnl, trade.netPnl, trade.commission,
            trade.returnPercent, trade.entryReason, trade.exitReason
        };
        for (int column = 0; column < tradeValues.size(); ++column) {
            tradeQuery.bindValue(column, tradeValues.at(column));
        }
        if (!tradeQuery.exec()) return fail(QStringLiteral("写入回测成交明细"), tradeQuery);
    }

    if (!m_database.commit()) {
        m_database.rollback();
        emitSqlError(QStringLiteral("提交回测结果归档事务"));
        return false;
    }
    return true;
}

QVector<BacktestArchiveSummary> SqliteLocalDataStore::loadBacktestArchiveSummaries()
{
    QVector<BacktestArchiveSummary> result;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "select result_id, task_id, completed_at_utc, strategy_id, strategy_version, symbol, name, "
            "market, venue, frequency, requested_bars, actual_bars, data_version, "
            "dataset_version, dataset_first_date, dataset_last_date, dataset_day_count, "
            "dataset_raw_tick_count, dataset_derived_bar_count, dataset_gap_day_count, "
            "dataset_stale_day_count, dataset_calendar_source, dataset_calendar_coverage_known, "
            "summary_initial_capital, final_equity, total_return_percent, max_drawdown_percent, "
            "win_rate_percent, sharpe_ratio, total_commission, total_trades, winning_trades, losing_trades "
            "from backtest_results order by completed_at_utc desc, result_id desc"))) {
        emitSqlError(QStringLiteral("读取回测归档列表"));
        return result;
    }
    while (query.next()) {
        BacktestArchiveSummary item;
        item.resultId = query.value(0).toString();
        item.taskId = query.value(1).toString();
        item.completedAt = QDateTime::fromString(query.value(2).toString(), Qt::ISODateWithMs);
        item.strategyId = query.value(3).toString();
        item.strategyVersion = query.value(4).toInt();
        item.instrument = {query.value(5).toString(), query.value(6).toString(),
                           static_cast<Market>(query.value(7).toInt()), query.value(8).toString()};
        item.frequency = query.value(9).toString();
        item.requestedBars = query.value(10).toInt();
        item.actualBars = query.value(11).toInt();
        item.dataVersion = query.value(12).toString();
        item.datasetVersion = query.value(13).toString();
        item.datasetFirstDate = QDate::fromString(query.value(14).toString(), Qt::ISODate);
        item.datasetLastDate = QDate::fromString(query.value(15).toString(), Qt::ISODate);
        item.datasetDayCount = query.value(16).toInt();
        item.datasetRawTickCount = query.value(17).toInt();
        item.datasetDerivedBarCount = query.value(18).toInt();
        item.datasetGapDayCount = query.value(19).toInt();
        item.datasetStaleDayCount = query.value(20).toInt();
        item.datasetCalendarSource = query.value(21).toString();
        item.datasetCalendarCoverageKnown = query.value(22).toBool();
        item.summary.initialCapital = query.value(23).toDouble();
        item.summary.finalEquity = query.value(24).toDouble();
        item.summary.totalReturnPercent = query.value(25).toDouble();
        item.summary.maxDrawdownPercent = query.value(26).toDouble();
        item.summary.winRatePercent = query.value(27).toDouble();
        item.summary.sharpeRatio = query.value(28).toDouble();
        item.summary.totalCommission = query.value(29).toDouble();
        item.summary.totalTrades = query.value(30).toInt();
        item.summary.winningTrades = query.value(31).toInt();
        item.summary.losingTrades = query.value(32).toInt();
        result.append(item);
    }
    return result;
}

std::optional<BacktestArchiveRecord> SqliteLocalDataStore::loadBacktestArchive(
    const QString &resultId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select task_id, completed_at_utc, strategy_id, strategy_version, strategy_parameters_json, "
        "rule_snapshot_json, data_version, symbol, name, market, venue, frequency, period_seconds, "
        "requested_bars, actual_bars, from_utc, to_utc, initial_capital, commission_rate, "
        "slippage_rate, lot_size, summary_initial_capital, final_equity, total_return_percent, "
        "max_drawdown_percent, win_rate_percent, sharpe_ratio, total_commission, total_trades, "
        "winning_trades, losing_trades, dataset_identity_json from backtest_results "
        "where result_id = ?"));
    query.addBindValue(resultId);
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取回测归档身份"));
        return std::nullopt;
    }
    if (!query.next()) return std::nullopt;

    BacktestArchiveRecord record;
    record.resultId = resultId;
    BacktestConfig &config = record.result.config;
    config.taskId = query.value(0).toString();
    record.completedAt = QDateTime::fromString(query.value(1).toString(), Qt::ISODateWithMs);
    config.strategyId = query.value(2).toString();
    config.strategyVersion = query.value(3).toInt();
    config.strategyParameters = variantMapFromJson(query.value(4).toString());
    config.ruleSnapshot = variantMapFromJson(query.value(5).toString());
    config.dataVersion = query.value(6).toString();
    config.instrument = {query.value(7).toString(), query.value(8).toString(),
                         static_cast<Market>(query.value(9).toInt()), query.value(10).toString()};
    config.frequency = query.value(11).toString();
    config.periodSeconds = query.value(12).toInt();
    config.requestedBars = query.value(13).toInt();
    config.actualBars = query.value(14).toInt();
    config.from = QDateTime::fromString(query.value(15).toString(), Qt::ISODateWithMs);
    config.to = QDateTime::fromString(query.value(16).toString(), Qt::ISODateWithMs);
    config.initialCapital = query.value(17).toDouble();
    config.commissionRate = query.value(18).toDouble();
    config.slippageRate = query.value(19).toDouble();
    config.lotSize = query.value(20).toInt();
    PerformanceSummary &summary = record.result.summary;
    summary.initialCapital = query.value(21).toDouble();
    summary.finalEquity = query.value(22).toDouble();
    summary.totalReturnPercent = query.value(23).toDouble();
    summary.maxDrawdownPercent = query.value(24).toDouble();
    summary.winRatePercent = query.value(25).toDouble();
    summary.sharpeRatio = query.value(26).toDouble();
    summary.totalCommission = query.value(27).toDouble();
    summary.totalTrades = query.value(28).toInt();
    summary.winningTrades = query.value(29).toInt();
    summary.losingTrades = query.value(30).toInt();
    const QString datasetJson = query.value(31).toString();
    if (!datasetJson.isEmpty()) {
        BacktestDatasetIdentity dataset;
        if (dataset.fromJson(datasetJson)) config.datasetIdentity = dataset;
    }

    QSqlQuery equityQuery(m_database);
    equityQuery.prepare(QStringLiteral(
        "select timestamp_utc, equity, cash, position_value, drawdown_percent "
        "from backtest_equity_points where result_id = ? order by sequence_no asc"));
    equityQuery.addBindValue(resultId);
    if (!equityQuery.exec()) {
        emitSqlError(QStringLiteral("读取回测权益曲线"));
        return std::nullopt;
    }
    while (equityQuery.next()) {
        EquityPoint point;
        point.timestamp = QDateTime::fromString(equityQuery.value(0).toString(), Qt::ISODateWithMs);
        point.equity = equityQuery.value(1).toDouble();
        point.cash = equityQuery.value(2).toDouble();
        point.positionValue = equityQuery.value(3).toDouble();
        point.drawdownPercent = equityQuery.value(4).toDouble();
        record.result.equityCurve.append(point);
    }

    QSqlQuery tradeQuery(m_database);
    tradeQuery.prepare(QStringLiteral(
        "select symbol, name, market, venue, entry_time_utc, exit_time_utc, entry_price, exit_price, "
        "quantity, gross_pnl, net_pnl, commission, return_percent, entry_reason, exit_reason "
        "from backtest_trades where result_id = ? order by sequence_no asc"));
    tradeQuery.addBindValue(resultId);
    if (!tradeQuery.exec()) {
        emitSqlError(QStringLiteral("读取回测成交明细"));
        return std::nullopt;
    }
    while (tradeQuery.next()) {
        BacktestTrade trade;
        trade.instrument = {tradeQuery.value(0).toString(), tradeQuery.value(1).toString(),
                            static_cast<Market>(tradeQuery.value(2).toInt()),
                            tradeQuery.value(3).toString()};
        trade.entryTime = QDateTime::fromString(tradeQuery.value(4).toString(), Qt::ISODateWithMs);
        trade.exitTime = QDateTime::fromString(tradeQuery.value(5).toString(), Qt::ISODateWithMs);
        trade.entryPrice = tradeQuery.value(6).toDouble();
        trade.exitPrice = tradeQuery.value(7).toDouble();
        trade.quantity = tradeQuery.value(8).toDouble();
        trade.grossPnl = tradeQuery.value(9).toDouble();
        trade.netPnl = tradeQuery.value(10).toDouble();
        trade.commission = tradeQuery.value(11).toDouble();
        trade.returnPercent = tradeQuery.value(12).toDouble();
        trade.entryReason = tradeQuery.value(13).toString();
        trade.exitReason = tradeQuery.value(14).toString();
        record.result.trades.append(trade);
    }
    if (record.result.equityCurve.isEmpty()) {
        emit writeFailed(QStringLiteral("回测归档 %1 缺少权益曲线，拒绝恢复").arg(resultId));
        return std::nullopt;
    }
    return record;
}

bool SqliteLocalDataStore::upsertOrder(const Order &order)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into orders(order_id, client_order_id, symbol, name, market, venue, side, order_type, "
        "time_in_force, quantity, limit_price, reference_price, created_at_utc, expires_at_utc, status, "
        "filled_quantity, average_fill_price, message, updated_at_utc) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "on conflict(order_id) do update set status=excluded.status, "
        "filled_quantity=excluded.filled_quantity, average_fill_price=excluded.average_fill_price, "
        "message=excluded.message, updated_at_utc=excluded.updated_at_utc"));
    query.addBindValue(order.orderId);
    query.addBindValue(order.request.clientOrderId);
    query.addBindValue(order.request.instrument.symbol);
    query.addBindValue(order.request.instrument.name);
    query.addBindValue(static_cast<int>(order.request.instrument.market));
    query.addBindValue(order.request.instrument.venue);
    query.addBindValue(static_cast<int>(order.request.side));
    query.addBindValue(static_cast<int>(order.request.type));
    query.addBindValue(static_cast<int>(order.request.timeInForce));
    query.addBindValue(order.request.quantity);
    query.addBindValue(order.request.limitPrice);
    query.addBindValue(order.request.referencePrice);
    query.addBindValue(order.request.createdAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(order.request.expiresAt.isValid()
        ? order.request.expiresAt.toUTC().toString(Qt::ISODateWithMs) : QString());
    query.addBindValue(static_cast<int>(order.status));
    query.addBindValue(order.filledQuantity);
    query.addBindValue(order.averageFillPrice);
    query.addBindValue(order.message.isNull() ? QStringLiteral("") : order.message);
    query.addBindValue(order.updatedAt.toUTC().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        emit writeFailed(QStringLiteral("保存订单失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool SqliteLocalDataStore::appendExecution(const Execution &execution)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert or ignore into executions(execution_id, order_id, symbol, name, market, venue, side, "
        "quantity, price, commission, timestamp_utc) values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(execution.executionId);
    query.addBindValue(execution.orderId);
    query.addBindValue(execution.instrument.symbol);
    query.addBindValue(execution.instrument.name);
    query.addBindValue(static_cast<int>(execution.instrument.market));
    query.addBindValue(execution.instrument.venue);
    query.addBindValue(static_cast<int>(execution.side));
    query.addBindValue(execution.quantity);
    query.addBindValue(execution.price);
    query.addBindValue(execution.commission);
    query.addBindValue(execution.timestamp.toUTC().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        emit writeFailed(QStringLiteral("保存成交失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool SqliteLocalDataStore::saveTradingState(const AccountSnapshot &account,
                                            const QVector<Position> &positions)
{
    if (!m_database.transaction()) {
        emitSqlError(QStringLiteral("开始保存账户状态事务"));
        return false;
    }

    QSqlQuery accountQuery(m_database);
    accountQuery.prepare(QStringLiteral(
        "insert into account_snapshots(timestamp_utc, trading_day, initial_equity, day_start_equity, cash, "
        "frozen_cash, market_value, total_equity, realized_pnl, unrealized_pnl, total_commission, daily_pnl) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    accountQuery.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    accountQuery.addBindValue(account.tradingDay.isValid()
        ? account.tradingDay.toString(Qt::ISODate) : QStringLiteral(""));
    accountQuery.addBindValue(account.initialEquity);
    accountQuery.addBindValue(account.dayStartEquity);
    accountQuery.addBindValue(account.cash);
    accountQuery.addBindValue(account.frozenCash);
    accountQuery.addBindValue(account.marketValue);
    accountQuery.addBindValue(account.totalEquity);
    accountQuery.addBindValue(account.realizedPnl);
    accountQuery.addBindValue(account.unrealizedPnl);
    accountQuery.addBindValue(account.totalCommission);
    accountQuery.addBindValue(account.dailyPnl);
    if (!accountQuery.exec()) {
        m_database.rollback();
        emitSqlError(QStringLiteral("保存账户快照"));
        return false;
    }

    QSqlQuery pruneQuery(m_database);
    if (!pruneQuery.exec(QStringLiteral(
            "delete from account_snapshots where id not in ("
            "select id from account_snapshots order by id desc limit 1000)"))) {
        m_database.rollback();
        emitSqlError(QStringLiteral("清理旧账户快照"));
        return false;
    }

    QSqlQuery deleteQuery(m_database);
    if (!deleteQuery.exec(QStringLiteral("delete from positions"))) {
        m_database.rollback();
        emitSqlError(QStringLiteral("更新持仓快照"));
        return false;
    }

    QSqlQuery positionQuery(m_database);
    positionQuery.prepare(QStringLiteral(
        "insert into positions(symbol, name, market, venue, quantity, available_quantity, average_cost, "
        "market_price, market_value, unrealized_pnl, realized_pnl) values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const Position &position : positions) {
        positionQuery.bindValue(0, position.instrument.symbol);
        positionQuery.bindValue(1, position.instrument.name);
        positionQuery.bindValue(2, static_cast<int>(position.instrument.market));
        positionQuery.bindValue(3, position.instrument.venue);
        positionQuery.bindValue(4, position.quantity);
        positionQuery.bindValue(5, position.availableQuantity);
        positionQuery.bindValue(6, position.averageCost);
        positionQuery.bindValue(7, position.marketPrice);
        positionQuery.bindValue(8, position.marketValue);
        positionQuery.bindValue(9, position.unrealizedPnl);
        positionQuery.bindValue(10, position.realizedPnl);
        if (!positionQuery.exec()) {
            m_database.rollback();
            emitSqlError(QStringLiteral("保存持仓快照"));
            return false;
        }
        positionQuery.finish();
    }

    if (!m_database.commit()) {
        emitSqlError(QStringLiteral("提交账户状态事务"));
        return false;
    }
    return true;
}

TradingState SqliteLocalDataStore::loadTradingState()
{
    TradingState state;
    QSqlQuery accountQuery(m_database);
    if (accountQuery.exec(QStringLiteral(
            "select trading_day, initial_equity, day_start_equity, cash, frozen_cash, market_value, total_equity, "
            "realized_pnl, unrealized_pnl, total_commission, daily_pnl "
            "from account_snapshots order by id desc limit 1"))
        && accountQuery.next()) {
        state.hasAccountSnapshot = true;
        state.account.tradingDay = QDate::fromString(accountQuery.value(0).toString(), Qt::ISODate);
        state.account.initialEquity = accountQuery.value(1).toDouble();
        state.account.dayStartEquity = accountQuery.value(2).toDouble();
        state.account.cash = accountQuery.value(3).toDouble();
        state.account.frozenCash = accountQuery.value(4).toDouble();
        state.account.marketValue = accountQuery.value(5).toDouble();
        state.account.totalEquity = accountQuery.value(6).toDouble();
        state.account.realizedPnl = accountQuery.value(7).toDouble();
        state.account.unrealizedPnl = accountQuery.value(8).toDouble();
        state.account.totalCommission = accountQuery.value(9).toDouble();
        state.account.dailyPnl = accountQuery.value(10).toDouble();
    }

    QSqlQuery orderQuery(m_database);
    if (orderQuery.exec(QStringLiteral(
            "select order_id, client_order_id, symbol, name, market, venue, side, order_type, time_in_force, "
            "quantity, limit_price, reference_price, created_at_utc, expires_at_utc, status, filled_quantity, "
            "average_fill_price, message, updated_at_utc from orders order by created_at_utc asc"))) {
        while (orderQuery.next()) {
            Order order;
            order.orderId = orderQuery.value(0).toString();
            order.request.clientOrderId = orderQuery.value(1).toString();
            order.request.instrument.symbol = orderQuery.value(2).toString();
            order.request.instrument.name = orderQuery.value(3).toString();
            order.request.instrument.market = static_cast<Market>(orderQuery.value(4).toInt());
            order.request.instrument.venue = orderQuery.value(5).toString();
            order.request.side = static_cast<OrderSide>(orderQuery.value(6).toInt());
            order.request.type = static_cast<OrderType>(orderQuery.value(7).toInt());
            order.request.timeInForce = static_cast<TimeInForce>(orderQuery.value(8).toInt());
            order.request.quantity = orderQuery.value(9).toDouble();
            order.request.limitPrice = orderQuery.value(10).toDouble();
            order.request.referencePrice = orderQuery.value(11).toDouble();
            order.request.createdAt = QDateTime::fromString(orderQuery.value(12).toString(), Qt::ISODateWithMs);
            order.request.expiresAt = QDateTime::fromString(orderQuery.value(13).toString(), Qt::ISODateWithMs);
            order.status = static_cast<OrderStatus>(orderQuery.value(14).toInt());
            order.filledQuantity = orderQuery.value(15).toDouble();
            order.averageFillPrice = orderQuery.value(16).toDouble();
            order.message = orderQuery.value(17).toString();
            order.updatedAt = QDateTime::fromString(orderQuery.value(18).toString(), Qt::ISODateWithMs);
            state.orders.append(order);
        }
    }

    QSqlQuery executionQuery(m_database);
    if (executionQuery.exec(QStringLiteral(
            "select execution_id, order_id, symbol, name, market, venue, side, quantity, price, commission, "
            "timestamp_utc from executions order by timestamp_utc asc"))) {
        while (executionQuery.next()) {
            Execution execution;
            execution.executionId = executionQuery.value(0).toString();
            execution.orderId = executionQuery.value(1).toString();
            execution.instrument.symbol = executionQuery.value(2).toString();
            execution.instrument.name = executionQuery.value(3).toString();
            execution.instrument.market = static_cast<Market>(executionQuery.value(4).toInt());
            execution.instrument.venue = executionQuery.value(5).toString();
            execution.side = static_cast<OrderSide>(executionQuery.value(6).toInt());
            execution.quantity = executionQuery.value(7).toDouble();
            execution.price = executionQuery.value(8).toDouble();
            execution.commission = executionQuery.value(9).toDouble();
            execution.timestamp = QDateTime::fromString(executionQuery.value(10).toString(), Qt::ISODateWithMs);
            state.executions.append(execution);
        }
    }

    QSqlQuery positionQuery(m_database);
    if (positionQuery.exec(QStringLiteral(
            "select symbol, name, market, venue, quantity, available_quantity, average_cost, market_price, "
            "market_value, unrealized_pnl, realized_pnl from positions order by symbol asc"))) {
        while (positionQuery.next()) {
            Position position;
            position.instrument.symbol = positionQuery.value(0).toString();
            position.instrument.name = positionQuery.value(1).toString();
            position.instrument.market = static_cast<Market>(positionQuery.value(2).toInt());
            position.instrument.venue = positionQuery.value(3).toString();
            position.quantity = positionQuery.value(4).toDouble();
            position.availableQuantity = positionQuery.value(5).toDouble();
            position.averageCost = positionQuery.value(6).toDouble();
            position.marketPrice = positionQuery.value(7).toDouble();
            position.marketValue = positionQuery.value(8).toDouble();
            position.unrealizedPnl = positionQuery.value(9).toDouble();
            position.realizedPnl = positionQuery.value(10).toDouble();
            state.positions.append(position);
        }
    }
    return state;
}

bool SqliteLocalDataStore::upsertStrategyConfiguration(const StrategyExecutionConfig &config)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into strategy_execution_configs(strategy_id, enabled, execution_mode, order_type, time_in_force, "
        "fixed_order_value, max_position_percent, sell_all, cooldown_seconds, approval_timeout_seconds, "
        "limit_offset_bps, lot_size, order_timeout_seconds, max_orders_per_day, max_daily_order_value) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "on conflict(strategy_id) do update set enabled=excluded.enabled, execution_mode=excluded.execution_mode, "
        "order_type=excluded.order_type, time_in_force=excluded.time_in_force, "
        "fixed_order_value=excluded.fixed_order_value, "
        "max_position_percent=excluded.max_position_percent, sell_all=excluded.sell_all, "
        "cooldown_seconds=excluded.cooldown_seconds, approval_timeout_seconds=excluded.approval_timeout_seconds, "
        "limit_offset_bps=excluded.limit_offset_bps, lot_size=excluded.lot_size, "
        "order_timeout_seconds=excluded.order_timeout_seconds, max_orders_per_day=excluded.max_orders_per_day, "
        "max_daily_order_value=excluded.max_daily_order_value"));
    query.addBindValue(config.strategyId);
    query.addBindValue(config.enabled ? 1 : 0);
    query.addBindValue(static_cast<int>(config.mode));
    query.addBindValue(static_cast<int>(config.orderType));
    query.addBindValue(static_cast<int>(config.timeInForce));
    query.addBindValue(config.fixedOrderValue);
    query.addBindValue(config.maxPositionPercent);
    query.addBindValue(config.sellAll ? 1 : 0);
    query.addBindValue(config.cooldownSeconds);
    query.addBindValue(config.approvalTimeoutSeconds);
    query.addBindValue(config.limitOffsetBps);
    query.addBindValue(config.lotSize);
    query.addBindValue(config.orderTimeoutSeconds);
    query.addBindValue(config.maxOrdersPerDay);
    query.addBindValue(config.maxDailyOrderValue);
    if (!query.exec()) {
        emit writeFailed(QStringLiteral("保存策略执行配置失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool SqliteLocalDataStore::removeStrategyConfiguration(const QString &strategyId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("delete from strategy_execution_configs where strategy_id = ?"));
    query.addBindValue(strategyId);
    if (!query.exec()) {
        emit writeFailed(QStringLiteral("删除策略执行配置失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool SqliteLocalDataStore::upsertStrategyIntent(const StrategyOrderIntent &intent)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into strategy_order_intents(intent_id, strategy_id, symbol, name, market, venue, signal_time_utc, "
        "signal_side, signal_price, signal_volume, signal_reason, order_side, order_type, order_time_in_force, "
        "quantity, limit_price, reference_price, order_expires_at_utc, intent_status, order_id, message, "
        "created_at_utc, updated_at_utc, expires_at_utc) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "on conflict(intent_id) do update set order_side=excluded.order_side, order_type=excluded.order_type, "
        "order_time_in_force=excluded.order_time_in_force, quantity=excluded.quantity, "
        "limit_price=excluded.limit_price, reference_price=excluded.reference_price, "
        "order_expires_at_utc=excluded.order_expires_at_utc, "
        "intent_status=excluded.intent_status, order_id=excluded.order_id, message=excluded.message, "
        "updated_at_utc=excluded.updated_at_utc, expires_at_utc=excluded.expires_at_utc"));
    const auto safeText = [](const QString &value) {
        return value.isNull() ? QStringLiteral("") : value;
    };
    query.addBindValue(intent.intentId);
    query.addBindValue(intent.strategyId);
    query.addBindValue(intent.signal.instrument.symbol);
    query.addBindValue(safeText(intent.signal.instrument.name));
    query.addBindValue(static_cast<int>(intent.signal.instrument.market));
    query.addBindValue(safeText(intent.signal.instrument.venue));
    query.addBindValue(intent.signal.timestamp.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(static_cast<int>(intent.signal.side));
    query.addBindValue(intent.signal.price);
    query.addBindValue(intent.signal.volume);
    query.addBindValue(safeText(intent.signal.reason));
    query.addBindValue(static_cast<int>(intent.orderRequest.side));
    query.addBindValue(static_cast<int>(intent.orderRequest.type));
    query.addBindValue(static_cast<int>(intent.orderRequest.timeInForce));
    query.addBindValue(intent.orderRequest.quantity);
    query.addBindValue(intent.orderRequest.limitPrice);
    query.addBindValue(intent.orderRequest.referencePrice);
    query.addBindValue(intent.orderRequest.expiresAt.isValid()
        ? intent.orderRequest.expiresAt.toUTC().toString(Qt::ISODateWithMs) : QString());
    query.addBindValue(static_cast<int>(intent.status));
    query.addBindValue(safeText(intent.orderId));
    query.addBindValue(safeText(intent.message));
    query.addBindValue(intent.createdAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(intent.updatedAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(intent.expiresAt.isValid()
        ? intent.expiresAt.toUTC().toString(Qt::ISODateWithMs) : QString());
    if (!query.exec()) {
        emit writeFailed(QStringLiteral("保存策略执行意图失败：%1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<StrategyExecutionConfig> SqliteLocalDataStore::loadStrategyConfigurations()
{
    QVector<StrategyExecutionConfig> configurations;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "select strategy_id, enabled, execution_mode, order_type, time_in_force, fixed_order_value, "
            "max_position_percent, sell_all, cooldown_seconds, approval_timeout_seconds, limit_offset_bps, "
            "lot_size, order_timeout_seconds, max_orders_per_day, max_daily_order_value "
            "from strategy_execution_configs order by strategy_id"))) {
        return configurations;
    }
    while (query.next()) {
        StrategyExecutionConfig config;
        config.strategyId = query.value(0).toString();
        config.enabled = query.value(1).toBool();
        config.mode = static_cast<StrategyExecutionMode>(query.value(2).toInt());
        config.orderType = static_cast<OrderType>(query.value(3).toInt());
        config.timeInForce = static_cast<TimeInForce>(query.value(4).toInt());
        config.fixedOrderValue = query.value(5).toDouble();
        config.maxPositionPercent = query.value(6).toDouble();
        config.sellAll = query.value(7).toBool();
        config.cooldownSeconds = query.value(8).toInt();
        config.approvalTimeoutSeconds = query.value(9).toInt();
        config.limitOffsetBps = query.value(10).toDouble();
        config.lotSize = query.value(11).toInt();
        config.orderTimeoutSeconds = query.value(12).toInt();
        config.maxOrdersPerDay = query.value(13).toInt();
        config.maxDailyOrderValue = query.value(14).toDouble();
        configurations.append(config);
    }
    return configurations;
}

QVector<StrategyOrderIntent> SqliteLocalDataStore::loadStrategyIntents()
{
    QVector<StrategyOrderIntent> intents;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "select intent_id, strategy_id, symbol, name, market, venue, signal_time_utc, signal_side, "
            "signal_price, signal_volume, signal_reason, order_side, order_type, order_time_in_force, "
            "quantity, limit_price, reference_price, order_expires_at_utc, intent_status, order_id, message, "
            "created_at_utc, updated_at_utc, expires_at_utc "
            "from strategy_order_intents order by created_at_utc asc"))) {
        return intents;
    }
    while (query.next()) {
        StrategyOrderIntent intent;
        intent.intentId = query.value(0).toString();
        intent.strategyId = query.value(1).toString();
        intent.signal.instrument.symbol = query.value(2).toString();
        intent.signal.instrument.name = query.value(3).toString();
        intent.signal.instrument.market = static_cast<Market>(query.value(4).toInt());
        intent.signal.instrument.venue = query.value(5).toString();
        intent.signal.timestamp = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs);
        intent.signal.side = static_cast<SignalSide>(query.value(7).toInt());
        intent.signal.price = query.value(8).toDouble();
        intent.signal.volume = query.value(9).toDouble();
        intent.signal.reason = query.value(10).toString();
        intent.orderRequest.clientOrderId = intent.intentId;
        intent.orderRequest.instrument = intent.signal.instrument;
        intent.orderRequest.side = static_cast<OrderSide>(query.value(11).toInt());
        intent.orderRequest.type = static_cast<OrderType>(query.value(12).toInt());
        intent.orderRequest.timeInForce = static_cast<TimeInForce>(query.value(13).toInt());
        intent.orderRequest.quantity = query.value(14).toDouble();
        intent.orderRequest.limitPrice = query.value(15).toDouble();
        intent.orderRequest.referencePrice = query.value(16).toDouble();
        intent.orderRequest.expiresAt = QDateTime::fromString(query.value(17).toString(), Qt::ISODateWithMs);
        intent.status = static_cast<OrderIntentStatus>(query.value(18).toInt());
        intent.orderId = query.value(19).toString();
        intent.message = query.value(20).toString();
        intent.createdAt = QDateTime::fromString(query.value(21).toString(), Qt::ISODateWithMs);
        intent.updatedAt = QDateTime::fromString(query.value(22).toString(), Qt::ISODateWithMs);
        intent.expiresAt = QDateTime::fromString(query.value(23).toString(), Qt::ISODateWithMs);
        intent.orderRequest.createdAt = intent.createdAt;
        intents.append(intent);
    }
    return intents;
}

bool SqliteLocalDataStore::insertCustomStrategy(
    const CustomStrategyDefinition &definition)
{
    QSqlQuery existing(m_database);
    existing.prepare(QStringLiteral(
        "select definition_json from custom_strategies where strategy_id = ? and version = ?"));
    existing.addBindValue(definition.strategyId);
    existing.addBindValue(definition.version);
    if (!existing.exec()) {
        emitSqlError(QStringLiteral("检查自定义策略版本"));
        return false;
    }
    if (existing.next()) {
        const QJsonDocument stored = QJsonDocument::fromJson(existing.value(0).toByteArray());
        if (stored.isObject() && stored.object() == definition.document) return true;
        emit writeFailed(QStringLiteral("自定义策略 %1 v%2 已存在且内容不同；请增加版本号")
                             .arg(definition.strategyId).arg(definition.version));
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into custom_strategies(strategy_id, version, schema_version, name, description, "
        "author, market, execution_scope, definition_json, created_at_utc, updated_at_utc) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(definition.strategyId);
    query.addBindValue(definition.version);
    query.addBindValue(definition.schemaVersion);
    query.addBindValue(definition.name);
    query.addBindValue(definition.description);
    query.addBindValue(definition.author);
    query.addBindValue(definition.market);
    query.addBindValue(definition.executionScope);
    query.addBindValue(definition.canonicalJson());
    query.addBindValue(definition.createdAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(definition.updatedAt.toUTC().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        emitSqlError(QStringLiteral("保存自定义策略"));
        return false;
    }
    return true;
}

QVector<CustomStrategyDefinition> SqliteLocalDataStore::loadCustomStrategies()
{
    QVector<CustomStrategyDefinition> definitions;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "select strategy_id, version, schema_version, name, description, author, market, "
            "execution_scope, definition_json, created_at_utc, updated_at_utc "
            "from custom_strategies order by strategy_id asc, version desc"))) {
        return definitions;
    }
    while (query.next()) {
        const QJsonDocument document = QJsonDocument::fromJson(query.value(8).toByteArray());
        if (!document.isObject()) continue;
        CustomStrategyDefinition definition;
        definition.strategyId = query.value(0).toString();
        definition.version = query.value(1).toInt();
        definition.schemaVersion = query.value(2).toInt();
        definition.name = query.value(3).toString();
        definition.description = query.value(4).toString();
        definition.author = query.value(5).toString();
        definition.market = query.value(6).toString();
        definition.executionScope = query.value(7).toString();
        definition.document = document.object();
        definition.createdAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODateWithMs);
        definition.updatedAt = QDateTime::fromString(query.value(10).toString(), Qt::ISODateWithMs);
        definitions.append(definition);
    }
    return definitions;
}

bool SqliteLocalDataStore::ensureSchema()
{
    const QStringList statements {
        QStringLiteral(
            "create table if not exists ticks("
            "id integer primary key autoincrement,"
            "symbol text not null,"
            "name text not null,"
            "market text not null,"
            "venue text not null,"
            "timestamp_utc text not null,"
            "batch_id text not null default '',"
            "price real not null,"
            "previous_close real not null default 0,"
            "open real not null default 0,"
            "high real not null default 0,"
            "low real not null default 0,"
            "volume real not null,"
            "amount real not null default 0,"
            "bid real not null,"
            "ask real not null)"),
        QStringLiteral("create index if not exists idx_ticks_symbol_time on ticks(symbol, timestamp_utc)"),
        QStringLiteral(
            "create table if not exists raw_trade_ticks("
            "id integer primary key autoincrement,"
            "symbol text not null, name text not null, market text not null, venue text not null,"
            "trading_date text not null, timestamp_utc text not null, source_line integer not null,"
            "price real not null, volume_shares real not null, amount real not null,"
            "direction integer not null, source text not null, imported_at_utc text not null)"),
        QStringLiteral(
            "create unique index if not exists idx_raw_trade_identity "
            "on raw_trade_ticks(symbol, trading_date, source, source_line)"),
        QStringLiteral(
            "create index if not exists idx_raw_trade_symbol_day_time "
            "on raw_trade_ticks(symbol, trading_date, timestamp_utc, source_line)"),
        QStringLiteral(
            "create table if not exists raw_tick_candle_cache_state("
            "symbol text not null, trading_date text not null, period_seconds integer not null,"
            "raw_count integer not null, raw_max_id integer not null, bar_count integer not null,"
            "generated_at_utc text not null, cache_version integer not null,"
            "primary key(symbol, trading_date, period_seconds))"),
        QStringLiteral(
            "create table if not exists market_snapshots("
            "batch_id text primary key,"
            "market integer not null,"
            "snapshot_time_utc text not null,"
            "completed_at_utc text not null,"
            "expected_count integer not null,"
            "received_count integer not null)"),
        QStringLiteral(
            "create index if not exists idx_market_snapshots_completed_time "
            "on market_snapshots(completed_at_utc, snapshot_time_utc)"),
        QStringLiteral(
            "create table if not exists candles("
            "id integer primary key autoincrement,"
            "symbol text not null,"
            "name text not null,"
            "market text not null,"
            "venue text not null,"
            "start_time_utc text not null,"
            "period_seconds integer not null,"
            "open real not null,"
            "high real not null,"
            "low real not null,"
            "close real not null,"
            "volume real not null,"
            "buy_volume real not null default 0,"
            "sell_volume real not null default 0,"
            "neutral_volume real not null default 0,"
            "directional_volume_known integer not null default 0,"
            "source text not null default 'unknown',"
            "adjustment text not null default 'none',"
            "downloaded_at_utc text)"),
        QStringLiteral("create index if not exists idx_candles_symbol_period_time on candles(symbol, period_seconds, start_time_utc)"),
        QStringLiteral(
            "create table if not exists historical_dataset_versions("
            "symbol text not null, period_seconds integer not null, source text not null, "
            "adjustment text not null, requested_bars integer not null, actual_bars integer not null, "
            "first_time_utc text not null, last_time_utc text not null, "
            "data_version text not null, downloaded_at_utc text not null, "
            "primary key(symbol, period_seconds, source, adjustment))"),
        QStringLiteral(
            "create index if not exists idx_historical_dataset_downloaded "
            "on historical_dataset_versions(downloaded_at_utc desc)"),
        QStringLiteral(
            "create table if not exists signals("
            "id integer primary key autoincrement,"
            "symbol text not null,"
            "name text not null,"
            "market text not null,"
            "venue text not null,"
            "timestamp_utc text not null,"
            "side text not null,"
            "price real not null,"
            "volume real not null,"
            "reason text not null)"),
        QStringLiteral("create index if not exists idx_signals_symbol_time on signals(symbol, timestamp_utc)"),
        QStringLiteral(
            "create table if not exists orders("
            "order_id text primary key, client_order_id text, symbol text not null, name text not null, "
            "market integer not null, venue text not null, side integer not null, order_type integer not null, "
            "time_in_force integer not null default 0, quantity real not null, limit_price real not null, "
            "reference_price real not null, created_at_utc text not null, expires_at_utc text, "
            "status integer not null, filled_quantity real not null, "
            "average_fill_price real not null, message text not null, updated_at_utc text not null)"),
        QStringLiteral("create index if not exists idx_orders_created on orders(created_at_utc)"),
        QStringLiteral(
            "create table if not exists executions("
            "execution_id text primary key, order_id text not null, symbol text not null, name text not null, "
            "market integer not null, venue text not null, side integer not null, quantity real not null, "
            "price real not null, commission real not null, timestamp_utc text not null)"),
        QStringLiteral("create index if not exists idx_executions_time on executions(timestamp_utc)"),
        QStringLiteral(
            "create table if not exists positions("
            "symbol text primary key, name text not null, market integer not null, venue text not null, "
            "quantity real not null, available_quantity real not null, average_cost real not null, "
            "market_price real not null, market_value real not null, unrealized_pnl real not null, "
            "realized_pnl real not null)"),
        QStringLiteral(
            "create table if not exists account_snapshots("
            "id integer primary key autoincrement, timestamp_utc text not null, trading_day text not null default '', "
            "initial_equity real not null, day_start_equity real not null default 0, "
            "cash real not null, frozen_cash real not null, market_value real not null, total_equity real not null, "
            "realized_pnl real not null, unrealized_pnl real not null, total_commission real not null, "
            "daily_pnl real not null default 0)"),
        QStringLiteral("create index if not exists idx_account_snapshots_time on account_snapshots(timestamp_utc)"),
        QStringLiteral(
            "create table if not exists strategy_execution_configs("
            "strategy_id text primary key, enabled integer not null, execution_mode integer not null, "
            "order_type integer not null, time_in_force integer not null default 0, "
            "fixed_order_value real not null, max_position_percent real not null, "
            "sell_all integer not null, cooldown_seconds integer not null, approval_timeout_seconds integer not null, "
            "limit_offset_bps real not null, lot_size integer not null, order_timeout_seconds integer not null default 0, "
            "max_orders_per_day integer not null default 0, max_daily_order_value real not null default 0)"),
        QStringLiteral(
            "create table if not exists strategy_order_intents("
            "intent_id text primary key, strategy_id text not null, symbol text not null, name text not null, "
            "market integer not null, venue text not null, signal_time_utc text not null, signal_side integer not null, "
            "signal_price real not null, signal_volume real not null, signal_reason text not null, "
            "order_side integer not null, order_type integer not null, order_time_in_force integer not null default 0, "
            "quantity real not null, limit_price real not null, reference_price real not null, order_expires_at_utc text, "
            "intent_status integer not null, order_id text not null, message text not null, "
            "created_at_utc text not null, updated_at_utc text not null, expires_at_utc text)"),
        QStringLiteral(
            "create index if not exists idx_strategy_intents_strategy_time "
            "on strategy_order_intents(strategy_id, created_at_utc)"),
        QStringLiteral(
            "create table if not exists custom_strategies("
            "strategy_id text not null, version integer not null, schema_version integer not null, "
            "name text not null, description text not null, author text not null, market text not null, "
            "execution_scope text not null, definition_json text not null, "
            "created_at_utc text not null, updated_at_utc text not null, "
            "primary key(strategy_id, version))"),
        QStringLiteral(
            "create index if not exists idx_custom_strategies_updated "
            "on custom_strategies(updated_at_utc desc)"),
        QStringLiteral(
            "create table if not exists schema_versions("
            "component text primary key, version integer not null, updated_at_utc text not null)"),
        QStringLiteral(
            "create table if not exists app_database_metadata("
            "singleton_id integer primary key check(singleton_id = 1),"
            "database_uuid text not null unique, schema_version integer not null,"
            "created_at_utc text not null, migrated_at_utc text not null,"
            "application_version text not null)"),
        QStringLiteral(
            "create table if not exists schema_migration_history("
            "version integer primary key, migration_name text not null,"
            "applied_at_utc text not null, application_version text not null,"
            "checksum text not null)"),
        QStringLiteral(
            "create table if not exists backtest_results("
            "result_id text primary key, task_id text not null, completed_at_utc text not null, "
            "strategy_id text not null, strategy_version integer not null, "
            "strategy_parameters_json text not null, rule_snapshot_json text not null, "
            "data_version text not null, symbol text not null, name text not null, "
            "market integer not null, venue text not null, frequency text not null, "
            "period_seconds integer not null, requested_bars integer not null, actual_bars integer not null, "
            "from_utc text not null, to_utc text not null, initial_capital real not null, "
            "commission_rate real not null, slippage_rate real not null, lot_size integer not null, "
            "summary_initial_capital real not null, final_equity real not null, "
            "total_return_percent real not null, max_drawdown_percent real not null, "
            "win_rate_percent real not null, sharpe_ratio real not null, total_commission real not null, "
            "total_trades integer not null, winning_trades integer not null, losing_trades integer not null, "
            "dataset_version text not null default '', dataset_identity_json text not null default '', "
            "dataset_raw_tick_count integer not null default 0, "
            "dataset_derived_bar_count integer not null default 0, "
            "dataset_day_count integer not null default 0, "
            "dataset_first_date text not null default '', dataset_last_date text not null default '', "
            "dataset_gap_day_count integer not null default 0, "
            "dataset_stale_day_count integer not null default 0, "
            "dataset_calendar_source text not null default '', "
            "dataset_calendar_coverage_known integer not null default 0)"),
        QStringLiteral(
            "create index if not exists idx_backtest_results_completed "
            "on backtest_results(completed_at_utc desc, result_id desc)"),
        QStringLiteral(
            "create table if not exists backtest_equity_points("
            "result_id text not null, sequence_no integer not null, timestamp_utc text not null, "
            "equity real not null, cash real not null, position_value real not null, "
            "drawdown_percent real not null, primary key(result_id, sequence_no), "
            "foreign key(result_id) references backtest_results(result_id) on delete cascade)"),
        QStringLiteral(
            "create table if not exists backtest_trades("
            "result_id text not null, sequence_no integer not null, symbol text not null, name text not null, "
            "market integer not null, venue text not null, entry_time_utc text not null, exit_time_utc text not null, "
            "entry_price real not null, exit_price real not null, quantity real not null, "
            "gross_pnl real not null, net_pnl real not null, commission real not null, "
            "return_percent real not null, entry_reason text not null, exit_reason text not null, "
            "primary key(result_id, sequence_no), "
            "foreign key(result_id) references backtest_results(result_id) on delete cascade)"),
        QStringLiteral(
            "create table if not exists tickdata_import_runs("
            "run_id text primary key, created_at_utc text not null, completed_at_utc text, "
            "symbol text not null, name text not null, market text not null, venue text not null, "
            "start_date text not null, end_date text not null, format integer not null, "
            "period_seconds integer not null, volume_unit integer not null, total_files integer not null, "
            "completed_files integer not null, succeeded_files integer not null, failed_files integer not null, "
            "missing_files integer not null, cancelled_files integer not null, "
            "quality_warning_files integer not null, valid_records integer not null, "
            "skipped_records integer not null, written_bars integer not null, "
            "status text not null, error text not null, "
            "skipped_existing_files integer not null default 0, "
            "overlap_files integer not null default 0, "
            "excluded_holiday_days integer not null default 0, "
            "calendar_fallback_days integer not null default 0, "
            "calendar_source text not null default '')"),
        QStringLiteral(
            "create index if not exists idx_tickdata_import_runs_created "
            "on tickdata_import_runs(created_at_utc desc, run_id desc)"),
        QStringLiteral(
            "create table if not exists tickdata_import_run_items("
            "run_id text not null, trading_date text not null, status text not null, "
            "file_size integer not null, existing_bars integer not null, expected_bars integer not null, "
            "coverage_status text not null, lines_read integer not null, valid_records integer not null, "
            "skipped_records integer not null, written_bars integer not null, "
            "missing_continuous_bars integer not null, quality_status text not null, "
            "source text not null, error text not null, primary key(run_id, trading_date), "
            "foreign key(run_id) references tickdata_import_runs(run_id) on delete cascade)"),
        QStringLiteral(
            "create index if not exists idx_tickdata_import_run_items_status "
            "on tickdata_import_run_items(run_id, status, trading_date)")
    };

    for (const QString &statement : statements) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            emitSqlError(QStringLiteral("初始化数据库结构"));
            return false;
        }
    }

    if (!ensureColumn(QStringLiteral("ticks"), QStringLiteral("batch_id"),
                      QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("ticks"), QStringLiteral("previous_close"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("ticks"), QStringLiteral("open"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("ticks"), QStringLiteral("high"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("ticks"), QStringLiteral("low"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("ticks"), QStringLiteral("amount"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("candles"), QStringLiteral("source"),
                      QStringLiteral("text not null default 'unknown'"))
        || !ensureColumn(QStringLiteral("candles"), QStringLiteral("adjustment"),
                         QStringLiteral("text not null default 'none'"))
        || !ensureColumn(QStringLiteral("candles"), QStringLiteral("downloaded_at_utc"),
                         QStringLiteral("text"))
        || !ensureColumn(QStringLiteral("candles"), QStringLiteral("buy_volume"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("candles"), QStringLiteral("sell_volume"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("candles"), QStringLiteral("neutral_volume"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("tickdata_import_runs"),
                         QStringLiteral("skipped_existing_files"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("tickdata_import_runs"),
                         QStringLiteral("overlap_files"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("tickdata_import_runs"),
                         QStringLiteral("excluded_holiday_days"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("tickdata_import_runs"),
                         QStringLiteral("calendar_fallback_days"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("tickdata_import_runs"),
                         QStringLiteral("calendar_source"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("candles"), QStringLiteral("directional_volume_known"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("account_snapshots"), QStringLiteral("trading_day"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("account_snapshots"), QStringLiteral("day_start_equity"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("account_snapshots"), QStringLiteral("daily_pnl"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("orders"), QStringLiteral("time_in_force"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("orders"), QStringLiteral("expires_at_utc"), QStringLiteral("text"))
        || !ensureColumn(QStringLiteral("strategy_execution_configs"), QStringLiteral("time_in_force"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("strategy_execution_configs"), QStringLiteral("order_timeout_seconds"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("strategy_execution_configs"), QStringLiteral("max_orders_per_day"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("strategy_execution_configs"), QStringLiteral("max_daily_order_value"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("strategy_order_intents"), QStringLiteral("order_time_in_force"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("strategy_order_intents"), QStringLiteral("order_expires_at_utc"),
                         QStringLiteral("text"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("dataset_version"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("dataset_identity_json"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("dataset_raw_tick_count"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("dataset_derived_bar_count"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("dataset_day_count"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("dataset_first_date"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("dataset_last_date"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("dataset_gap_day_count"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("dataset_stale_day_count"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("dataset_calendar_source"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("dataset_calendar_coverage_known"),
                         QStringLiteral("integer not null default 0"))) {
        return false;
    }

    const QStringList candleMigrationStatements {
        QStringLiteral(
            "create unique index if not exists idx_ticks_batch_symbol "
            "on ticks(batch_id, symbol) where batch_id <> ''"),
        QStringLiteral(
            "delete from candles where id not in ("
            "select max(id) from candles group by symbol, period_seconds, start_time_utc)"),
        QStringLiteral(
            "create unique index if not exists idx_candles_unique_bar "
            "on candles(symbol, period_seconds, start_time_utc)")
    };
    for (const QString &statement : candleMigrationStatements) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            emitSqlError(QStringLiteral("迁移 K 线缓存结构"));
            return false;
        }
    }

    QSqlQuery schemaVersionQuery(m_database);
    schemaVersionQuery.prepare(QStringLiteral(
        "insert into schema_versions(component, version, updated_at_utc) values('backtest_archive', 1, ?) "
        "on conflict(component) do update set version=max(version, excluded.version), "
        "updated_at_utc=excluded.updated_at_utc"));
    schemaVersionQuery.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!schemaVersionQuery.exec()) {
        emitSqlError(QStringLiteral("记录回测归档结构版本"));
        return false;
    }

    return true;
}

bool SqliteLocalDataStore::ensureColumn(const QString &tableName, const QString &columnName, const QString &definition)
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("pragma table_info(%1)").arg(tableName))) {
        emitSqlError(QStringLiteral("检查数据库字段"));
        return false;
    }

    while (query.next()) {
        if (query.value(1).toString() == columnName) {
            return true;
        }
    }

    if (!query.exec(QStringLiteral("alter table %1 add column %2 %3")
                        .arg(tableName, columnName, definition))) {
        emitSqlError(QStringLiteral("新增数据库字段"));
        return false;
    }
    return true;
}

int SqliteLocalDataStore::tableRowCount(const QString &tableName) const
{
    if (!m_database.isOpen()) {
        return 0;
    }

    const QStringList allowedTables {
        QStringLiteral("ticks"),
        QStringLiteral("signals")
    };
    if (!allowedTables.contains(tableName)) {
        return 0;
    }

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("select count(*) from %1").arg(tableName)) || !query.next()) {
        return 0;
    }

    return query.value(0).toInt();
}

void SqliteLocalDataStore::emitSqlError(const QString &operation)
{
    const QString message = QStringLiteral("%1失败：%2")
                                .arg(operation, m_database.lastError().text());
    qWarning().noquote() << message;
    emit writeFailed(message);
}

} // 命名空间 pp
