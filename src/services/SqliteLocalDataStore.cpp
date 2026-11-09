#include "services/SqliteLocalDataStore.h"
#include "services/FactorCandidateGovernance.h"

#include "services/BacktestDataVersion.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>
#include <QCryptographicHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <QStringList>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace pp {

namespace {

constexpr int kCurrentDatabaseSchemaVersion = 25;
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

bool isShanghaiShenzhenSymbol(const QString &symbol)
{
    static const QRegularExpression expression(QStringLiteral(
        "^(?:60[0135689]\\d{3}|68[089]\\d{3}|00[0123]\\d{3}|30[01]\\d{3})$"));
    return expression.match(symbol).hasMatch();
}

QString industryDatasetContentSha256(
    const AShareIndustryDataset &dataset,
    const QVector<AShareIndustryMembership> &memberships)
{
    QVector<AShareIndustryMembership> sorted = memberships;
    std::sort(sorted.begin(), sorted.end(),
              [](const AShareIndustryMembership &left,
                 const AShareIndustryMembership &right) {
        return std::tie(left.instrument.symbol, left.effectiveFrom,
                        left.effectiveTo, left.industryCode,
                        left.industryName)
            < std::tie(right.instrument.symbol, right.effectiveFrom,
                       right.effectiveTo, right.industryCode,
                       right.industryName);
    });
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData("pptrader-ashare-industry-dataset-v1\n");
    const QStringList identity{dataset.classification.trimmed(),
                               dataset.source.trimmed(),
                               dataset.sourceVersion.trimmed()};
    for (const QString &value : identity) {
        const QByteArray bytes = value.toUtf8();
        hash.addData(QByteArray::number(bytes.size()));
        hash.addData(":");
        hash.addData(bytes);
        hash.addData("\n");
    }
    for (const AShareIndustryMembership &membership : sorted) {
        const QStringList fields{
            membership.instrument.symbol.trimmed(),
            membership.instrument.name,
            membership.instrument.venue,
            membership.industryCode.trimmed(),
            membership.industryName.trimmed(),
            membership.effectiveFrom.toString(Qt::ISODate),
            membership.effectiveTo.isValid()
                ? membership.effectiveTo.toString(Qt::ISODate)
                : QStringLiteral("")};
        for (const QString &field : fields) {
            const QByteArray bytes = field.toUtf8();
            hash.addData(QByteArray::number(bytes.size()));
            hash.addData(":");
            hash.addData(bytes);
            hash.addData("|");
        }
        hash.addData("\n");
    }
    return QStringLiteral("sha256:%1")
        .arg(QString::fromLatin1(hash.result().toHex()));
}

bool normalizeIndustryDataset(AShareIndustryDataset *dataset, QString *error)
{
    if (error) error->clear();
    dataset->classification = dataset->classification.trimmed();
    dataset->source = dataset->source.trimmed();
    dataset->sourceVersion = dataset->sourceVersion.trimmed();
    dataset->revisionReason = dataset->revisionReason.trimmed();
    if (dataset->classification.isEmpty() || dataset->source.isEmpty()
        || dataset->sourceVersion.isEmpty() || dataset->memberships.isEmpty()) {
        if (error) *error = QStringLiteral("行业数据集身份或成员为空");
        return false;
    }
    QVector<AShareIndustryMembership> normalized = dataset->memberships;
    for (AShareIndustryMembership &membership : normalized) {
        membership.instrument.symbol = membership.instrument.symbol.trimmed();
        membership.instrument.name = membership.instrument.name.trimmed();
        membership.instrument.venue = membership.instrument.venue.trimmed();
        membership.industryCode = membership.industryCode.trimmed();
        membership.industryName = membership.industryName.trimmed();
        if (!membership.isValid()) {
            if (error) *error = QStringLiteral("行业数据集包含无效成员");
            return false;
        }
    }
    std::sort(normalized.begin(), normalized.end(),
              [](const AShareIndustryMembership &left,
                 const AShareIndustryMembership &right) {
        return std::tie(left.instrument.symbol, left.effectiveFrom,
                        left.effectiveTo, left.industryCode)
            < std::tie(right.instrument.symbol, right.effectiveFrom,
                       right.effectiveTo, right.industryCode);
    });
    for (int index = 1; index < normalized.size(); ++index) {
        const auto &previous = normalized.at(index - 1);
        const auto &current = normalized.at(index);
        if (previous.instrument.symbol == current.instrument.symbol
            && (!previous.effectiveTo.isValid()
                || previous.effectiveTo >= current.effectiveFrom)) {
            if (error) {
                *error = QStringLiteral("行业数据集同一标的存在重叠有效期：%1")
                             .arg(current.instrument.symbol);
            }
            return false;
        }
    }
    dataset->memberships = normalized;
    dataset->recordCount = normalized.size();
    dataset->contentSha256 = industryDatasetContentSha256(*dataset, normalized);
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
    m_readOnly = false;
    m_databasePath = databasePath;
    QDir().mkpath(QFileInfo(databasePath).absolutePath());

    if (!m_database.isValid())
        m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setConnectOptions(QString{});
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
    const bool migrated = migrateSchema();
    return migrated;
}

bool SqliteLocalDataStore::initializeReadOnly(const QString &databasePath)
{
    close();
    m_readOnly = true;
    m_databasePath = databasePath;
    if (databasePath.trimmed().isEmpty() || !QFileInfo::exists(databasePath)) {
        return false;
    }
    if (!m_database.isValid()) {
        m_database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), m_connectionName);
    }
    m_database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    m_database.setDatabaseName(databasePath);
    if (!m_database.open()) {
        emitSqlError(QStringLiteral("只读打开本地数据库"));
        return false;
    }
    for (const QString &statement : {
             QStringLiteral("pragma busy_timeout=5000"),
             QStringLiteral("pragma query_only=ON"),
             QStringLiteral("pragma foreign_keys=ON")}) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            emitSqlError(QStringLiteral("配置 SQLite 只读查询"));
            close();
            return false;
        }
    }
    QSqlQuery version(m_database);
    if (!version.exec(QStringLiteral("pragma user_version")) || !version.next()
        || version.value(0).toInt() != kCurrentDatabaseSchemaVersion) {
        emit writeFailed(QStringLiteral(
            "只读查询拒绝：数据库 schema 不是当前 v%1")
                             .arg(kCurrentDatabaseSchemaVersion));
        close();
        return false;
    }
    QString validationError;
    if (!verifyRequiredTables(&validationError)) {
        emit writeFailed(QStringLiteral("只读数据库校验失败：%1")
                             .arg(validationError));
        close();
        return false;
    }
    QSqlQuery identity(m_database);
    if (!identity.exec(QStringLiteral(
            "select database_uuid, schema_version from app_database_metadata "
            "where singleton_id = 1"))
        || !identity.next()
        || identity.value(1).toInt() != kCurrentDatabaseSchemaVersion) {
        emitSqlError(QStringLiteral("读取只读数据库身份"));
        close();
        return false;
    }
    m_databaseUuid = identity.value(0).toString();
    m_schemaVersion = identity.value(1).toInt();
    return true;
}

bool SqliteLocalDataStore::beginReadSnapshot()
{
    if (!m_readOnly || !m_database.isOpen() || m_readSnapshotActive) {
        return false;
    }
    if (!m_database.transaction()) {
        emitSqlError(QStringLiteral("开始 SQLite 一致读快照"));
        return false;
    }
    m_readSnapshotActive = true;
    return true;
}

bool SqliteLocalDataStore::endReadSnapshot()
{
    if (!m_readSnapshotActive) return false;
    const bool committed = m_database.commit();
    m_readSnapshotActive = false;
    if (!committed) emitSqlError(QStringLiteral("结束 SQLite 一致读快照"));
    return committed;
}

void SqliteLocalDataStore::close()
{
    if (!m_database.isValid() || !m_database.isOpen()) return;
    if (m_readSnapshotActive) {
        m_database.rollback();
        m_readSnapshotActive = false;
    }
    if (m_rawTradeImportActive) rollbackRawTradeTickImport();
    if (!m_readOnly) {
        QSqlQuery checkpoint(m_database);
        checkpoint.exec(QStringLiteral("pragma wal_checkpoint(TRUNCATE)"));
        checkpoint.finish();
    }
    m_database.close();
    m_schemaVersion = 0;
    m_databaseUuid.clear();
    m_readOnly = false;
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
        QStringLiteral("raw_tick_content_fingerprints"),
        QStringLiteral("raw_tick_day_quality_audits"),
        QStringLiteral("candles"), QStringLiteral("market_snapshots"),
        QStringLiteral("historical_dataset_versions"),
        QStringLiteral("ashare_instrument_day_status"),
        QStringLiteral("ashare_instrument_day_import_runs"),
        QStringLiteral("ashare_instrument_day_import_items"),
        QStringLiteral("ashare_industry_datasets"),
        QStringLiteral("ashare_industry_memberships"),
        QStringLiteral("factor_definition_versions"),
        QStringLiteral("factor_candidate_versions"),
        QStringLiteral("factor_candidate_evaluations"),
        QStringLiteral("external_factor_datasets"),
        QStringLiteral("external_factor_observations"),
        QStringLiteral("factor_candidate_dataset_bindings"),
        QStringLiteral("external_factor_research_archives"),
        QStringLiteral("raw_tick_parameter_sensitivity_results"),
        QStringLiteral("backtest_results"), QStringLiteral("backtest_candles"),
        QStringLiteral("backtest_raw_ticks"),
        QStringLiteral("schema_migration_history"),
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
        if (fromVersion < 4) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(4, 'batch_backtest_recovery', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-batch-recovery-v4"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v4 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 5) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(5, 'portfolio_backtest_archive', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-portfolio-archive-v5"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v5 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 6) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(6, 'raw_tick_content_identity', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-raw-tick-content-identity-v6"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v6 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 7) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(7, 'backtest_validation_runs', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-validation-runs-v7"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v7 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 8) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(8, 'cross_sectional_backtest_archive', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-cross-sectional-archive-v8"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v8 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 9) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(9, 'ashare_calendar_status', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-ashare-calendar-status-v9"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v9 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 10) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(10, 'ashare_instrument_day_import_audit', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-ashare-instrument-day-import-v10"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v10 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 11) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(11, 'ashare_instrument_day_revision_audit', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-ashare-instrument-day-revision-v11"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v11 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 12) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(12, 'ashare_industry_dataset', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-ashare-industry-dataset-v12"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v12 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 13) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(13, 'ashare_industry_import_audit', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-ashare-industry-import-v13"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v13 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 14) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(14, 'factor_definition_registry', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-factor-definition-registry-v14"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v14 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 15) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(15, 'factor_candidate_governance', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-factor-candidate-governance-v15"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v15 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 16) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(16, 'authorized_factor_dataset', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-authorized-factor-dataset-v16"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v16 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 17) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(17, 'external_factor_research_archive', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-external-factor-research-v17"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v17 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 18) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(18, 'backtest_candle_snapshot', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-backtest-candle-snapshot-v18"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v18 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 19) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(19, 'raw_tick_backtest_identity', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-raw-tick-backtest-v19"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v19 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 20) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(20, 'backtest_open_position', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-backtest-open-position-v20"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v20 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 21) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(21, 'raw_tick_holdout_validation', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-raw-tick-holdout-v21"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v21 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 22) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(22, 'raw_tick_walk_forward', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-raw-tick-walk-forward-v22"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v22 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 23) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(23, 'raw_tick_parameter_sensitivity', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-raw-tick-sensitivity-v23"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v23 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 24) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(24, 'raw_tick_day_quality_audit', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-raw-tick-day-quality-v24"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v24 迁移历史"));
                return false;
            }
        }
        if (fromVersion < 25) {
            history.prepare(QStringLiteral(
                "insert into schema_migration_history(version, migration_name, applied_at_utc, "
                "application_version, checksum) values(25, 'backtest_requested_sample_window', ?, ?, ?) "
                "on conflict(version) do nothing"));
            history.addBindValue(now);
            history.addBindValue(kApplicationSchemaVersion);
            history.addBindValue(QStringLiteral("pptrader-backtest-sample-window-v25"));
            if (!history.exec()) {
                m_database.rollback();
                emitSqlError(QStringLiteral("记录全库 v25 迁移历史"));
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
    // Full integrity_check walks every page and made a large, already-current
    // development database block the GUI before QML could create its window.
    // Keep the deep check on migration (the risky path), while current-schema
    // startup performs the required-table contract check; callers can still
    // invoke verifyIntegrity() explicitly for an on-demand audit.
    const bool valid = needsMigration
        ? verifyIntegrity(&validationError)
        : verifyRequiredTables(&validationError);
    if (!valid) {
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
    m_rawTradeImportInstrument = instrument;
    m_rawTradeImportDate = tradingDate;
    m_rawTradeImportSource = source;
    m_rawTradeImportAppendedCount = 0;
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
        ++m_rawTradeImportAppendedCount;
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
    const RawTradeFingerprint fingerprint = rawTradeFingerprint(
        m_rawTradeImportInstrument, m_rawTradeImportDate);
    bool identityReady = fingerprint.count >= m_rawTradeImportAppendedCount;
    if (fingerprint.count > 0) {
        identityReady = identityReady
            && fingerprint.contentSha256.startsWith(QStringLiteral("sha256:"))
            && fingerprint.contentSha256.size() == 71;
    } else {
        QSqlQuery removeEmptyFingerprint(m_database);
        removeEmptyFingerprint.prepare(QStringLiteral(
            "delete from raw_tick_content_fingerprints where symbol = ? and trading_date = ?"));
        removeEmptyFingerprint.addBindValue(m_rawTradeImportInstrument.symbol);
        removeEmptyFingerprint.addBindValue(m_rawTradeImportDate.toString(Qt::ISODate));
        identityReady = identityReady && removeEmptyFingerprint.exec();
    }
    if (!identityReady) {
        m_database.rollback();
        m_rawTradeImportActive = false;
        m_rawTradeImportInstrument = {};
        m_rawTradeImportDate = {};
        m_rawTradeImportSource.clear();
        m_rawTradeImportAppendedCount = 0;
        emit writeFailed(QStringLiteral("原始逐笔内容身份预热失败，导入事务已回滚"));
        return false;
    }
    const bool succeeded = m_database.commit();
    m_rawTradeImportActive = false;
    m_rawTradeImportInstrument = {};
    m_rawTradeImportDate = {};
    m_rawTradeImportSource.clear();
    m_rawTradeImportAppendedCount = 0;
    if (!succeeded) emitSqlError(QStringLiteral("提交原始逐笔导入事务"));
    return succeeded;
}

bool SqliteLocalDataStore::commitRawTradeTickImport(
    const TickDataImportResult &auditResult)
{
    if (!m_rawTradeImportActive || !auditResult.succeeded
        || auditResult.cancelled || auditResult.validRecords <= 0
        || auditResult.validRecords != m_rawTradeImportAppendedCount
        || auditResult.linesRead < auditResult.validRecords
        || auditResult.skippedRecords < 0
        || auditResult.outOfOrderRecords < 0
        || auditResult.qualityStatus.trimmed().isEmpty()) {
        rollbackRawTradeTickImport();
        emit writeFailed(QStringLiteral("原始逐笔逐日质量审计无效，导入事务已回滚"));
        return false;
    }

    const RawTradeFingerprint fingerprint = rawTradeFingerprint(
        m_rawTradeImportInstrument, m_rawTradeImportDate);
    if (fingerprint.count != m_rawTradeImportAppendedCount
        || !isSha256(fingerprint.contentSha256)) {
        rollbackRawTradeTickImport();
        emit writeFailed(QStringLiteral("原始逐笔内容身份与逐日质量审计不一致，导入事务已回滚"));
        return false;
    }

    QSqlQuery audit(m_database);
    audit.prepare(QStringLiteral(
        "insert into raw_tick_day_quality_audits("
        "symbol, trading_date, raw_content_sha256, audit_version, lines_read, "
        "valid_records, skipped_records, out_of_order_records, quality_status, "
        "source, audited_at_utc) values(?, ?, ?, 2, ?, ?, ?, ?, ?, ?, ?) "
        "on conflict(symbol, trading_date, raw_content_sha256, audit_version) do nothing"));
    audit.addBindValue(m_rawTradeImportInstrument.symbol);
    audit.addBindValue(m_rawTradeImportDate.toString(Qt::ISODate));
    audit.addBindValue(fingerprint.contentSha256);
    audit.addBindValue(auditResult.linesRead);
    audit.addBindValue(auditResult.validRecords);
    audit.addBindValue(auditResult.skippedRecords);
    audit.addBindValue(auditResult.outOfOrderRecords);
    audit.addBindValue(auditResult.qualityStatus.trimmed());
    audit.addBindValue(m_rawTradeImportSource);
    audit.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!audit.exec()) {
        rollbackRawTradeTickImport();
        emitSqlError(QStringLiteral("写入原始 Tick 逐日质量审计"));
        return false;
    }

    const bool succeeded = m_database.commit();
    m_rawTradeImportActive = false;
    m_rawTradeImportInstrument = {};
    m_rawTradeImportDate = {};
    m_rawTradeImportSource.clear();
    m_rawTradeImportAppendedCount = 0;
    if (!succeeded) emitSqlError(QStringLiteral("提交原始逐笔导入与质量审计事务"));
    return succeeded;
}

void SqliteLocalDataStore::rollbackRawTradeTickImport()
{
    if (!m_rawTradeImportActive) return;
    m_database.rollback();
    m_rawTradeImportActive = false;
    m_rawTradeImportInstrument = {};
    m_rawTradeImportDate = {};
    m_rawTradeImportSource.clear();
    m_rawTradeImportAppendedCount = 0;
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
    if (fingerprint.count > 0) {
        fingerprint.contentSha256 = loadOrComputeRawTradeContentSha256(
            instrument.symbol, tradingDate, fingerprint.count, fingerprint.maximumId);
    }
    return fingerprint;
}

QString SqliteLocalDataStore::loadOrComputeRawTradeContentSha256(
    const QString &symbol, const QDate &tradingDate,
    qint64 rawCount, qint64 rawMaximumId)
{
    if (symbol.isEmpty() || !tradingDate.isValid() || rawCount <= 0) return {};
    const QString dateText = tradingDate.toString(Qt::ISODate);
    QSqlQuery cached(m_database);
    cached.prepare(QStringLiteral(
        "select raw_count, raw_max_id, content_sha256, dirty "
        "from raw_tick_content_fingerprints where symbol = ? and trading_date = ?"));
    cached.addBindValue(symbol);
    cached.addBindValue(dateText);
    if (cached.exec() && cached.next()
        && cached.value(0).toLongLong() == rawCount
        && cached.value(1).toLongLong() == rawMaximumId
        && cached.value(3).toInt() == 0) {
        const QString digest = cached.value(2).toString();
        if (isSha256(digest)) return digest;
    }
    cached.finish();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData("pptrader-raw-tick-content-v1\n");
    QSqlQuery facts(m_database);
    facts.setForwardOnly(true);
    facts.prepare(QStringLiteral(
        "select timestamp_utc, source_line, price, volume_shares, amount, direction, source "
        "from raw_trade_ticks where symbol = ? and trading_date = ? "
        "order by timestamp_utc asc, source asc, source_line asc, id asc"));
    facts.addBindValue(symbol);
    facts.addBindValue(dateText);
    if (!facts.exec()) {
        emitSqlError(QStringLiteral("计算原始逐笔内容摘要"));
        return {};
    }
    qint64 observedCount = 0;
    while (facts.next()) {
        const QDateTime timestamp = QDateTime::fromString(
            facts.value(0).toString(), Qt::ISODateWithMs);
        const QByteArray source = facts.value(6).toString().toUtf8();
        hash.addData(QByteArray::number(timestamp.toUTC().toMSecsSinceEpoch()));
        hash.addData(",");
        hash.addData(QByteArray::number(facts.value(1).toLongLong()));
        hash.addData(",");
        hash.addData(QByteArray::number(facts.value(2).toDouble(), 'g', 17));
        hash.addData(",");
        hash.addData(QByteArray::number(facts.value(3).toDouble(), 'g', 17));
        hash.addData(",");
        hash.addData(QByteArray::number(facts.value(4).toDouble(), 'g', 17));
        hash.addData(",");
        hash.addData(QByteArray::number(facts.value(5).toInt()));
        hash.addData(",");
        hash.addData(QByteArray::number(source.size()));
        hash.addData(":");
        hash.addData(source);
        hash.addData("\n");
        ++observedCount;
    }
    if (observedCount != rawCount) return {};
    const QString digest = QStringLiteral("sha256:%1")
        .arg(QString::fromLatin1(hash.result().toHex()));
    QSqlQuery store(m_database);
    store.prepare(QStringLiteral(
        "insert into raw_tick_content_fingerprints(symbol, trading_date, raw_count, raw_max_id, "
        "content_sha256, dirty, computed_at_utc) values(?, ?, ?, ?, ?, 0, ?) "
        "on conflict(symbol, trading_date) do update set raw_count=excluded.raw_count, "
        "raw_max_id=excluded.raw_max_id, content_sha256=excluded.content_sha256, dirty=0, "
        "computed_at_utc=excluded.computed_at_utc"));
    store.addBindValue(symbol);
    store.addBindValue(dateText);
    store.addBindValue(rawCount);
    store.addBindValue(rawMaximumId);
    store.addBindValue(digest);
    store.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!store.exec()) {
        emitSqlError(QStringLiteral("缓存原始逐笔内容摘要"));
        return {};
    }
    return digest;
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
        "select raw_count, raw_max_id, raw_content_sha256, cache_version "
        "from raw_tick_candle_cache_state "
        "where symbol = ? and trading_date = ? and period_seconds = ?"));
    query.addBindValue(instrument.symbol);
    query.addBindValue(tradingDate.toString(Qt::ISODate));
    query.addBindValue(periodSeconds);
    return query.exec() && query.next()
        && query.value(0).toLongLong() == fingerprint.count
        && !fingerprint.contentSha256.isEmpty()
        && query.value(2).toString() == fingerprint.contentSha256
        && query.value(3).toInt() == 1;
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
        "raw_max_id, bar_count, raw_content_sha256, generated_at_utc, cache_version) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, 1) "
        "on conflict(symbol, trading_date, period_seconds) do update set raw_count=excluded.raw_count, "
        "raw_max_id=excluded.raw_max_id, bar_count=excluded.bar_count, "
        "raw_content_sha256=excluded.raw_content_sha256, "
        "generated_at_utc=excluded.generated_at_utc, cache_version=excluded.cache_version"));
    state.addBindValue(instrument.symbol);
    state.addBindValue(tradingDate.toString(Qt::ISODate));
    state.addBindValue(periodSeconds);
    state.addBindValue(fingerprint.count);
    state.addBindValue(fingerprint.maximumId);
    state.addBindValue(candles.size());
    state.addBindValue(fingerprint.contentSha256.isEmpty()
                           ? QStringLiteral("") : fingerprint.contentSha256);
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

QVector<Candle> SqliteLocalDataStore::loadCandlesFromSource(
    const Instrument &instrument, int periodSeconds,
    const QDateTime &from, const QDateTime &to, const QString &source)
{
    QVector<Candle> candles;
    const QString normalizedSource = source.trimmed();
    if (instrument.symbol.trimmed().isEmpty() || periodSeconds <= 0
        || !from.isValid() || !to.isValid() || from > to
        || normalizedSource.isEmpty()) {
        return candles;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select start_time_utc, open, high, low, close, volume, buy_volume, sell_volume, neutral_volume, "
        "directional_volume_known, source, adjustment, downloaded_at_utc "
        "from candles where symbol = ? and period_seconds = ? and source = ? "
        "and close > 0 and start_time_utc >= ? and start_time_utc <= ? "
        "order by start_time_utc asc"));
    query.addBindValue(instrument.symbol);
    query.addBindValue(periodSeconds);
    query.addBindValue(normalizedSource);
    query.addBindValue(from.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(to.toUTC().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        emitSqlError(QStringLiteral("按来源和日期范围读取 K 线"));
        return candles;
    }
    while (query.next()) {
        candles.append(candleFromQuery(query, instrument, periodSeconds));
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

QVector<Instrument> SqliteLocalDataStore::rawBacktestInstruments(
    int maximumSymbols, const std::function<bool()> &cancelled)
{
    QVector<Instrument> result;
    QStringList symbols;
    QSqlQuery symbolsQuery(m_database);
    QString symbolStatement = QStringLiteral(
        "select distinct symbol from raw_tick_content_fingerprints "
        "where raw_count > 0 order by symbol asc");
    if (maximumSymbols > 0) symbolStatement += QStringLiteral(" limit ?");
    symbolsQuery.prepare(symbolStatement);
    if (maximumSymbols > 0) symbolsQuery.addBindValue(maximumSymbols);
    if (!symbolsQuery.exec()) {
        emitSqlError(QStringLiteral("读取原始 Tick 回测标的清单"));
        return result;
    }
    while (symbolsQuery.next()) {
        if (cancelled && cancelled()) return {};
        const QString symbol = symbolsQuery.value(0).toString().trimmed();
        if (!symbol.isEmpty()) symbols.append(symbol);
    }
    symbolsQuery.finish();

    QSqlQuery metadata(m_database);
    metadata.prepare(QStringLiteral(
        "select name, market, venue from raw_trade_ticks where symbol = ? "
        "order by trading_date desc, timestamp_utc desc, source_line desc limit 1"));
    result.reserve(symbols.size());
    for (const QString &symbol : symbols) {
        if (cancelled && cancelled()) return {};
        metadata.bindValue(0, symbol);
        if (!metadata.exec()) {
            emitSqlError(QStringLiteral("读取原始 Tick 回测标的元数据"));
            return {};
        }
        if (metadata.next()) {
            Instrument instrument;
            instrument.symbol = symbol;
            instrument.name = metadata.value(0).toString();
            instrument.market = marketFromCode(metadata.value(1).toString(), Market::AShare);
            instrument.venue = metadata.value(2).toString();
            result.append(instrument);
        }
        metadata.finish();
    }
    return result;
}

QVector<BacktestDatasetIdentity> SqliteLocalDataStore::loadBacktestDatasetIdentities()
{
    QVector<BacktestDatasetIdentity> result;
    QHash<QString, QVector<RawTickDayFingerprint>> daysBySymbol;
    QHash<QString, Instrument> instrumentsBySymbol;
    QSet<QString> unresolvedDayKeys;
    QSet<QString> knownDayKeys;
    QVector<QPair<QString, QDate>> unresolvedDays;
    const auto dayKey = [](const QString &symbol, const QDate &date) {
        return QStringLiteral("%1|%2").arg(symbol, date.toString(Qt::ISODate));
    };
    const auto rememberUnresolved = [&](const QString &symbol, const QDate &date) {
        const QString key = dayKey(symbol, date);
        knownDayKeys.insert(key);
        if (unresolvedDayKeys.contains(key)) return;
        unresolvedDayKeys.insert(key);
        unresolvedDays.append(qMakePair(symbol, date));
    };

    // 目录刷新在 Controller 主线程执行。完整导入已预热强指纹，正常路径
    // 只读逐日元数据；仅 dirty/缺失指纹的日期回退计数，绝不在此计算 SHA。
    QSqlQuery fingerprints(m_database);
    if (!fingerprints.exec(QStringLiteral(
            "select symbol, trading_date, raw_count, raw_max_id, content_sha256, dirty "
            "from raw_tick_content_fingerprints order by symbol asc, trading_date asc"))) {
        emitSqlError(QStringLiteral("读取原始逐笔内容摘要缓存"));
        return result;
    }
    while (fingerprints.next()) {
        const QString symbol = fingerprints.value(0).toString();
        const QDate tradingDate = QDate::fromString(
            fingerprints.value(1).toString(), Qt::ISODate);
        if (symbol.isEmpty() || !tradingDate.isValid()) continue;
        const QString key = dayKey(symbol, tradingDate);
        knownDayKeys.insert(key);
        RawTickDayFingerprint day;
        day.tradingDate = tradingDate;
        day.rawCount = fingerprints.value(2).toLongLong();
        day.rawMaximumId = fingerprints.value(3).toLongLong();
        day.rawContentSha256 = fingerprints.value(4).toString();
        if (fingerprints.value(5).toInt() != 0 || day.rawCount <= 0
            || day.rawMaximumId <= 0 || !isSha256(day.rawContentSha256)) {
            rememberUnresolved(symbol, tradingDate);
            continue;
        }
        daysBySymbol[symbol].append(day);
    }
    fingerprints.finish();

    QHash<QString, QHash<QDate, RawTickDayFingerprint>> cacheByKey;
    QSet<QString> catalogSymbols;
    QSqlQuery caches(m_database);
    if (!caches.exec(QStringLiteral(
            "select symbol, period_seconds, trading_date, raw_count, raw_max_id, "
            "bar_count, cache_version, raw_content_sha256 "
            "from raw_tick_candle_cache_state order by symbol asc, period_seconds asc, "
            "trading_date asc"))) {
        emitSqlError(QStringLiteral("读取逐笔派生缓存状态"));
        return result;
    }
    while (caches.next()) {
        const QString symbol = caches.value(0).toString();
        const QString key = QStringLiteral("%1|%2")
            .arg(symbol, caches.value(1).toString());
        const QDate tradingDate = QDate::fromString(caches.value(2).toString(), Qt::ISODate);
        if (symbol.isEmpty() || !tradingDate.isValid()) continue;
        catalogSymbols.insert(symbol);
        if (!knownDayKeys.contains(dayKey(symbol, tradingDate))) {
            rememberUnresolved(symbol, tradingDate);
        }
        RawTickDayFingerprint cached;
        cached.tradingDate = tradingDate;
        cached.rawCount = caches.value(3).toLongLong();
        cached.rawMaximumId = caches.value(4).toLongLong();
        cached.barCount = caches.value(5).toInt();
        cached.cacheVersion = caches.value(6).toInt();
        cached.rawContentSha256 = caches.value(7).toString();
        cacheByKey[key].insert(tradingDate, cached);
    }
    caches.finish();

    // dirty 日的 count/max 必须以事实为准，但只扫该日；内容 SHA 由后台准备服务重算。
    QSqlQuery dayFacts(m_database);
    dayFacts.prepare(QStringLiteral(
        "select count(*), coalesce(max(id), 0) from raw_trade_ticks "
        "where symbol = ? and trading_date = ?"));
    for (const auto &[symbol, tradingDate] : unresolvedDays) {
        dayFacts.bindValue(0, symbol);
        dayFacts.bindValue(1, tradingDate.toString(Qt::ISODate));
        if (!dayFacts.exec() || !dayFacts.next()) {
            emitSqlError(QStringLiteral("读取待更新逐笔数据集指纹"));
            return result;
        }
        RawTickDayFingerprint day;
        day.tradingDate = tradingDate;
        day.rawCount = dayFacts.value(0).toLongLong();
        day.rawMaximumId = dayFacts.value(1).toLongLong();
        dayFacts.finish();
        if (day.rawCount > 0) daysBySymbol[symbol].append(day);
    }

    // 仪器元数据只需每个目录标的索引定位一条，不再 max/group by 全量事实。
    QStringList symbols = catalogSymbols.values();
    std::sort(symbols.begin(), symbols.end());
    QSqlQuery metadata(m_database);
    metadata.prepare(QStringLiteral(
        "select name, market, venue from raw_trade_ticks where symbol = ? "
        "order by trading_date desc, timestamp_utc desc, source_line desc limit 1"));
    for (const QString &symbol : symbols) {
        metadata.bindValue(0, symbol);
        if (!metadata.exec()) {
            emitSqlError(QStringLiteral("读取逐笔标的元数据"));
            return result;
        }
        if (metadata.next()) {
            Instrument instrument;
            instrument.symbol = symbol;
            instrument.name = metadata.value(0).toString();
            instrument.market = marketFromCode(metadata.value(1).toString(), Market::AShare);
            instrument.venue = metadata.value(2).toString();
            instrumentsBySymbol.insert(symbol, instrument);
        }
        metadata.finish();
    }

    for (auto it = daysBySymbol.begin(); it != daysBySymbol.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
                  [](const RawTickDayFingerprint &left,
                     const RawTickDayFingerprint &right) {
                      return left.tradingDate < right.tradingDate;
                  });
    }

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
        if (identity.days.isEmpty()) continue;
        bool allContentDigestsAvailable = !identity.days.isEmpty();
        const QHash<QDate, RawTickDayFingerprint> &cacheByDate = cacheByKey.value(key);
        for (RawTickDayFingerprint &day : identity.days) {
            const bool strongIdentityClean = !unresolvedDayKeys.contains(
                dayKey(symbol, day.tradingDate)) && isSha256(day.rawContentSha256);
            allContentDigestsAvailable = allContentDigestsAvailable
                && strongIdentityClean;
            const auto match = cacheByDate.constFind(day.tradingDate);
            if (match != cacheByDate.constEnd()) {
                day.barCount = match->barCount;
                day.cacheVersion = match->cacheVersion;
                const bool factsMatch = strongIdentityClean
                    && match->rawContentSha256 == day.rawContentSha256;
                day.cacheFresh = factsMatch && match->rawCount == day.rawCount
                    && match->cacheVersion == 1;
                if (!day.cacheFresh) {
                    ++identity.staleDayCount;
                }
            } else {
                ++identity.staleDayCount;
            }
        }
        identity.firstTradingDate = identity.days.first().tradingDate;
        identity.lastTradingDate = identity.days.last().tradingDate;
        identity.fingerprintVersion = allContentDigestsAvailable ? 2 : 1;
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
    identity.fingerprintVersion = 2;

    // v6 起的触发器保证原始 Tick 变更时指纹必然置 dirty；完整导入提交前
    // 已把 count/max/SHA-256 一次性预热为干净行。因此正常回测路径可以直接使用
    // 该强身份，避免每次请求都重新扫描整个逐笔索引。dirty 行仍回退到事实表。
    QSqlQuery fingerprints(m_database);
    fingerprints.prepare(QStringLiteral(
        "select trading_date, raw_count, raw_max_id, content_sha256, dirty "
        "from raw_tick_content_fingerprints where symbol = ? order by trading_date asc"));
    fingerprints.addBindValue(instrument.symbol);
    if (!fingerprints.exec()) {
        emitSqlError(QStringLiteral("读取原始逐笔内容指纹"));
        return std::nullopt;
    }

    QSqlQuery dayFacts(m_database);
    dayFacts.prepare(QStringLiteral(
        "select count(*), coalesce(max(id), 0) from raw_trade_ticks "
        "where symbol = ? and trading_date = ?"));
    while (fingerprints.next()) {
        const QDate tradingDate = QDate::fromString(
            fingerprints.value(0).toString(), Qt::ISODate);
        if (!tradingDate.isValid()) continue;

        RawTickDayFingerprint day;
        day.tradingDate = tradingDate;
        day.rawCount = fingerprints.value(1).toLongLong();
        day.rawMaximumId = fingerprints.value(2).toLongLong();
        day.rawContentSha256 = fingerprints.value(3).toString();
        const bool clean = fingerprints.value(4).toInt() == 0
            && day.rawCount > 0 && day.rawMaximumId > 0
            && isSha256(day.rawContentSha256);
        if (!clean) {
            dayFacts.bindValue(0, instrument.symbol);
            dayFacts.bindValue(1, tradingDate.toString(Qt::ISODate));
            if (!dayFacts.exec() || !dayFacts.next()) {
                emitSqlError(QStringLiteral("读取原始逐笔数据集指纹"));
                return std::nullopt;
            }
            day.rawCount = dayFacts.value(0).toLongLong();
            day.rawMaximumId = dayFacts.value(1).toLongLong();
            dayFacts.finish();
            if (day.rawCount <= 0) continue;
            day.rawContentSha256 = loadOrComputeRawTradeContentSha256(
                instrument.symbol, day.tradingDate,
                day.rawCount, day.rawMaximumId);
            if (day.rawContentSha256.isEmpty()) return std::nullopt;
        }
        identity.days.append(day);
    }
    fingerprints.finish();

    // v6 之前已有事实的旧库可能整库没有指纹行；首次读取保留完整回填路径。
    if (identity.days.isEmpty()) {
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
            const QDate tradingDate = QDate::fromString(
                facts.value(0).toString(), Qt::ISODate);
            if (!tradingDate.isValid()) continue;
            RawTickDayFingerprint day;
            day.tradingDate = tradingDate;
            day.rawCount = facts.value(1).toLongLong();
            day.rawMaximumId = facts.value(2).toLongLong();
            day.rawContentSha256 = loadOrComputeRawTradeContentSha256(
                instrument.symbol, day.tradingDate,
                day.rawCount, day.rawMaximumId);
            if (day.rawContentSha256.isEmpty()) return std::nullopt;
            identity.days.append(day);
        }
        facts.finish();
    }
    if (identity.days.isEmpty()) return std::nullopt;

    identity.firstTradingDate = identity.days.first().tradingDate;
    identity.lastTradingDate = identity.days.last().tradingDate;

    QHash<QDate, RawTickDayFingerprint> cacheByDate;
    QSqlQuery caches(m_database);
    caches.prepare(QStringLiteral(
        "select trading_date, raw_count, raw_max_id, bar_count, cache_version, "
        "raw_content_sha256 "
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
        cached.rawContentSha256 = caches.value(5).toString();
        cacheByDate.insert(tradingDate, cached);
    }
    caches.finish();

    for (RawTickDayFingerprint &day : identity.days) {
        const auto match = cacheByDate.constFind(day.tradingDate);
        if (match != cacheByDate.constEnd()) {
            day.barCount = match->barCount;
            day.cacheVersion = match->cacheVersion;
            const bool contentMatches = !day.rawContentSha256.isEmpty()
                && match->rawContentSha256 == day.rawContentSha256;
            day.cacheFresh = match->rawCount == day.rawCount
                && contentMatches && match->cacheVersion == 1;
            if (!day.cacheFresh) {
                ++identity.staleDayCount;
            }
        } else {
            ++identity.staleDayCount;
        }
    }
    identity.datasetVersion = computeBacktestDatasetVersion(identity);
    return identity;
}

std::optional<BacktestDatasetIdentity>
SqliteLocalDataStore::loadRawBacktestDatasetIdentity(const Instrument &instrument)
{
    auto identity = loadBacktestDatasetIdentity(instrument, 1);
    if (!identity) return std::nullopt;
    identity->periodSeconds = 0;
    identity->source = QStringLiteral("raw_trade_ticks_v1");
    identity->aggregatorVersion = 0;
    identity->staleDayCount = 0;
    for (RawTickDayFingerprint &day : identity->days) {
        day.barCount = 0;
        day.cacheVersion = 0;
        day.cacheFresh = true;
    }
    identity->datasetVersion = computeBacktestDatasetVersion(*identity);
    return identity;
}

bool SqliteLocalDataStore::upsertAShareInstrumentDayRecords(
    const QVector<AShareInstrumentDayRecord> &records)
{
    if (records.isEmpty()) return true;
    for (const AShareInstrumentDayRecord &record : records) {
        if (!record.isValid()) {
            emit writeFailed(QStringLiteral(
                "A 股停复牌事实无效：必须包含代码、交易日、明确状态、来源和来源版本"));
            return false;
        }
    }
    if (!m_database.transaction()) {
        emitSqlError(QStringLiteral("开始写入 A 股停复牌事实事务"));
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into ashare_instrument_day_status("
        "symbol, name, venue, trading_date, state, reason, source, source_version, "
        "updated_at_utc) values(?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "on conflict(symbol, trading_date) do update set "
        "name=excluded.name, venue=excluded.venue, state=excluded.state, "
        "reason=excluded.reason, source=excluded.source, "
        "source_version=excluded.source_version, updated_at_utc=excluded.updated_at_utc"));
    const QString updatedAt = QDateTime::currentDateTimeUtc()
        .toString(Qt::ISODateWithMs);
    for (const AShareInstrumentDayRecord &record : records) {
        query.bindValue(0, record.instrument.symbol.trimmed());
        query.bindValue(1, record.instrument.name.isNull()
                               ? QStringLiteral("") : record.instrument.name);
        query.bindValue(2, record.instrument.venue.isNull()
                               ? QStringLiteral("") : record.instrument.venue);
        query.bindValue(3, record.tradingDate.toString(Qt::ISODate));
        query.bindValue(4, static_cast<int>(record.state));
        query.bindValue(5, record.reason.isNull()
                               ? QStringLiteral("") : record.reason);
        query.bindValue(6, record.source.trimmed());
        query.bindValue(7, record.sourceVersion.trimmed());
        query.bindValue(8, updatedAt);
        if (!query.exec()) {
            const QString sqlError = query.lastError().text();
            m_database.rollback();
            const QString message = QStringLiteral("写入 A 股停复牌事实失败：%1")
                                        .arg(sqlError);
            qWarning().noquote() << message;
            emit writeFailed(message);
            return false;
        }
        query.finish();
    }
    if (!m_database.commit()) {
        m_database.rollback();
        emitSqlError(QStringLiteral("提交 A 股停复牌事实事务"));
        return false;
    }
    return true;
}

AShareInstrumentDayImportChangeSummary
SqliteLocalDataStore::analyzeAShareInstrumentDayImport(
    const QVector<AShareInstrumentDayRecord> &records)
{
    AShareInstrumentDayImportChangeSummary summary;
    if (records.isEmpty()) {
        summary.error = QStringLiteral("停复牌事实为空");
        return summary;
    }
    QSqlQuery existing(m_database);
    existing.prepare(QStringLiteral(
        "select name, venue, state, reason, source from ashare_instrument_day_status "
        "where symbol=? and trading_date=?"));
    for (const AShareInstrumentDayRecord &record : records) {
        if (!record.isValid()) {
            summary.error = QStringLiteral("停复牌事实包含无效记录");
            return summary;
        }
        existing.bindValue(0, record.instrument.symbol.trimmed());
        existing.bindValue(1, record.tradingDate.toString(Qt::ISODate));
        if (!existing.exec()) {
            summary.error = QStringLiteral("读取既有停复牌事实失败：%1")
                                .arg(existing.lastError().text());
            return summary;
        }
        if (!existing.next()) {
            ++summary.newRecords;
        } else {
            const bool sameFact = existing.value(0).toString() == record.instrument.name
                && existing.value(1).toString() == record.instrument.venue
                && existing.value(2).toInt() == static_cast<int>(record.state)
                && existing.value(3).toString() == record.reason
                && existing.value(4).toString() == record.source;
            if (sameFact) ++summary.unchangedRecords;
            else ++summary.correctedRecords;
        }
        existing.finish();
    }
    summary.valid = true;
    return summary;
}

bool SqliteLocalDataStore::importAShareInstrumentDayRecords(
    AShareInstrumentDayImportRun &run,
    const QVector<AShareInstrumentDayRecord> &records,
    bool *reused, QString *error)
{
    if (reused) *reused = false;
    if (error) error->clear();
    const auto failImport = [this, error](const QString &message) {
        if (error) *error = message;
        emit writeFailed(message);
        return false;
    };
    if (!run.isValid() || records.isEmpty()
        || run.recordCount != records.size()) {
        return failImport(QStringLiteral("A 股停复牌导入批次身份无效"));
    }
    for (const AShareInstrumentDayRecord &record : records) {
        if (!record.isValid() || record.source != run.source
            || record.sourceVersion != run.sourceVersion) {
            return failImport(QStringLiteral("A 股停复牌事实与导入批次身份不一致"));
        }
    }
    if (!m_database.transaction()) {
        emitSqlError(QStringLiteral("开始 A 股停复牌可审计导入事务"));
        return false;
    }

    QSqlQuery existing(m_database);
    existing.prepare(QStringLiteral(
        "select run_id from ashare_instrument_day_import_runs "
        "where source=? and source_version=? and content_sha256=?"));
    existing.addBindValue(run.source);
    existing.addBindValue(run.sourceVersion);
    existing.addBindValue(run.contentSha256);
    if (!existing.exec()) {
        m_database.rollback();
        emitSqlError(QStringLiteral("检查 A 股停复牌重复导入"));
        return false;
    }
    if (existing.next()) {
        run.runId = existing.value(0).toString();
        if (!m_database.commit()) {
            m_database.rollback();
            emitSqlError(QStringLiteral("结束 A 股停复牌重复导入检查"));
            return false;
        }
        if (reused) *reused = true;
        return true;
    }

    const AShareInstrumentDayImportChangeSummary changes =
        analyzeAShareInstrumentDayImport(records);
    if (!changes.valid) {
        m_database.rollback();
        return failImport(changes.error);
    }
    if (changes.correctedRecords > 0 && run.revisionReason.trimmed().isEmpty()) {
        m_database.rollback();
        return failImport(QStringLiteral(
            "检测到 %1 条既有事实发生变化；必须填写修订原因后再确认导入")
                              .arg(changes.correctedRecords));
    }
    run.newRecords = changes.newRecords;
    run.unchangedRecords = changes.unchangedRecords;
    run.correctedRecords = changes.correctedRecords;

    QSqlQuery batch(m_database);
    batch.prepare(QStringLiteral(
        "insert into ashare_instrument_day_import_runs("
        "run_id, imported_at_utc, file_name, source, source_version, "
        "content_sha256, lines_read, record_count, first_date, last_date, "
        "new_record_count, unchanged_record_count, corrected_record_count, "
        "revision_reason, status, error) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    batch.addBindValue(run.runId);
    batch.addBindValue(run.importedAt.toUTC().toString(Qt::ISODateWithMs));
    batch.addBindValue(run.fileName);
    batch.addBindValue(run.source);
    batch.addBindValue(run.sourceVersion);
    batch.addBindValue(run.contentSha256);
    batch.addBindValue(run.linesRead);
    batch.addBindValue(run.recordCount);
    batch.addBindValue(run.firstDate.toString(Qt::ISODate));
    batch.addBindValue(run.lastDate.toString(Qt::ISODate));
    batch.addBindValue(run.newRecords);
    batch.addBindValue(run.unchangedRecords);
    batch.addBindValue(run.correctedRecords);
    batch.addBindValue(run.revisionReason.isNull()
                           ? QStringLiteral("") : run.revisionReason.trimmed());
    batch.addBindValue(run.status);
    batch.addBindValue(run.error.isNull() ? QStringLiteral("") : run.error);
    if (!batch.exec()) {
        const QString message = QStringLiteral("写入 A 股停复牌导入审计批次失败：%1")
                                    .arg(batch.lastError().text());
        m_database.rollback();
        return failImport(message);
    }

    QSqlQuery prior(m_database);
    prior.prepare(QStringLiteral(
        "select name, venue, state, reason, source, import_run_id "
        "from ashare_instrument_day_status where symbol=? and trading_date=?"));
    QSqlQuery item(m_database);
    item.prepare(QStringLiteral(
        "insert into ashare_instrument_day_import_items("
        "run_id, sequence, symbol, name, venue, trading_date, state, reason, "
        "source, source_version, change_kind, previous_import_run_id) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    QSqlQuery fact(m_database);
    fact.prepare(QStringLiteral(
        "insert into ashare_instrument_day_status("
        "symbol, name, venue, trading_date, state, reason, source, source_version, "
        "updated_at_utc, import_run_id) values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "on conflict(symbol, trading_date) do update set "
        "name=excluded.name, venue=excluded.venue, state=excluded.state, "
        "reason=excluded.reason, source=excluded.source, "
        "source_version=excluded.source_version, updated_at_utc=excluded.updated_at_utc, "
        "import_run_id=excluded.import_run_id"));
    const QString updatedAt = run.importedAt.toUTC().toString(Qt::ISODateWithMs);
    int sequence = 0;
    for (const AShareInstrumentDayRecord &record : records) {
        prior.bindValue(0, record.instrument.symbol.trimmed());
        prior.bindValue(1, record.tradingDate.toString(Qt::ISODate));
        if (!prior.exec()) {
            m_database.rollback();
            emitSqlError(QStringLiteral("读取停复牌事实修订前状态"));
            return false;
        }
        QString changeKind = QStringLiteral("new");
        QString previousRunId = QStringLiteral("");
        if (prior.next()) {
            previousRunId = prior.value(5).toString();
            const bool sameFact = prior.value(0).toString() == record.instrument.name
                && prior.value(1).toString() == record.instrument.venue
                && prior.value(2).toInt() == static_cast<int>(record.state)
                && prior.value(3).toString() == record.reason
                && prior.value(4).toString() == record.source;
            changeKind = sameFact ? QStringLiteral("unchanged")
                                  : QStringLiteral("corrected");
        }
        prior.finish();

        item.bindValue(0, run.runId);
        item.bindValue(1, sequence++);
        item.bindValue(2, record.instrument.symbol.trimmed());
        item.bindValue(3, record.instrument.name);
        item.bindValue(4, record.instrument.venue);
        item.bindValue(5, record.tradingDate.toString(Qt::ISODate));
        item.bindValue(6, static_cast<int>(record.state));
        item.bindValue(7, record.reason);
        item.bindValue(8, record.source);
        item.bindValue(9, record.sourceVersion);
        item.bindValue(10, changeKind);
        item.bindValue(11, previousRunId);
        if (!item.exec()) {
            m_database.rollback();
            emitSqlError(QStringLiteral("写入 A 股停复牌不可变批次明细"));
            return false;
        }
        item.finish();

        fact.bindValue(0, record.instrument.symbol.trimmed());
        fact.bindValue(1, record.instrument.name);
        fact.bindValue(2, record.instrument.venue);
        fact.bindValue(3, record.tradingDate.toString(Qt::ISODate));
        fact.bindValue(4, static_cast<int>(record.state));
        fact.bindValue(5, record.reason);
        fact.bindValue(6, record.source);
        fact.bindValue(7, record.sourceVersion);
        fact.bindValue(8, updatedAt);
        fact.bindValue(9, run.runId);
        if (!fact.exec()) {
            m_database.rollback();
            emitSqlError(QStringLiteral("写入 A 股停复牌事实"));
            return false;
        }
        fact.finish();
    }
    if (!m_database.commit()) {
        m_database.rollback();
        emitSqlError(QStringLiteral("提交 A 股停复牌可审计导入事务"));
        return false;
    }
    return true;
}

QVector<AShareInstrumentDayImportRun>
SqliteLocalDataStore::loadAShareInstrumentDayImportRuns(int limit)
{
    QVector<AShareInstrumentDayImportRun> result;
    if (limit <= 0) return result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select run_id, imported_at_utc, file_name, source, source_version, "
        "content_sha256, lines_read, record_count, first_date, last_date, "
        "new_record_count, unchanged_record_count, corrected_record_count, "
        "revision_reason, status, error from ashare_instrument_day_import_runs "
        "order by imported_at_utc desc, rowid desc limit ?"));
    query.addBindValue(limit);
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取 A 股停复牌导入审计历史"));
        return result;
    }
    while (query.next()) {
        AShareInstrumentDayImportRun run;
        run.runId = query.value(0).toString();
        run.importedAt = QDateTime::fromString(query.value(1).toString(),
                                               Qt::ISODateWithMs);
        run.fileName = query.value(2).toString();
        run.source = query.value(3).toString();
        run.sourceVersion = query.value(4).toString();
        run.contentSha256 = query.value(5).toString();
        run.linesRead = query.value(6).toLongLong();
        run.recordCount = query.value(7).toLongLong();
        run.firstDate = QDate::fromString(query.value(8).toString(), Qt::ISODate);
        run.lastDate = QDate::fromString(query.value(9).toString(), Qt::ISODate);
        run.newRecords = query.value(10).toLongLong();
        run.unchangedRecords = query.value(11).toLongLong();
        run.correctedRecords = query.value(12).toLongLong();
        run.revisionReason = query.value(13).toString();
        run.status = query.value(14).toString();
        run.error = query.value(15).toString();
        if (run.isValid()) result.append(run);
    }
    return result;
}

QVector<AShareInstrumentDayRecord>
SqliteLocalDataStore::loadAShareInstrumentDayImportRunRecords(
    const QString &runId)
{
    QVector<AShareInstrumentDayRecord> result;
    if (runId.trimmed().isEmpty()) return result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select symbol, name, venue, trading_date, state, reason, source, "
        "source_version from ashare_instrument_day_import_items "
        "where run_id=? order by sequence asc"));
    query.addBindValue(runId.trimmed());
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取 A 股停复牌不可变批次明细"));
        return result;
    }
    while (query.next()) {
        AShareInstrumentDayRecord record;
        record.instrument.symbol = query.value(0).toString();
        record.instrument.name = query.value(1).toString();
        record.instrument.market = Market::AShare;
        record.instrument.venue = query.value(2).toString();
        record.tradingDate = QDate::fromString(query.value(3).toString(),
                                               Qt::ISODate);
        record.state = static_cast<AShareInstrumentDayState>(query.value(4).toInt());
        record.reason = query.value(5).toString();
        record.source = query.value(6).toString();
        record.sourceVersion = query.value(7).toString();
        if (record.isValid()) result.append(record);
    }
    return result;
}

QVector<AShareInstrumentDayRecord>
SqliteLocalDataStore::loadAShareInstrumentDayRecords(
    const QStringList &symbols, const QDate &firstDate,
    const QDate &lastDate)
{
    QVector<AShareInstrumentDayRecord> result;
    QStringList normalizedSymbols;
    for (const QString &symbol : symbols) {
        const QString normalized = symbol.trimmed();
        if (!normalized.isEmpty() && !normalizedSymbols.contains(normalized))
            normalizedSymbols.append(normalized);
    }
    if (normalizedSymbols.isEmpty() || !firstDate.isValid()
        || !lastDate.isValid() || firstDate > lastDate) return result;
    QStringList placeholders;
    placeholders.reserve(normalizedSymbols.size());
    for (int index = 0; index < normalizedSymbols.size(); ++index)
        placeholders.append(QStringLiteral("?"));
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select symbol, name, venue, trading_date, state, reason, source, "
        "source_version from ashare_instrument_day_status where symbol in (%1) "
        "and trading_date >= ? and trading_date <= ? "
        "order by symbol asc, trading_date asc")
                      .arg(placeholders.join(QStringLiteral(","))));
    int binding = 0;
    for (const QString &symbol : normalizedSymbols)
        query.bindValue(binding++, symbol);
    query.bindValue(binding++, firstDate.toString(Qt::ISODate));
    query.bindValue(binding, lastDate.toString(Qt::ISODate));
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取 A 股停复牌事实"));
        return result;
    }
    while (query.next()) {
        AShareInstrumentDayRecord record;
        record.instrument.symbol = query.value(0).toString();
        record.instrument.name = query.value(1).toString();
        record.instrument.market = Market::AShare;
        record.instrument.venue = query.value(2).toString();
        record.tradingDate = QDate::fromString(query.value(3).toString(),
                                               Qt::ISODate);
        record.state = static_cast<AShareInstrumentDayState>(
            query.value(4).toInt());
        record.reason = query.value(5).toString();
        record.source = query.value(6).toString();
        record.sourceVersion = query.value(7).toString();
        if (record.isValid()) result.append(record);
    }
    return result;
}

bool SqliteLocalDataStore::saveAShareIndustryDataset(
    AShareIndustryDataset &dataset, bool *reused, QString *error)
{
    if (reused) *reused = false;
    if (error) error->clear();
    auto reject = [this, error](const QString &message) {
        if (error) *error = message;
        emit writeFailed(message);
        return false;
    };
    const AShareIndustryDatasetAnalysis analysis =
        analyzeAShareIndustryDataset(dataset);
    if (!analysis.valid) return reject(analysis.error);
    if (analysis.reused) {
        if (reused) *reused = true;
        return true;
    }
    if (analysis.requiresRevisionReason && dataset.revisionReason.isEmpty()) {
        return reject(QStringLiteral(
            "相同分类、来源和版本已有不同内容；必须填写修订原因"));
    }
    dataset.previousDatasetId = analysis.previousDatasetId;
    const QVector<AShareIndustryMembership> &normalized = dataset.memberships;

    if (dataset.datasetId.trimmed().isEmpty()) {
        dataset.datasetId = QStringLiteral("industry-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    } else {
        dataset.datasetId = dataset.datasetId.trimmed();
    }
    if (!dataset.importedAt.isValid())
        dataset.importedAt = QDateTime::currentDateTimeUtc();
    if (!m_database.transaction())
        return reject(QStringLiteral("无法开始行业数据集事务"));
    auto rollback = [this, error](const QString &message) {
        m_database.rollback();
        if (error) *error = message;
        emit writeFailed(message);
        return false;
    };
    QSqlQuery header(m_database);
    header.prepare(QStringLiteral(
        "insert into ashare_industry_datasets(dataset_id, classification, "
        "source, source_version, content_sha256, imported_at_utc, record_count, "
        "file_name, lines_read, revision_reason, previous_dataset_id) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    header.addBindValue(dataset.datasetId);
    header.addBindValue(dataset.classification);
    header.addBindValue(dataset.source);
    header.addBindValue(dataset.sourceVersion);
    header.addBindValue(dataset.contentSha256);
    header.addBindValue(dataset.importedAt.toUTC().toString(Qt::ISODateWithMs));
    header.addBindValue(normalized.size());
    header.addBindValue(dataset.fileName.isNull()
                            ? QStringLiteral("") : dataset.fileName);
    header.addBindValue(dataset.linesRead);
    header.addBindValue(dataset.revisionReason.isNull()
                            ? QStringLiteral("") : dataset.revisionReason);
    header.addBindValue(dataset.previousDatasetId.isNull()
                            ? QStringLiteral("") : dataset.previousDatasetId);
    if (!header.exec())
        return rollback(QStringLiteral("写入行业数据集身份失败：%1")
                            .arg(header.lastError().text()));

    QSqlQuery item(m_database);
    item.prepare(QStringLiteral(
        "insert into ashare_industry_memberships(dataset_id, sequence, symbol, "
        "name, venue, industry_code, industry_name, effective_from, effective_to) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (int index = 0; index < normalized.size(); ++index) {
        const AShareIndustryMembership &membership = normalized.at(index);
        item.bindValue(0, dataset.datasetId);
        item.bindValue(1, index);
        item.bindValue(2, membership.instrument.symbol);
        item.bindValue(3, membership.instrument.name.isNull()
                              ? QStringLiteral("") : membership.instrument.name);
        item.bindValue(4, membership.instrument.venue.isNull()
                              ? QStringLiteral("") : membership.instrument.venue);
        item.bindValue(5, membership.industryCode);
        item.bindValue(6, membership.industryName);
        item.bindValue(7, membership.effectiveFrom.toString(Qt::ISODate));
        item.bindValue(8, membership.effectiveTo.isValid()
                              ? membership.effectiveTo.toString(Qt::ISODate)
                              : QStringLiteral(""));
        if (!item.exec())
            return rollback(QStringLiteral("写入行业数据集成员失败：%1")
                                .arg(item.lastError().text()));
        item.finish();
    }
    if (!m_database.commit())
        return rollback(QStringLiteral("提交行业数据集事务失败"));
    return true;
}

AShareIndustryDatasetAnalysis
SqliteLocalDataStore::analyzeAShareIndustryDataset(
    AShareIndustryDataset &dataset)
{
    AShareIndustryDatasetAnalysis analysis;
    if (!normalizeIndustryDataset(&dataset, &analysis.error)) return analysis;
    QSqlQuery exact(m_database);
    exact.prepare(QStringLiteral(
        "select dataset_id, imported_at_utc from ashare_industry_datasets "
        "where classification=? and source=? and source_version=? "
        "and content_sha256=?"));
    exact.addBindValue(dataset.classification);
    exact.addBindValue(dataset.source);
    exact.addBindValue(dataset.sourceVersion);
    exact.addBindValue(dataset.contentSha256);
    if (!exact.exec()) {
        analysis.error = QStringLiteral("检查行业数据集重复身份失败：%1")
                             .arg(exact.lastError().text());
        return analysis;
    }
    if (exact.next()) {
        analysis.valid = true;
        analysis.reused = true;
        analysis.existingDatasetId = exact.value(0).toString();
        dataset.datasetId = analysis.existingDatasetId;
        dataset.importedAt = QDateTime::fromString(
            exact.value(1).toString(), Qt::ISODateWithMs);
        return analysis;
    }
    QSqlQuery previous(m_database);
    previous.prepare(QStringLiteral(
        "select dataset_id from ashare_industry_datasets where classification=? "
        "and source=? and source_version=? "
        "order by imported_at_utc desc, rowid desc limit 1"));
    previous.addBindValue(dataset.classification);
    previous.addBindValue(dataset.source);
    previous.addBindValue(dataset.sourceVersion);
    if (!previous.exec()) {
        analysis.error = QStringLiteral("检查行业数据集修订身份失败：%1")
                             .arg(previous.lastError().text());
        return analysis;
    }
    if (previous.next()) {
        analysis.previousDatasetId = previous.value(0).toString();
        analysis.requiresRevisionReason = true;
    }
    analysis.valid = true;
    return analysis;
}

QVector<AShareIndustryDataset>
SqliteLocalDataStore::loadAShareIndustryDatasets(int limit)
{
    QVector<AShareIndustryDataset> result;
    QSqlQuery query(m_database);
    // 并列时间戳用 rowid 兜底，不能用随机 dataset_id：同一毫秒内导入的两份数据
    // 时间戳完全相同，按 UUID 排序得到的是任意顺序，"最新一份"会随机变化。
    // rowid 是插入顺序，才真正表达“越晚导入越靠前”。本文件其它 “order by
    // <时间> desc, <随机 id> desc” 的查询同理由此一并改为 rowid。
    query.prepare(QStringLiteral(
        "select dataset_id, classification, source, source_version, "
        "content_sha256, imported_at_utc, record_count, file_name, lines_read, "
        "revision_reason, previous_dataset_id from ashare_industry_datasets "
        "order by imported_at_utc desc, rowid desc limit ?"));
    query.addBindValue(std::max(1, limit));
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取行业数据集审计历史"));
        return result;
    }
    while (query.next()) {
        AShareIndustryDataset dataset;
        dataset.datasetId = query.value(0).toString();
        dataset.classification = query.value(1).toString();
        dataset.source = query.value(2).toString();
        dataset.sourceVersion = query.value(3).toString();
        dataset.contentSha256 = query.value(4).toString();
        dataset.importedAt = QDateTime::fromString(
            query.value(5).toString(), Qt::ISODateWithMs);
        dataset.recordCount = query.value(6).toLongLong();
        dataset.fileName = query.value(7).toString();
        dataset.linesRead = query.value(8).toLongLong();
        dataset.revisionReason = query.value(9).toString();
        dataset.previousDatasetId = query.value(10).toString();
        result.append(dataset);
    }
    return result;
}

std::optional<AShareIndustrySnapshot>
SqliteLocalDataStore::loadAShareIndustrySnapshot(
    const QString &datasetId, const QDate &asOfDate,
    const QStringList &symbols)
{
    QStringList normalizedSymbols;
    for (const QString &symbol : symbols) {
        const QString normalized = symbol.trimmed();
        if (!normalized.isEmpty() && !normalizedSymbols.contains(normalized))
            normalizedSymbols.append(normalized);
    }
    std::sort(normalizedSymbols.begin(), normalizedSymbols.end());
    if (datasetId.trimmed().isEmpty() || !asOfDate.isValid()
        || normalizedSymbols.isEmpty()) return std::nullopt;

    QSqlQuery header(m_database);
    header.prepare(QStringLiteral(
        "select classification, source, source_version, content_sha256 "
        "from ashare_industry_datasets where dataset_id=?"));
    header.addBindValue(datasetId.trimmed());
    if (!header.exec()) {
        emitSqlError(QStringLiteral("读取行业数据集身份"));
        return std::nullopt;
    }
    if (!header.next()) return std::nullopt;

    AShareIndustrySnapshot snapshot;
    snapshot.datasetId = datasetId.trimmed();
    snapshot.classification = header.value(0).toString();
    snapshot.source = header.value(1).toString();
    snapshot.sourceVersion = header.value(2).toString();
    snapshot.datasetContentSha256 = header.value(3).toString();
    snapshot.asOfDate = asOfDate;
    snapshot.expectedSymbols = normalizedSymbols.size();

    QStringList placeholders;
    placeholders.fill(QStringLiteral("?"), normalizedSymbols.size());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select symbol, name, venue, industry_code, industry_name, "
        "effective_from, effective_to from ashare_industry_memberships "
        "where dataset_id=? and symbol in (%1) and effective_from<=? "
        "and (effective_to='' or effective_to>=?) "
        "order by symbol asc, effective_from asc")
                      .arg(placeholders.join(QStringLiteral(","))));
    int binding = 0;
    query.bindValue(binding++, snapshot.datasetId);
    for (const QString &symbol : normalizedSymbols)
        query.bindValue(binding++, symbol);
    query.bindValue(binding++, asOfDate.toString(Qt::ISODate));
    query.bindValue(binding, asOfDate.toString(Qt::ISODate));
    if (!query.exec()) {
        emitSqlError(QStringLiteral("解析行业数据集日期快照"));
        return std::nullopt;
    }
    QSet<QString> resolved;
    while (query.next()) {
        const QString symbol = query.value(0).toString();
        if (resolved.contains(symbol)) {
            emit writeFailed(QStringLiteral("行业数据集存在重叠有效期：%1")
                                 .arg(symbol));
            return std::nullopt;
        }
        AShareIndustryMembership membership;
        membership.instrument = {symbol, query.value(1).toString(),
                                 Market::AShare, query.value(2).toString()};
        membership.industryCode = query.value(3).toString();
        membership.industryName = query.value(4).toString();
        membership.effectiveFrom = QDate::fromString(
            query.value(5).toString(), Qt::ISODate);
        membership.effectiveTo = QDate::fromString(
            query.value(6).toString(), Qt::ISODate);
        if (!membership.isValid()) return std::nullopt;
        resolved.insert(symbol);
        snapshot.memberships.append(membership);
    }
    snapshot.resolvedSymbols = resolved.size();
    snapshot.coverageComplete =
        snapshot.resolvedSymbols == snapshot.expectedSymbols;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData("pptrader-ashare-industry-snapshot-v1\n");
    hash.addData(snapshot.datasetContentSha256.toUtf8());
    hash.addData("\n");
    hash.addData(asOfDate.toString(Qt::ISODate).toUtf8());
    hash.addData("\n");
    for (const AShareIndustryMembership &membership : snapshot.memberships) {
        hash.addData(membership.instrument.symbol.toUtf8());
        hash.addData("=");
        hash.addData(membership.industryCode.toUtf8());
        hash.addData("\n");
    }
    snapshot.snapshotVersion = QStringLiteral("sha256:%1")
        .arg(QString::fromLatin1(hash.result().toHex()));
    return snapshot;
}

bool SqliteLocalDataStore::registerFactorDefinitionVersion(
    FactorDefinitionVersion &definition, bool *reused, QString *error)
{
    if (reused) *reused = false;
    if (error) error->clear();
    definition.factorId = definition.factorId.trimmed();
    definition.name = definition.name.trimmed();
    definition.alias = definition.alias.trimmed();
    definition.sourceStrategyId = definition.sourceStrategyId.trimmed();
    definition.version = 0;
    definition.contentSha256 = definition.computedContentSha256();
    QString validationError;
    if (!definition.isValid(&validationError)) {
        if (error) *error = validationError;
        return false;
    }
    if (!m_database.transaction()) {
        if (error) *error = QStringLiteral("无法开始因子版本注册事务");
        return false;
    }

    QSqlQuery latest(m_database);
    latest.prepare(QStringLiteral(
        "select version, content_sha256, created_at_utc from "
        "factor_definition_versions where factor_id=? "
        "order by version desc limit 1"));
    latest.addBindValue(definition.factorId);
    if (!latest.exec()) {
        m_database.rollback();
        if (error) *error = latest.lastError().text();
        return false;
    }
    int nextVersion = 1;
    if (latest.next()) {
        const int currentVersion = latest.value(0).toInt();
        nextVersion = currentVersion + 1;
        if (latest.value(1).toString() == definition.contentSha256) {
            definition.version = currentVersion;
            definition.createdAt = QDateTime::fromString(
                latest.value(2).toString(), Qt::ISODateWithMs);
            if (!m_database.commit()) {
                if (error) *error = QStringLiteral("提交因子版本复用事务失败");
                return false;
            }
            if (reused) *reused = true;
            return true;
        }
    }
    latest.finish();

    definition.version = nextVersion;
    definition.createdAt = QDateTime::currentDateTimeUtc();
    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "insert into factor_definition_versions(factor_id, version, name, alias, "
        "source_strategy_id, source_strategy_version, definition_json, "
        "preprocessing_json, content_sha256, created_at_utc) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(definition.factorId);
    insert.addBindValue(definition.version);
    insert.addBindValue(definition.name);
    insert.addBindValue(definition.alias);
    insert.addBindValue(definition.sourceStrategyId);
    insert.addBindValue(definition.sourceStrategyVersion);
    insert.addBindValue(compactJson(definition.definition));
    insert.addBindValue(compactJson(definition.preprocessing.toVariantMap()));
    insert.addBindValue(definition.contentSha256);
    insert.addBindValue(definition.createdAt.toString(Qt::ISODateWithMs));
    if (!insert.exec()) {
        const QString message = insert.lastError().text();
        m_database.rollback();
        if (error) *error = message;
        return false;
    }
    if (!m_database.commit()) {
        if (error) *error = QStringLiteral("提交因子版本注册事务失败");
        return false;
    }
    return true;
}

QVector<FactorDefinitionVersion>
SqliteLocalDataStore::loadFactorDefinitionVersions(int limit)
{
    QVector<FactorDefinitionVersion> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select factor_id, version, name, alias, source_strategy_id, "
        "source_strategy_version, definition_json, preprocessing_json, "
        "content_sha256, created_at_utc from factor_definition_versions "
        "order by created_at_utc desc, factor_id asc, version desc limit ?"));
    query.addBindValue(std::max(1, limit));
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取因子定义版本目录"));
        return result;
    }
    while (query.next()) {
        FactorDefinitionVersion definition;
        definition.factorId = query.value(0).toString();
        definition.version = query.value(1).toInt();
        definition.name = query.value(2).toString();
        definition.alias = query.value(3).toString();
        definition.sourceStrategyId = query.value(4).toString();
        definition.sourceStrategyVersion = query.value(5).toInt();
        definition.definition = QJsonDocument::fromJson(
            query.value(6).toByteArray()).object().toVariantMap();
        definition.preprocessing = FactorPreprocessingPolicy::fromVariantMap(
            QJsonDocument::fromJson(query.value(7).toByteArray())
                .object().toVariantMap());
        definition.contentSha256 = query.value(8).toString();
        definition.createdAt = QDateTime::fromString(
            query.value(9).toString(), Qt::ISODateWithMs);
        if (definition.isValid()) result.append(definition);
    }
    return result;
}

bool SqliteLocalDataStore::registerFactorCandidateVersion(
    FactorCandidateVersion &candidate, bool *reused, QString *error)
{
    if (reused) *reused = false;
    if (error) error->clear();
    candidate.candidateId = candidate.candidateId.trimmed();
    candidate.name = candidate.name.trimmed();
    candidate.hypothesis = candidate.hypothesis.trimmed();
    candidate.sourceKind = candidate.sourceKind.trimmed();
    candidate.sourceReference = candidate.sourceReference.trimmed();
    if (candidate.sourceReference.isNull())
        candidate.sourceReference = QStringLiteral("");
    candidate.version = 0;
    candidate.frozenAt = {};
    candidate.contentSha256 = candidate.computedContentSha256();
    QString validationError;
    if (!candidate.isValid(&validationError)) {
        if (error) *error = validationError;
        return false;
    }
    if (!m_database.transaction()) {
        if (error) *error = QStringLiteral("无法开始候选因子登记事务");
        return false;
    }
    QSqlQuery identical(m_database);
    identical.prepare(QStringLiteral(
        "select version, proposed_at_utc, frozen_at_utc from "
        "factor_candidate_versions where candidate_id=? and content_sha256=?"));
    identical.addBindValue(candidate.candidateId);
    identical.addBindValue(candidate.contentSha256);
    if (!identical.exec()) {
        m_database.rollback();
        if (error) *error = identical.lastError().text();
        return false;
    }
    if (identical.next()) {
        candidate.version = identical.value(0).toInt();
        candidate.proposedAt = QDateTime::fromString(
            identical.value(1).toString(), Qt::ISODateWithMs);
        candidate.frozenAt = QDateTime::fromString(
            identical.value(2).toString(), Qt::ISODateWithMs);
        if (!m_database.commit()) {
            if (error) *error = QStringLiteral("提交候选因子复用事务失败");
            return false;
        }
        if (reused) *reused = true;
        return true;
    }
    identical.finish();
    QSqlQuery latest(m_database);
    latest.prepare(QStringLiteral(
        "select coalesce(max(version), 0) from factor_candidate_versions "
        "where candidate_id=?"));
    latest.addBindValue(candidate.candidateId);
    if (!latest.exec() || !latest.next()) {
        m_database.rollback();
        if (error) *error = QStringLiteral("读取候选因子最新版本失败");
        return false;
    }
    candidate.version = latest.value(0).toInt() + 1;
    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "insert into factor_candidate_versions(candidate_id, version, name, "
        "hypothesis, source_kind, source_reference, factor_definition_json, "
        "proposed_at_utc, frozen_at_utc, content_sha256) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, null, ?)"));
    insert.addBindValue(candidate.candidateId);
    insert.addBindValue(candidate.version);
    insert.addBindValue(candidate.name);
    insert.addBindValue(candidate.hypothesis);
    insert.addBindValue(candidate.sourceKind);
    insert.addBindValue(candidate.sourceReference);
    insert.addBindValue(compactJson(candidate.factorDefinition));
    insert.addBindValue(candidate.proposedAt.toUTC().toString(Qt::ISODateWithMs));
    insert.addBindValue(candidate.contentSha256);
    if (!insert.exec()) {
        const QString message = insert.lastError().text();
        m_database.rollback();
        if (error) *error = message;
        return false;
    }
    if (!m_database.commit()) {
        if (error) *error = QStringLiteral("提交候选因子登记事务失败");
        return false;
    }
    return true;
}

bool SqliteLocalDataStore::freezeFactorCandidateVersion(
    const QString &candidateId, int version, const QDateTime &frozenAt,
    QString *error)
{
    if (error) error->clear();
    if (candidateId.trimmed().isEmpty() || version < 1 || !frozenAt.isValid()) {
        if (error) *error = QStringLiteral("候选因子冻结参数不完整");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select proposed_at_utc, frozen_at_utc from factor_candidate_versions "
        "where candidate_id=? and version=?"));
    query.addBindValue(candidateId.trimmed());
    query.addBindValue(version);
    if (!query.exec() || !query.next()) {
        if (error) *error = QStringLiteral("候选因子版本不存在");
        return false;
    }
    const QDateTime proposedAt = QDateTime::fromString(
        query.value(0).toString(), Qt::ISODateWithMs);
    const QDateTime existingFrozenAt = QDateTime::fromString(
        query.value(1).toString(), Qt::ISODateWithMs);
    if (frozenAt < proposedAt) {
        if (error) *error = QStringLiteral("候选因子冻结时间不能早于提出时间");
        return false;
    }
    if (existingFrozenAt.isValid()) {
        if (existingFrozenAt.toUTC() == frozenAt.toUTC()) return true;
        if (error) *error = QStringLiteral("候选因子版本已经冻结，冻结时间不可修改");
        return false;
    }
    QSqlQuery update(m_database);
    update.prepare(QStringLiteral(
        "update factor_candidate_versions set frozen_at_utc=? "
        "where candidate_id=? and version=? and frozen_at_utc is null"));
    update.addBindValue(frozenAt.toUTC().toString(Qt::ISODateWithMs));
    update.addBindValue(candidateId.trimmed());
    update.addBindValue(version);
    if (!update.exec() || update.numRowsAffected() != 1) {
        if (error) *error = QStringLiteral("冻结候选因子版本失败");
        return false;
    }
    return true;
}

QVector<FactorCandidateVersion> SqliteLocalDataStore::loadFactorCandidateVersions(
    int limit)
{
    QVector<FactorCandidateVersion> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select candidate_id, version, name, hypothesis, source_kind, source_reference, "
        "factor_definition_json, proposed_at_utc, frozen_at_utc, content_sha256 "
        "from factor_candidate_versions order by candidate_id asc, version desc limit ?"));
    query.addBindValue(std::max(1, limit));
    if (!query.exec()) return result;
    while (query.next()) {
        FactorCandidateVersion candidate;
        candidate.candidateId = query.value(0).toString();
        candidate.version = query.value(1).toInt();
        candidate.name = query.value(2).toString();
        candidate.hypothesis = query.value(3).toString();
        candidate.sourceKind = query.value(4).toString();
        candidate.sourceReference = query.value(5).toString();
        candidate.factorDefinition = QJsonDocument::fromJson(
            query.value(6).toByteArray()).object().toVariantMap();
        candidate.proposedAt = QDateTime::fromString(
            query.value(7).toString(), Qt::ISODateWithMs);
        candidate.frozenAt = QDateTime::fromString(
            query.value(8).toString(), Qt::ISODateWithMs);
        candidate.contentSha256 = query.value(9).toString();
        result.append(candidate);
    }
    return result;
}

bool SqliteLocalDataStore::linkFactorCandidateEvaluation(
    const QString &candidateId, int version, const PortfolioArchiveRecord &archive,
    FactorCandidateEvaluationRole role, FactorCandidateEvaluation &evaluation,
    bool *reused, QString *error)
{
    if (reused) *reused = false;
    if (error) error->clear();
    FactorCandidateVersion candidate;
    bool found = false;
    for (const FactorCandidateVersion &item : loadFactorCandidateVersions(10000)) {
        if (item.candidateId == candidateId.trimmed() && item.version == version) {
            candidate = item;
            found = true;
            break;
        }
    }
    if (!found) {
        if (error) *error = QStringLiteral("候选因子版本不存在");
        return false;
    }
    const FactorCandidateEvaluationDecision decision =
        FactorCandidateGovernance::evaluate(candidate, archive, role);
    if (!decision.accepted) {
        if (error) *error = decision.reason;
        return false;
    }
    const QString roleCode = factorCandidateEvaluationRoleCode(role);
    QSqlQuery existing(m_database);
    existing.prepare(QStringLiteral(
        "select evaluation_id, candidate_content_sha256, archive_completed_at_utc, "
        "archive_factor_id, archive_factor_version, research_method_version, "
        "preregistered, archive_identity_sha256, linked_at_utc from "
        "factor_candidate_evaluations where candidate_id=? and candidate_version=? "
        "and archive_result_id=? and evaluation_role=?"));
    existing.addBindValue(candidate.candidateId);
    existing.addBindValue(candidate.version);
    existing.addBindValue(archive.resultId);
    existing.addBindValue(roleCode);
    if (!existing.exec()) {
        if (error) *error = QStringLiteral("检查候选因子验证关联失败");
        return false;
    }
    if (existing.next()) {
        evaluation.evaluationId = existing.value(0).toString();
        evaluation.candidateId = candidate.candidateId;
        evaluation.candidateVersion = candidate.version;
        evaluation.candidateContentSha256 = existing.value(1).toString();
        evaluation.archiveResultId = archive.resultId;
        evaluation.archiveCompletedAt = QDateTime::fromString(existing.value(2).toString(), Qt::ISODateWithMs);
        evaluation.archiveFactorId = existing.value(3).toString();
        evaluation.archiveFactorVersion = existing.value(4).toInt();
        evaluation.researchMethodVersion = existing.value(5).toString();
        evaluation.role = role;
        evaluation.preregistered = existing.value(6).toBool();
        evaluation.archiveIdentitySha256 = existing.value(7).toString();
        evaluation.linkedAt = QDateTime::fromString(existing.value(8).toString(), Qt::ISODateWithMs);
        if (reused) *reused = true;
        return true;
    }
    const QVariantMap factorIdentity = archive.result.immutableIdentity
        .value(QStringLiteral("factorResearch")).toMap();
    evaluation.evaluationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    evaluation.candidateId = candidate.candidateId;
    evaluation.candidateVersion = candidate.version;
    evaluation.candidateContentSha256 = candidate.contentSha256;
    evaluation.archiveResultId = archive.resultId;
    evaluation.archiveCompletedAt = archive.completedAt.toUTC();
    evaluation.archiveFactorId = factorIdentity.value(QStringLiteral("factorId")).toString();
    evaluation.archiveFactorVersion = factorIdentity.value(QStringLiteral("factorVersion")).toInt();
    evaluation.researchMethodVersion = decision.researchMethodVersion;
    evaluation.role = role;
    evaluation.preregistered = decision.preregistered;
    evaluation.archiveIdentitySha256 = decision.archiveIdentitySha256;
    evaluation.linkedAt = QDateTime::currentDateTimeUtc();
    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "insert into factor_candidate_evaluations(evaluation_id, candidate_id, "
        "candidate_version, candidate_content_sha256, archive_result_id, "
        "archive_completed_at_utc, archive_factor_id, archive_factor_version, "
        "research_method_version, evaluation_role, preregistered, "
        "archive_identity_sha256, linked_at_utc) values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(evaluation.evaluationId);
    insert.addBindValue(evaluation.candidateId);
    insert.addBindValue(evaluation.candidateVersion);
    insert.addBindValue(evaluation.candidateContentSha256);
    insert.addBindValue(evaluation.archiveResultId);
    insert.addBindValue(evaluation.archiveCompletedAt.toString(Qt::ISODateWithMs));
    insert.addBindValue(evaluation.archiveFactorId);
    insert.addBindValue(evaluation.archiveFactorVersion);
    insert.addBindValue(evaluation.researchMethodVersion);
    insert.addBindValue(roleCode);
    insert.addBindValue(evaluation.preregistered ? 1 : 0);
    insert.addBindValue(evaluation.archiveIdentitySha256);
    insert.addBindValue(evaluation.linkedAt.toString(Qt::ISODateWithMs));
    if (!insert.exec()) {
        if (error) *error = insert.lastError().text();
        return false;
    }
    return true;
}

QVector<FactorCandidateEvaluation> SqliteLocalDataStore::loadFactorCandidateEvaluations(
    const QString &candidateId, int version, int limit)
{
    QVector<FactorCandidateEvaluation> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select evaluation_id, candidate_id, candidate_version, candidate_content_sha256, "
        "archive_result_id, archive_completed_at_utc, archive_factor_id, "
        "archive_factor_version, research_method_version, evaluation_role, preregistered, "
        "archive_identity_sha256, linked_at_utc from factor_candidate_evaluations "
        "where candidate_id=? and candidate_version=? "
        "order by linked_at_utc desc, rowid desc limit ?"));
    query.addBindValue(candidateId.trimmed());
    query.addBindValue(version);
    query.addBindValue(std::max(1, limit));
    if (!query.exec()) return result;
    while (query.next()) {
        FactorCandidateEvaluation evaluation;
        evaluation.evaluationId = query.value(0).toString();
        evaluation.candidateId = query.value(1).toString();
        evaluation.candidateVersion = query.value(2).toInt();
        evaluation.candidateContentSha256 = query.value(3).toString();
        evaluation.archiveResultId = query.value(4).toString();
        evaluation.archiveCompletedAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODateWithMs);
        evaluation.archiveFactorId = query.value(6).toString();
        evaluation.archiveFactorVersion = query.value(7).toInt();
        evaluation.researchMethodVersion = query.value(8).toString();
        evaluation.role = query.value(9).toString() == QStringLiteral("prospective_holdout")
            ? FactorCandidateEvaluationRole::ProspectiveHoldout
            : FactorCandidateEvaluationRole::RetrospectiveReference;
        evaluation.preregistered = query.value(10).toBool();
        evaluation.archiveIdentitySha256 = query.value(11).toString();
        evaluation.linkedAt = QDateTime::fromString(query.value(12).toString(), Qt::ISODateWithMs);
        result.append(evaluation);
    }
    return result;
}

bool SqliteLocalDataStore::saveExternalFactorDataset(
    ExternalFactorDataset &dataset,
    const QVector<ExternalFactorObservation> &observations,
    bool *reused, QString *error)
{
    if (reused) *reused = false;
    if (error) error->clear();
    dataset.factorKey = dataset.factorKey.trimmed();
    dataset.source = dataset.source.trimmed();
    dataset.sourceVersion = dataset.sourceVersion.trimmed();
    dataset.sourceReference = dataset.sourceReference.trimmed();
    if (dataset.factorKey.isEmpty() || !isSha256(dataset.factorDefinitionSha256)
        || dataset.source != QStringLiteral("authorized_csv")
        || dataset.sourceVersion.isEmpty() || dataset.sourceReference.isEmpty()
        || !isSha256(dataset.contentSha256) || observations.isEmpty()) {
        if (error) *error = QStringLiteral("授权因子数据集身份、来源或观测不完整");
        return false;
    }

    QVector<ExternalFactorObservation> normalized = observations;
    QSet<QString> symbols;
    QSet<QString> timestamps;
    for (ExternalFactorObservation &observation : normalized) {
        observation.symbol = observation.symbol.trimmed();
        if (!observation.timestamp.isValid()
            || !isShanghaiShenzhenSymbol(observation.symbol)
            || !std::isfinite(observation.value)) {
            if (error) *error = QStringLiteral(
                "授权因子数据集包含无效时间、数值或非沪深 A 股代码");
            return false;
        }
        observation.timestamp = observation.timestamp.toUTC();
        symbols.insert(observation.symbol);
        timestamps.insert(observation.timestamp.toString(Qt::ISODateWithMs));
    }
    std::sort(normalized.begin(), normalized.end(),
              [](const ExternalFactorObservation &left,
                 const ExternalFactorObservation &right) {
        return std::tie(left.timestamp, left.symbol)
            < std::tie(right.timestamp, right.symbol);
    });
    for (int index = 1; index < normalized.size(); ++index) {
        if (normalized.at(index - 1).timestamp == normalized.at(index).timestamp
            && normalized.at(index - 1).symbol == normalized.at(index).symbol) {
            if (error) *error = QStringLiteral("授权因子数据集包含重复观测身份");
            return false;
        }
    }
    dataset.recordCount = normalized.size();
    dataset.symbolCount = symbols.size();
    dataset.timestampCount = timestamps.size();
    dataset.firstTimestamp = normalized.first().timestamp;
    dataset.lastTimestamp = normalized.last().timestamp;
    if (!dataset.importedAt.isValid())
        dataset.importedAt = QDateTime::currentDateTimeUtc();
    else
        dataset.importedAt = dataset.importedAt.toUTC();

    QSqlQuery existing(m_database);
    existing.prepare(QStringLiteral(
        "select dataset_id, imported_at_utc, record_count, symbol_count, "
        "timestamp_count, first_timestamp_utc, last_timestamp_utc "
        "from external_factor_datasets where factor_key=? "
        "and factor_definition_sha256=? and source=? and source_version=? "
        "and content_sha256=?"));
    existing.addBindValue(dataset.factorKey);
    existing.addBindValue(dataset.factorDefinitionSha256);
    existing.addBindValue(dataset.source);
    existing.addBindValue(dataset.sourceVersion);
    existing.addBindValue(dataset.contentSha256);
    if (!existing.exec()) {
        if (error) *error = existing.lastError().text();
        return false;
    }
    if (existing.next()) {
        dataset.datasetId = existing.value(0).toString();
        dataset.importedAt = QDateTime::fromString(existing.value(1).toString(),
                                                   Qt::ISODateWithMs);
        dataset.recordCount = existing.value(2).toLongLong();
        dataset.symbolCount = existing.value(3).toInt();
        dataset.timestampCount = existing.value(4).toInt();
        dataset.firstTimestamp = QDateTime::fromString(existing.value(5).toString(),
                                                       Qt::ISODateWithMs);
        dataset.lastTimestamp = QDateTime::fromString(existing.value(6).toString(),
                                                      Qt::ISODateWithMs);
        if (reused) *reused = true;
        return true;
    }

    if (dataset.datasetId.trimmed().isEmpty())
        dataset.datasetId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!m_database.transaction()) {
        if (error) *error = QStringLiteral("无法开始授权因子数据集事务");
        return false;
    }
    QSqlQuery insertDataset(m_database);
    insertDataset.prepare(QStringLiteral(
        "insert into external_factor_datasets(dataset_id, factor_key, "
        "factor_definition_sha256, source, source_version, source_reference, "
        "content_sha256, imported_at_utc, record_count, symbol_count, "
        "timestamp_count, first_timestamp_utc, last_timestamp_utc) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insertDataset.addBindValue(dataset.datasetId);
    insertDataset.addBindValue(dataset.factorKey);
    insertDataset.addBindValue(dataset.factorDefinitionSha256);
    insertDataset.addBindValue(dataset.source);
    insertDataset.addBindValue(dataset.sourceVersion);
    insertDataset.addBindValue(dataset.sourceReference);
    insertDataset.addBindValue(dataset.contentSha256);
    insertDataset.addBindValue(dataset.importedAt.toString(Qt::ISODateWithMs));
    insertDataset.addBindValue(dataset.recordCount);
    insertDataset.addBindValue(dataset.symbolCount);
    insertDataset.addBindValue(dataset.timestampCount);
    insertDataset.addBindValue(dataset.firstTimestamp.toString(Qt::ISODateWithMs));
    insertDataset.addBindValue(dataset.lastTimestamp.toString(Qt::ISODateWithMs));
    if (!insertDataset.exec()) {
        m_database.rollback();
        if (error) *error = insertDataset.lastError().text();
        return false;
    }
    QSqlQuery insertObservation(m_database);
    insertObservation.prepare(QStringLiteral(
        "insert into external_factor_observations(dataset_id, timestamp_utc, "
        "symbol, value) values(?, ?, ?, ?)"));
    for (const ExternalFactorObservation &observation : normalized) {
        insertObservation.bindValue(0, dataset.datasetId);
        insertObservation.bindValue(1, observation.timestamp.toString(Qt::ISODateWithMs));
        insertObservation.bindValue(2, observation.symbol);
        insertObservation.bindValue(3, observation.value);
        if (!insertObservation.exec()) {
            m_database.rollback();
            if (error) *error = insertObservation.lastError().text();
            return false;
        }
    }
    if (!m_database.commit()) {
        if (error) *error = QStringLiteral("提交授权因子数据集事务失败");
        return false;
    }
    return true;
}

QVector<ExternalFactorDataset> SqliteLocalDataStore::loadExternalFactorDatasets(
    int limit)
{
    QVector<ExternalFactorDataset> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select dataset_id, factor_key, factor_definition_sha256, source, "
        "source_version, source_reference, content_sha256, imported_at_utc, "
        "record_count, symbol_count, timestamp_count, first_timestamp_utc, "
        "last_timestamp_utc from external_factor_datasets "
        "order by imported_at_utc desc, dataset_id asc limit ?"));
    query.addBindValue(std::clamp(limit, 1, 10000));
    if (!query.exec()) return result;
    while (query.next()) {
        ExternalFactorDataset dataset;
        dataset.datasetId = query.value(0).toString();
        dataset.factorKey = query.value(1).toString();
        dataset.factorDefinitionSha256 = query.value(2).toString();
        dataset.source = query.value(3).toString();
        dataset.sourceVersion = query.value(4).toString();
        dataset.sourceReference = query.value(5).toString();
        dataset.contentSha256 = query.value(6).toString();
        dataset.importedAt = QDateTime::fromString(query.value(7).toString(),
                                                  Qt::ISODateWithMs);
        dataset.recordCount = query.value(8).toLongLong();
        dataset.symbolCount = query.value(9).toInt();
        dataset.timestampCount = query.value(10).toInt();
        dataset.firstTimestamp = QDateTime::fromString(query.value(11).toString(),
                                                       Qt::ISODateWithMs);
        dataset.lastTimestamp = QDateTime::fromString(query.value(12).toString(),
                                                      Qt::ISODateWithMs);
        result.append(dataset);
    }
    return result;
}

QVector<ExternalFactorObservation>
SqliteLocalDataStore::loadExternalFactorObservations(const QString &datasetId)
{
    QVector<ExternalFactorObservation> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select timestamp_utc, symbol, value from external_factor_observations "
        "where dataset_id=? order by timestamp_utc asc, symbol asc"));
    query.addBindValue(datasetId);
    if (!query.exec()) return result;
    while (query.next()) {
        result.append({QDateTime::fromString(query.value(0).toString(),
                                             Qt::ISODateWithMs),
                       query.value(1).toString(), query.value(2).toDouble()});
    }
    return result;
}

bool SqliteLocalDataStore::bindExternalFactorDataset(
    const QString &candidateId, int candidateVersion,
    const QString &datasetId, ExternalFactorDatasetBinding &binding,
    bool *reused, QString *error)
{
    if (reused) *reused = false;
    if (error) error->clear();
    QSqlQuery candidateQuery(m_database);
    candidateQuery.prepare(QStringLiteral(
        "select factor_definition_json, frozen_at_utc, content_sha256 "
        "from factor_candidate_versions where candidate_id=? and version=?"));
    candidateQuery.addBindValue(candidateId);
    candidateQuery.addBindValue(candidateVersion);
    if (!candidateQuery.exec() || !candidateQuery.next()) {
        if (error) *error = QStringLiteral("候选因子版本不存在");
        return false;
    }
    FactorCandidateVersion candidate;
    candidate.candidateId = candidateId;
    candidate.version = candidateVersion;
    candidate.factorDefinition = variantMapFromJson(
        candidateQuery.value(0).toString());
    candidate.frozenAt = QDateTime::fromString(candidateQuery.value(1).toString(),
                                               Qt::ISODateWithMs);
    candidate.contentSha256 = candidateQuery.value(2).toString();
    if (!candidate.frozenAt.isValid()) {
        if (error) *error = QStringLiteral("候选因子必须先冻结才能绑定外部数据集");
        return false;
    }

    QSqlQuery datasetQuery(m_database);
    datasetQuery.prepare(QStringLiteral(
        "select factor_definition_sha256, content_sha256 "
        "from external_factor_datasets where dataset_id=?"));
    datasetQuery.addBindValue(datasetId);
    if (!datasetQuery.exec() || !datasetQuery.next()) {
        if (error) *error = QStringLiteral("授权外部因子数据集不存在");
        return false;
    }
    const QString definitionSha = datasetQuery.value(0).toString();
    const QString datasetContentSha = datasetQuery.value(1).toString();
    if (candidate.computedFactorDefinitionSha256() != definitionSha) {
        if (error) *error = QStringLiteral("候选因子定义与外部数据集定义摘要不匹配");
        return false;
    }

    QSqlQuery existing(m_database);
    existing.prepare(QStringLiteral(
        "select binding_id, candidate_content_sha256, bound_at_utc "
        "from factor_candidate_dataset_bindings where candidate_id=? "
        "and candidate_version=? and dataset_id=?"));
    existing.addBindValue(candidateId);
    existing.addBindValue(candidateVersion);
    existing.addBindValue(datasetId);
    if (!existing.exec()) {
        if (error) *error = existing.lastError().text();
        return false;
    }
    binding.candidateId = candidateId;
    binding.candidateVersion = candidateVersion;
    binding.factorDefinitionSha256 = definitionSha;
    binding.datasetId = datasetId;
    binding.datasetContentSha256 = datasetContentSha;
    if (existing.next()) {
        binding.bindingId = existing.value(0).toString();
        binding.candidateContentSha256 = existing.value(1).toString();
        binding.boundAt = QDateTime::fromString(existing.value(2).toString(),
                                               Qt::ISODateWithMs);
        if (reused) *reused = true;
        return true;
    }

    binding.bindingId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    binding.candidateContentSha256 = candidate.contentSha256;
    binding.boundAt = QDateTime::currentDateTimeUtc();
    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "insert into factor_candidate_dataset_bindings(binding_id, candidate_id, "
        "candidate_version, candidate_content_sha256, factor_definition_sha256, "
        "dataset_id, dataset_content_sha256, bound_at_utc) "
        "values(?, ?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(binding.bindingId);
    insert.addBindValue(binding.candidateId);
    insert.addBindValue(binding.candidateVersion);
    insert.addBindValue(binding.candidateContentSha256);
    insert.addBindValue(binding.factorDefinitionSha256);
    insert.addBindValue(binding.datasetId);
    insert.addBindValue(binding.datasetContentSha256);
    insert.addBindValue(binding.boundAt.toString(Qt::ISODateWithMs));
    if (!insert.exec()) {
        if (error) *error = insert.lastError().text();
        return false;
    }
    return true;
}

QVector<ExternalFactorDatasetBinding>
SqliteLocalDataStore::loadExternalFactorDatasetBindings(
    const QString &candidateId, int candidateVersion, int limit)
{
    QVector<ExternalFactorDatasetBinding> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select binding_id, candidate_content_sha256, factor_definition_sha256, "
        "dataset_id, dataset_content_sha256, bound_at_utc "
        "from factor_candidate_dataset_bindings where candidate_id=? "
        "and candidate_version=? order by bound_at_utc desc, binding_id asc limit ?"));
    query.addBindValue(candidateId);
    query.addBindValue(candidateVersion);
    query.addBindValue(std::clamp(limit, 1, 10000));
    if (!query.exec()) return result;
    while (query.next()) {
        ExternalFactorDatasetBinding binding;
        binding.bindingId = query.value(0).toString();
        binding.candidateId = candidateId;
        binding.candidateVersion = candidateVersion;
        binding.candidateContentSha256 = query.value(1).toString();
        binding.factorDefinitionSha256 = query.value(2).toString();
        binding.datasetId = query.value(3).toString();
        binding.datasetContentSha256 = query.value(4).toString();
        binding.boundAt = QDateTime::fromString(query.value(5).toString(),
                                               Qt::ISODateWithMs);
        result.append(binding);
    }
    return result;
}

bool SqliteLocalDataStore::saveExternalFactorResearchArchive(
    ExternalFactorResearchArchiveRecord record, QString *error)
{
    if (error) error->clear();
    if (record.identitySha256.isEmpty())
        record.identitySha256 = record.computedIdentitySha256();
    QString validationError;
    if (!record.isValid(&validationError)) {
        if (error) *error = validationError;
        return false;
    }

    QSqlQuery binding(m_database);
    binding.prepare(QStringLiteral(
        "select c.content_sha256, c.frozen_at_utc, d.content_sha256, "
        "d.factor_definition_sha256, d.factor_key "
        "from factor_candidate_dataset_bindings b "
        "join factor_candidate_versions c on c.candidate_id=b.candidate_id "
        "and c.version=b.candidate_version "
        "join external_factor_datasets d on d.dataset_id=b.dataset_id "
        "where b.candidate_id=? and b.candidate_version=? and b.dataset_id=?"));
    binding.addBindValue(record.candidateId);
    binding.addBindValue(record.candidateVersion);
    binding.addBindValue(record.datasetId);
    if (!binding.exec() || !binding.next()) {
        if (error) *error = QStringLiteral("研究归档缺少冻结候选与授权数据集绑定");
        return false;
    }
    if (binding.value(1).toString().isEmpty()
        || binding.value(0).toString() != record.candidateContentSha256
        || binding.value(2).toString() != record.datasetContentSha256
        || binding.value(3).toString() != record.factorDefinitionSha256
        || binding.value(4).toString() != record.factorKey) {
        if (error) *error = QStringLiteral("研究归档候选或授权数据身份不匹配");
        return false;
    }

    QSqlQuery existing(m_database);
    existing.prepare(QStringLiteral(
        "select identity_sha256 from external_factor_research_archives "
        "where result_id=? or identity_sha256=?"));
    existing.addBindValue(record.resultId);
    existing.addBindValue(record.identitySha256);
    if (!existing.exec()) {
        if (error) *error = existing.lastError().text();
        return false;
    }
    if (existing.next()) {
        if (existing.value(0).toString() == record.identitySha256) return true;
        if (error) *error = QStringLiteral("研究归档 ID 已存在但身份不同");
        return false;
    }

    if (!m_database.transaction()) {
        if (error) *error = m_database.lastError().text();
        return false;
    }
    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "insert into external_factor_research_archives("
        "result_id, completed_at_utc, candidate_id, candidate_version, "
        "dataset_id, factor_key, period_seconds, evaluated_frame_count, "
        "mean_ic, mean_rank_ic, identity_sha256, record_json) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(record.resultId);
    insert.addBindValue(record.completedAt.toUTC().toString(Qt::ISODateWithMs));
    insert.addBindValue(record.candidateId);
    insert.addBindValue(record.candidateVersion);
    insert.addBindValue(record.datasetId);
    insert.addBindValue(record.factorKey);
    insert.addBindValue(record.periodSeconds);
    insert.addBindValue(record.summary.evaluatedFrameCount);
    insert.addBindValue(record.summary.meanIc);
    insert.addBindValue(record.summary.meanRankIc);
    insert.addBindValue(record.identitySha256);
    insert.addBindValue(compactJson(record.toVariantMap()));
    if (!insert.exec() || !m_database.commit()) {
        const QString message = insert.lastError().text().isEmpty()
            ? m_database.lastError().text() : insert.lastError().text();
        m_database.rollback();
        if (error) *error = message;
        return false;
    }
    return true;
}

QVector<ExternalFactorResearchArchiveSummary>
SqliteLocalDataStore::loadExternalFactorResearchArchiveSummaries(int limit)
{
    QVector<ExternalFactorResearchArchiveSummary> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select result_id, completed_at_utc, candidate_id, candidate_version, "
        "dataset_id, factor_key, period_seconds, evaluated_frame_count, "
        "mean_ic, mean_rank_ic, identity_sha256 "
        "from external_factor_research_archives "
        "order by completed_at_utc desc, result_id asc limit ?"));
    query.addBindValue(std::clamp(limit, 1, 10000));
    if (!query.exec()) return result;
    while (query.next()) {
        ExternalFactorResearchArchiveSummary summary;
        summary.resultId = query.value(0).toString();
        summary.completedAt = QDateTime::fromString(
            query.value(1).toString(), Qt::ISODateWithMs);
        summary.candidateId = query.value(2).toString();
        summary.candidateVersion = query.value(3).toInt();
        summary.datasetId = query.value(4).toString();
        summary.factorKey = query.value(5).toString();
        summary.periodSeconds = query.value(6).toInt();
        summary.evaluatedFrameCount = query.value(7).toInt();
        summary.meanIc = query.value(8).toDouble();
        summary.meanRankIc = query.value(9).toDouble();
        summary.identitySha256 = query.value(10).toString();
        result.append(summary);
    }
    return result;
}

std::optional<ExternalFactorResearchArchiveRecord>
SqliteLocalDataStore::loadExternalFactorResearchArchive(
    const QString &resultId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select record_json, identity_sha256 "
        "from external_factor_research_archives where result_id=?"));
    query.addBindValue(resultId);
    if (!query.exec() || !query.next()) return std::nullopt;
    const auto record = ExternalFactorResearchArchiveRecord::fromVariantMap(
        variantMapFromJson(query.value(0).toString()));
    if (!record || record->identitySha256 != query.value(1).toString())
        return std::nullopt;
    return record;
}

bool SqliteLocalDataStore::saveRawTickParameterSensitivityResult(
    const RawTickParameterSensitivityResult &record, QString *error)
{
    if (error) error->clear();
    bool foldIdentityValid = record.analysisMode == QStringLiteral("single_holdout")
        && record.walkForwardFoldCount == 1;
    if (record.analysisMode == QStringLiteral("nested_walk_forward")
        && record.walkForwardFoldCount >= 2
        && record.folds.size() == record.walkForwardFoldCount) {
        foldIdentityValid = true;
        for (int index = 0; index < record.folds.size(); ++index) {
            const RawTickParameterSensitivityFold &fold = record.folds.at(index);
            const bool foldValid = fold.foldNumber == index + 1
                && fold.trainingStartDate.isValid()
                && fold.trainingEndDate >= fold.trainingStartDate
                && fold.validationStartDate > fold.trainingEndDate
                && fold.validationEndDate >= fold.validationStartDate
                && fold.trainingActualTicks > 0
                && fold.validationActualTicks > 0
                && isSha256(fold.trainingDataVersion)
                && isSha256(fold.validationDataVersion)
                && !fold.selectedParameters.isEmpty()
                && !fold.candidates.isEmpty();
            if (!foldValid || (index > 0
                && (fold.trainingStartDate != record.folds.first().trainingStartDate
                    || fold.trainingEndDate != record.folds.at(index - 1)
                                                   .validationEndDate
                    || fold.validationStartDate
                        <= record.folds.at(index - 1).validationEndDate))) {
                foldIdentityValid = false;
                break;
            }
        }
        if (foldIdentityValid) {
            const RawTickParameterSensitivityFold &finalFold = record.folds.last();
            foldIdentityValid = record.trainingStartDate
                    == finalFold.trainingStartDate
                && record.trainingEndDate == finalFold.trainingEndDate
                && record.validationStartDate == finalFold.validationStartDate
                && record.validationEndDate == finalFold.validationEndDate
                && record.trainingActualTicks == finalFold.trainingActualTicks
                && record.validationActualTicks == finalFold.validationActualTicks
                && record.trainingDataVersion == finalFold.trainingDataVersion
                && record.validationDataVersion == finalFold.validationDataVersion
                && record.selectedParameters == finalFold.selectedParameters;
        }
    }
    const bool valid = !record.resultId.trimmed().isEmpty()
        && record.completedAt.isValid()
        && !record.strategyId.trimmed().isEmpty()
        && !record.instrument.symbol.trimmed().isEmpty()
        && record.instrument.market == Market::AShare
        && record.initialCapital > 0.0 && record.commissionRate >= 0.0
        && record.slippageRate >= 0.0 && record.lotSize > 0
        && record.trainingStartDate.isValid()
        && record.trainingEndDate >= record.trainingStartDate
        && record.validationStartDate > record.trainingEndDate
        && record.validationEndDate >= record.validationStartDate
        && record.trainingActualTicks > 0 && record.validationActualTicks > 0
        && isSha256(record.trainingDataVersion)
        && isSha256(record.validationDataVersion)
        && isSha256(record.datasetVersion)
        && isSha256(record.candidateSetSha256)
        && isSha256(record.identitySha256)
        && !record.selectionCriterion.isEmpty()
        && !record.selectedParameters.isEmpty()
        && !record.candidates.isEmpty() && foldIdentityValid
        && record.researchOnly;
    if (!valid) {
        if (error) {
            *error = foldIdentityValid
                ? QStringLiteral("原始 Tick 参数敏感性归档身份无效")
                : QStringLiteral("原始 Tick 参数敏感性外层折身份无效");
        }
        return false;
    }

    QSqlQuery existing(m_database);
    existing.prepare(QStringLiteral(
        "select result_id, identity_sha256 from raw_tick_parameter_sensitivity_results "
        "where result_id=? or identity_sha256=?"));
    existing.addBindValue(record.resultId);
    existing.addBindValue(record.identitySha256);
    if (!existing.exec()) {
        if (error) *error = existing.lastError().text();
        return false;
    }
    if (existing.next()) {
        const QString storedResultId = existing.value(0).toString();
        const QString storedIdentity = existing.value(1).toString();
        if (storedResultId == record.resultId
            && storedIdentity == record.identitySha256) return true;
        if (error) {
            *error = storedIdentity == record.identitySha256
                ? QStringLiteral("相同参数敏感性身份已归档为：%1")
                      .arg(storedResultId)
                : QStringLiteral("参数敏感性结果 ID 已存在但身份不同");
        }
        return false;
    }

    if (!m_database.transaction()) {
        if (error) *error = m_database.lastError().text();
        return false;
    }
    QSqlQuery insert(m_database);
    insert.prepare(QStringLiteral(
        "insert into raw_tick_parameter_sensitivity_results("
        "result_id, completed_at_utc, strategy_id, symbol, training_start_date, "
        "training_end_date, validation_start_date, validation_end_date, "
        "candidate_count, validation_return_percent, dataset_version, "
        "identity_sha256, record_json) values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(record.resultId);
    insert.addBindValue(record.completedAt.toUTC().toString(Qt::ISODateWithMs));
    insert.addBindValue(record.strategyId);
    insert.addBindValue(record.instrument.symbol);
    insert.addBindValue(record.trainingStartDate.toString(Qt::ISODate));
    insert.addBindValue(record.trainingEndDate.toString(Qt::ISODate));
    insert.addBindValue(record.validationStartDate.toString(Qt::ISODate));
    insert.addBindValue(record.validationEndDate.toString(Qt::ISODate));
    insert.addBindValue(record.candidates.size());
    insert.addBindValue(record.validationSummary.totalReturnPercent);
    insert.addBindValue(record.datasetVersion);
    insert.addBindValue(record.identitySha256);
    insert.addBindValue(compactJson(record.toVariantMap()));
    if (!insert.exec() || !m_database.commit()) {
        const QString message = insert.lastError().text().isEmpty()
            ? m_database.lastError().text() : insert.lastError().text();
        m_database.rollback();
        if (error) *error = message;
        return false;
    }
    return true;
}

QVector<RawTickParameterSensitivityResult>
SqliteLocalDataStore::loadRawTickParameterSensitivityResults(int limit)
{
    QVector<RawTickParameterSensitivityResult> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select record_json, identity_sha256 "
        "from raw_tick_parameter_sensitivity_results "
        "order by completed_at_utc desc, result_id asc limit ?"));
    query.addBindValue(std::clamp(limit, 1, 10000));
    if (!query.exec()) return result;
    while (query.next()) {
        const auto record = RawTickParameterSensitivityResult::fromVariantMap(
            variantMapFromJson(query.value(0).toString()));
        if (record && record->identitySha256 == query.value(1).toString()) {
            result.append(*record);
        }
    }
    return result;
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
    sql += QStringLiteral(" order by created_at_utc desc, rowid desc limit ?");
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

QVector<RawTickImportDayAudit>
SqliteLocalDataStore::loadRawTickImportDayAudits(
    const Instrument &instrument, const QDate &startDate,
    const QDate &endDate)
{
    QVector<RawTickImportDayAudit> result;
    if (instrument.symbol.trimmed().isEmpty() || !startDate.isValid()
        || !endDate.isValid() || startDate > endDate) return result;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select a.trading_date, a.raw_content_sha256, a.audit_version, "
        "a.lines_read, a.valid_records, a.skipped_records, "
        "a.out_of_order_records, a.quality_status, a.source, a.audited_at_utc "
        "from raw_tick_day_quality_audits a "
        "join raw_tick_content_fingerprints f on f.symbol=a.symbol "
        "and f.trading_date=a.trading_date "
        "and f.content_sha256=a.raw_content_sha256 and f.dirty=0 "
        "where a.symbol=? and a.trading_date>=? and a.trading_date<=? "
        "and a.valid_records>0 "
        "order by a.trading_date asc, a.audit_version desc, a.audited_at_utc desc"));
    query.addBindValue(instrument.symbol.trimmed());
    query.addBindValue(startDate.toString(Qt::ISODate));
    query.addBindValue(endDate.toString(Qt::ISODate));
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取原始 Tick 逐日导入质量审计"));
        return result;
    }
    QSet<QDate> selectedDates;
    while (query.next()) {
        const QDate date = QDate::fromString(query.value(0).toString(),
                                             Qt::ISODate);
        if (!date.isValid() || selectedDates.contains(date)) continue;
        RawTickImportDayAudit audit;
        audit.tradingDate = date;
        audit.rawContentSha256 = query.value(1).toString();
        audit.auditVersion = query.value(2).toInt();
        audit.linesRead = query.value(3).toLongLong();
        audit.validRecords = query.value(4).toLongLong();
        audit.skippedRecords = query.value(5).toLongLong();
        audit.outOfOrderRecords = query.value(6).toLongLong();
        audit.qualityStatus = query.value(7).toString();
        audit.source = query.value(8).toString();
        audit.auditedAt = QDateTime::fromString(
            query.value(9).toString(), Qt::ISODateWithMs);
        if (!audit.isValid()) continue;
        selectedDates.insert(date);
        result.append(audit);
    }
    return result;
}

bool SqliteLocalDataStore::upsertBatchBacktestRun(
    const BatchBacktestRunSummary &summary)
{
    const bool rawMode = summary.dataMode == BacktestDataMode::RawTicks;
    const bool legacyCountWindow = !rawMode && !summary.startDate.isValid()
        && !summary.endDate.isValid() && summary.barCount >= 2;
    const int walkForwardFoldCount = summary.holdoutTradingDays > 0
        ? std::max(1, summary.walkForwardFoldCount) : 0;
    if (summary.runId.trimmed().isEmpty() || summary.strategyId.trimmed().isEmpty()
        || summary.frequency.trimmed().isEmpty()
        || (!legacyCountWindow
            && (!summary.startDate.isValid() || !summary.endDate.isValid()
                || summary.startDate > summary.endDate))
        || (rawMode ? summary.periodSeconds != 0 : summary.periodSeconds <= 0)
        || summary.barCount < 0 || summary.totalSymbols <= 0
        || summary.holdoutTradingDays < 0
        || summary.walkForwardFoldCount < 0
        || walkForwardFoldCount > 20
        || (summary.holdoutTradingDays == 0
            && summary.walkForwardFoldCount > 0)
        || (summary.holdoutTradingDays > 0
            && (!rawMode || summary.holdoutTradingDays < 2))) {
        emit writeFailed(QStringLiteral("批量回测运行摘要不完整，未写入"));
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into batch_backtest_runs(run_id, created_at_utc, completed_at_utc, strategy_id, "
        "strategy_parameters_json, data_mode, start_date, end_date, frequency, "
        "period_seconds, bar_count, initial_capital, "
        "commission_rate, slippage_rate, lot_size, holdout_trading_days, "
        "walk_forward_fold_count, "
        "total_symbols, completed_symbols, "
        "succeeded_symbols, failed_symbols, status, error) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "on conflict(run_id) do update set completed_at_utc=excluded.completed_at_utc, "
        "strategy_id=excluded.strategy_id, "
        "strategy_parameters_json=excluded.strategy_parameters_json, "
        "data_mode=excluded.data_mode, start_date=excluded.start_date, end_date=excluded.end_date, "
        "frequency=excluded.frequency, period_seconds=excluded.period_seconds, "
        "bar_count=excluded.bar_count, initial_capital=excluded.initial_capital, "
        "commission_rate=excluded.commission_rate, slippage_rate=excluded.slippage_rate, "
        "lot_size=excluded.lot_size, holdout_trading_days=excluded.holdout_trading_days, "
        "walk_forward_fold_count=excluded.walk_forward_fold_count, "
        "total_symbols=excluded.total_symbols, "
        "completed_symbols=excluded.completed_symbols, "
        "succeeded_symbols=excluded.succeeded_symbols, "
        "failed_symbols=excluded.failed_symbols, status=excluded.status, error=excluded.error"));
    query.addBindValue(summary.runId);
    query.addBindValue(summary.createdAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(summary.completedAt.isValid()
        ? summary.completedAt.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral(""));
    query.addBindValue(summary.strategyId);
    query.addBindValue(compactJson(summary.strategyParameters));
    query.addBindValue(backtestDataModeCode(summary.dataMode));
    query.addBindValue(summary.startDate.toString(Qt::ISODate));
    query.addBindValue(summary.endDate.toString(Qt::ISODate));
    query.addBindValue(summary.frequency);
    query.addBindValue(summary.periodSeconds);
    query.addBindValue(summary.barCount);
    query.addBindValue(summary.initialCapital);
    query.addBindValue(summary.commissionRate);
    query.addBindValue(summary.slippageRate);
    query.addBindValue(summary.lotSize);
    query.addBindValue(summary.holdoutTradingDays);
    query.addBindValue(walkForwardFoldCount);
    query.addBindValue(summary.totalSymbols);
    query.addBindValue(summary.completedSymbols);
    query.addBindValue(summary.succeededSymbols);
    query.addBindValue(summary.failedSymbols);
    query.addBindValue(summary.status);
    query.addBindValue(summary.error.isEmpty() ? QStringLiteral("") : summary.error);
    if (!query.exec()) {
        emit writeFailed(QStringLiteral("写入批量回测运行摘要失败：%1")
                             .arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool SqliteLocalDataStore::upsertBatchBacktestRunItem(
    const BatchBacktestRunItem &item)
{
    if (item.runId.trimmed().isEmpty() || item.instrument.symbol.trimmed().isEmpty()
        || item.status.trimmed().isEmpty()) {
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into batch_backtest_run_items(run_id, symbol, name, market, venue, status, error, "
        "data_version, dataset_version) values(?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "on conflict(run_id, symbol) do update set name=excluded.name, market=excluded.market, "
        "venue=excluded.venue, status=excluded.status, error=excluded.error, "
        "data_version=excluded.data_version, dataset_version=excluded.dataset_version"));
    query.addBindValue(item.runId);
    query.addBindValue(item.instrument.symbol);
    query.addBindValue(item.instrument.name);
    query.addBindValue(static_cast<int>(item.instrument.market));
    query.addBindValue(item.instrument.venue);
    query.addBindValue(item.status);
    query.addBindValue(item.error.isEmpty() ? QStringLiteral("") : item.error);
    query.addBindValue(item.dataVersion.isEmpty() ? QStringLiteral("") : item.dataVersion);
    query.addBindValue(item.datasetVersion.isEmpty()
                           ? QStringLiteral("") : item.datasetVersion);
    if (!query.exec()) {
        emit writeFailed(QStringLiteral("写入批量回测运行明细失败：%1")
                             .arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<BatchBacktestRunSummary> SqliteLocalDataStore::loadBatchBacktestRuns(int limit)
{
    QVector<BatchBacktestRunSummary> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select run_id, created_at_utc, completed_at_utc, strategy_id, "
        "strategy_parameters_json, data_mode, start_date, end_date, frequency, "
        "period_seconds, bar_count, initial_capital, "
        "commission_rate, slippage_rate, lot_size, holdout_trading_days, "
        "walk_forward_fold_count, "
        "total_symbols, completed_symbols, "
        "succeeded_symbols, failed_symbols, status, error "
        "from batch_backtest_runs order by created_at_utc desc, run_id desc limit ?"));
    query.addBindValue(std::max(1, limit));
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取批量回测运行摘要"));
        return result;
    }
    while (query.next()) {
        BatchBacktestRunSummary summary;
        summary.runId = query.value(0).toString();
        summary.createdAt = QDateTime::fromString(
            query.value(1).toString(), Qt::ISODateWithMs);
        summary.completedAt = QDateTime::fromString(
            query.value(2).toString(), Qt::ISODateWithMs);
        summary.strategyId = query.value(3).toString();
        summary.strategyParameters = QJsonDocument::fromJson(
            query.value(4).toString().toUtf8()).object().toVariantMap();
        summary.dataMode = query.value(5).toString() == QStringLiteral("raw_ticks")
            ? BacktestDataMode::RawTicks : BacktestDataMode::Candles;
        summary.startDate = QDate::fromString(query.value(6).toString(), Qt::ISODate);
        summary.endDate = QDate::fromString(query.value(7).toString(), Qt::ISODate);
        summary.frequency = query.value(8).toString();
        summary.periodSeconds = query.value(9).toInt();
        summary.barCount = query.value(10).toInt();
        summary.initialCapital = query.value(11).toDouble();
        summary.commissionRate = query.value(12).toDouble();
        summary.slippageRate = query.value(13).toDouble();
        summary.lotSize = query.value(14).toInt();
        summary.holdoutTradingDays = query.value(15).toInt();
        summary.walkForwardFoldCount = query.value(16).toInt();
        if (summary.holdoutTradingDays > 0
            && summary.walkForwardFoldCount == 0) {
            summary.walkForwardFoldCount = 1;
        }
        summary.totalSymbols = query.value(17).toInt();
        summary.completedSymbols = query.value(18).toInt();
        summary.succeededSymbols = query.value(19).toInt();
        summary.failedSymbols = query.value(20).toInt();
        summary.status = query.value(21).toString();
        summary.error = query.value(22).toString();
        result.append(summary);
    }
    return result;
}

QVector<BatchBacktestRunItem> SqliteLocalDataStore::loadBatchBacktestRunItems(
    const QString &runId)
{
    QVector<BatchBacktestRunItem> result;
    if (runId.trimmed().isEmpty()) return result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select symbol, name, market, venue, status, error, data_version, dataset_version "
        "from batch_backtest_run_items where run_id = ? order by symbol asc"));
    query.addBindValue(runId.trimmed());
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取批量回测运行明细"));
        return result;
    }
    while (query.next()) {
        BatchBacktestRunItem item;
        item.runId = runId;
        item.instrument.symbol = query.value(0).toString();
        item.instrument.name = query.value(1).toString();
        item.instrument.market = static_cast<Market>(query.value(2).toInt());
        item.instrument.venue = query.value(3).toString();
        item.status = query.value(4).toString();
        item.error = query.value(5).toString();
        item.dataVersion = query.value(6).toString();
        item.datasetVersion = query.value(7).toString();
        result.append(item);
    }
    return result;
}

namespace {

QString positionsToJson(const QVector<PortfolioPosition> &positions)
{
    QJsonArray array;
    for (const PortfolioPosition &position : positions) {
        array.append(QJsonObject{
            {QStringLiteral("symbol"), position.instrument.symbol},
            {QStringLiteral("name"), position.instrument.name},
            {QStringLiteral("quantity"), position.quantity},
            {QStringLiteral("lastPrice"), position.lastPrice},
            {QStringLiteral("marketValue"), position.marketValue},
            {QStringLiteral("weight"), position.weight},
            {QStringLiteral("priceStale"), position.priceStale},
            {QStringLiteral("marketStatus"), position.marketStatus}});
    }
    return QString::fromUtf8(
        QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVector<PortfolioPosition> positionsFromJson(const QString &json)
{
    QVector<PortfolioPosition> result;
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        PortfolioPosition position;
        position.instrument.symbol =
            object.value(QStringLiteral("symbol")).toString();
        position.instrument.name =
            object.value(QStringLiteral("name")).toString();
        position.quantity =
            object.value(QStringLiteral("quantity")).toDouble();
        position.lastPrice =
            object.value(QStringLiteral("lastPrice")).toDouble();
        position.marketValue =
            object.value(QStringLiteral("marketValue")).toDouble();
        position.weight =
            object.value(QStringLiteral("weight")).toDouble();
        position.priceStale =
            object.value(QStringLiteral("priceStale")).toBool(false);
        position.marketStatus = object.value(QStringLiteral("marketStatus"))
            .toString(QStringLiteral("quoted"));
        result.append(position);
    }
    return result;
}

QString stringListToJson(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) array.append(value);
    return QString::fromUtf8(
        QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QStringList stringListFromJson(const QString &json)
{
    QStringList result;
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &value : array) result.append(value.toString());
    return result;
}

QString datasetIdentitiesToJson(
    const QVector<BacktestDatasetIdentity> &identities)
{
    QJsonArray array;
    for (const BacktestDatasetIdentity &identity : identities) {
        const QJsonDocument document = QJsonDocument::fromJson(
            identity.toJson().toUtf8());
        if (document.isObject()) array.append(document.object());
    }
    return QString::fromUtf8(
        QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVector<BacktestDatasetIdentity> datasetIdentitiesFromJson(
    const QString &json)
{
    QVector<BacktestDatasetIdentity> result;
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    result.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) return {};
        BacktestDatasetIdentity identity;
        const QString identityJson = QString::fromUtf8(
            QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
        if (!identity.fromJson(identityJson)) return {};
        result.append(identity);
    }
    return result;
}

bool validateCrossSectionalArchiveIdentity(const PortfolioResult &result,
                                           QString *error)
{
    if (result.resultKind != QStringLiteral("cross_sectional")) return true;
    const QString strategyId = result.immutableIdentity
        .value(QStringLiteral("strategyId")).toString().trimmed();
    const int strategyVersion = result.immutableIdentity
        .value(QStringLiteral("strategyVersion")).toInt();
    const QString definitionJson = result.immutableIdentity
        .value(QStringLiteral("definitionJson")).toString().trimmed();
    const int requestedBars = result.immutableIdentity
        .value(QStringLiteral("requestedBars")).toInt();
    const int actualBars = result.immutableIdentity
        .value(QStringLiteral("actualBars")).toInt();
    if (strategyId.isEmpty() || strategyVersion <= 0
        || definitionJson.isEmpty() || requestedBars <= 0 || actualBars <= 0
        || result.datasetIdentities.size() != result.definition.legs.size()
        || result.datasetVersions.size() != result.datasetIdentities.size()) {
        if (error) *error = QStringLiteral("不可变身份或腿级数据身份不完整");
        return false;
    }
    for (int index = 0; index < result.datasetIdentities.size(); ++index) {
        const BacktestDatasetIdentity &identity =
            result.datasetIdentities.at(index);
        if (identity.isEmpty() || !isSha256(identity.datasetVersion)
            || result.datasetVersions.at(index) != identity.datasetVersion
            || result.definition.legs.at(index).instrument.symbol
                   != identity.instrument.symbol) {
            if (error) *error = QStringLiteral("腿级数据身份无效或与组合定义不一致");
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

bool validateIndustryConstraintIdentity(const PortfolioResult &result,
                                        QString *error)
{
    if (result.definition.maximumIndustryWeight <= 0.0) return true;
    const QVariantMap industryIdentity = result.immutableIdentity
        .value(QStringLiteral("industrySnapshot")).toMap();
    const QString snapshotVersion = industryIdentity
        .value(QStringLiteral("snapshotVersion")).toString();
    if (industryIdentity.value(QStringLiteral("datasetId")).toString()
            != result.definition.industryDatasetId
        || !industryIdentity.value(QStringLiteral("coverageComplete")).toBool()
        || !isSha256(snapshotVersion)
        || result.riskSummary.industrySnapshotVersion != snapshotVersion) {
        if (error) *error = QStringLiteral("行业约束身份不完整");
        return false;
    }
    if (error) error->clear();
    return true;
}

bool validateFactorExposureIdentity(const PortfolioResult &result,
                                    QString *error)
{
    if (result.definition.maximumAbsoluteFactorExposure <= 0.0) return true;
    if (result.resultKind != QStringLiteral("cross_sectional")) {
        if (error) *error = QStringLiteral("因子暴露约束只适用于横截面结果");
        return false;
    }
    const QVariantMap identity = result.immutableIdentity
        .value(QStringLiteral("factorExposure")).toMap();
    const QVariantMap definition = identity
        .value(QStringLiteral("definition")).toMap();
    const QByteArray definitionJson = QJsonDocument::fromVariant(definition)
        .toJson(QJsonDocument::Compact);
    const QString expectedSha256 = QStringLiteral("sha256:%1").arg(
        QString::fromLatin1(QCryptographicHash::hash(
            definitionJson, QCryptographicHash::Sha256).toHex()));
    const QString alias = identity.value(QStringLiteral("alias"))
                              .toString().trimmed();
    if (alias != result.definition.factorExposureAlias
        || definition.value(QStringLiteral("alias")).toString() != alias
        || identity.value(QStringLiteral("definitionSha256")).toString()
               != expectedSha256
        || !isSha256(expectedSha256)
        || identity.value(QStringLiteral("standardization")).toString()
               != QStringLiteral("population_zscore")
        || identity.value(QStringLiteral("weighting")).toString()
               != QStringLiteral("portfolio_equity_including_cash")
        || identity.value(QStringLiteral("limitPolicy")).toString()
               != QStringLiteral("proportional_de_risk_to_cash")
        || result.riskSummary.factorExposureAlias != alias) {
        if (error) *error = QStringLiteral("因子暴露约束身份不完整");
        return false;
    }
    if (error) error->clear();
    return true;
}

bool validateFactorResearchIdentity(const PortfolioResult &result,
                                    QString *error)
{
    if (result.definition.factorResearchAlias.trimmed().isEmpty()) return true;
    if (result.resultKind != QStringLiteral("cross_sectional")) {
        if (error) *error = QStringLiteral("因子研究只适用于横截面结果");
        return false;
    }
    const QVariantMap identity = result.immutableIdentity
        .value(QStringLiteral("factorResearch")).toMap();
    const FactorResearchSummary summary = FactorResearchSummary::fromVariantMap(
        identity.value(QStringLiteral("summary")).toMap());
    if (identity.value(QStringLiteral("factorId")).toString()
            != result.definition.factorDefinitionId
        || identity.value(QStringLiteral("factorVersion")).toInt()
               != result.definition.factorDefinitionVersion
        || identity.value(QStringLiteral("alias")).toString()
               != result.definition.factorResearchAlias
        || identity.value(QStringLiteral("definitionSha256")).toString()
               != result.definition.factorDefinitionSha256
        || !isSha256(result.definition.factorDefinitionSha256)
        || identity.value(QStringLiteral("returnHorizon")).toString()
               != QStringLiteral("next_aligned_frame")
        || identity.value(QStringLiteral("returnCalculation")).toString()
               != QStringLiteral("close_to_close")
        || identity.value(QStringLiteral("frameAggregation")).toString()
               != QStringLiteral("equal_weighted_mean")
        || summary.factorId != result.definition.factorDefinitionId
        || summary.factorVersion != result.definition.factorDefinitionVersion
        || summary.factorAlias != result.definition.factorResearchAlias) {
        if (error) *error = QStringLiteral("因子研究身份不完整");
        return false;
    }
    const bool extendedResearch = !summary.timeSeries.isEmpty()
        || !summary.quantileReturns.isEmpty()
        || summary.icirAvailable || summary.rankIcirAvailable;
    const QString methodVersion = identity.value(
        QStringLiteral("researchMethodVersion")).toString();
    if (extendedResearch
        && ((methodVersion != QStringLiteral("factor_research_v2")
             && methodVersion != QStringLiteral("factor_research_v3")
             && methodVersion != QStringLiteral("factor_research_v4")
             && methodVersion != QStringLiteral("factor_research_v5")
             && methodVersion != QStringLiteral("factor_research_v6"))
            || identity.value(QStringLiteral("icirConvention")).toString()
                   != QStringLiteral("mean_over_sample_sd_unannualized")
            || identity.value(QStringLiteral("quantileCount")).toInt()
                   != summary.quantileCount
            || summary.quantileCount != 5
            || summary.quantileReturns.size() != summary.quantileCount
            || identity.value(QStringLiteral("quantileMethod")).toString()
                   != QStringLiteral("stable_factor_then_symbol_equal_count")
            || identity.value(QStringLiteral("quantileAggregation")).toString()
                   != QStringLiteral(
                       "equal_weighted_within_frame_then_across_frames")
            || identity.value(QStringLiteral("spreadDefinition")).toString()
                   != QStringLiteral("q5_minus_q1"))) {
        if (error) *error = QStringLiteral("因子研究方法身份不完整");
        return false;
    }
    const bool turnoverCostResearch = methodVersion
            == QStringLiteral("factor_research_v3")
        || methodVersion == QStringLiteral("factor_research_v4")
        || methodVersion == QStringLiteral("factor_research_v5")
        || methodVersion == QStringLiteral("factor_research_v6")
        || summary.turnoverObservationCount > 0
        || summary.costPolicy.oneWayCostBps > 0.0;
    const FactorResearchCostPolicy identityCost =
        FactorResearchCostPolicy::fromVariantMap(
            identity.value(QStringLiteral("costPolicy")).toMap());
    const bool supportedCostMethod =
        methodVersion == QStringLiteral("factor_research_v3")
        || methodVersion == QStringLiteral("factor_research_v4")
        || methodVersion == QStringLiteral("factor_research_v5")
        || methodVersion == QStringLiteral("factor_research_v6");
    if (turnoverCostResearch
        && (!supportedCostMethod
            || identity.value(QStringLiteral("turnoverConvention")).toString()
                   != QStringLiteral("half_sum_absolute_weight_change")
            || identity.value(QStringLiteral("initialFormation")).toString()
                   != QStringLiteral("excluded")
            || identity.value(QStringLiteral("costFormula")).toString()
                   != QStringLiteral(
                       "absolute_weight_change_times_one_way_cost_rate")
            || std::abs(identityCost.oneWayCostBps
                        - result.definition.factorResearchCost.oneWayCostBps)
                   > 1e-12
            || std::abs(summary.costPolicy.oneWayCostBps
                        - result.definition.factorResearchCost.oneWayCostBps)
                   > 1e-12)) {
        if (error) *error = QStringLiteral("因子研究换手成本身份不完整");
        return false;
    }
    if (methodVersion == QStringLiteral("factor_research_v5")
        || methodVersion == QStringLiteral("factor_research_v6")) {
        const int frameCount = summary.timeSeries.size();
        const int expectedLag = frameCount >= 20
            ? std::clamp(static_cast<int>(std::floor(
                  4.0 * std::pow(static_cast<double>(frameCount) / 100.0,
                                 2.0 / 9.0))),
                         0, frameCount - 1)
            : 0;
        const auto validInference = [](bool available, double standardError,
                                       double lower, double upper,
                                       double pValue) {
            return !available
                || (std::isfinite(standardError) && standardError > 0.0
                    && std::isfinite(lower) && std::isfinite(upper)
                    && lower <= upper && std::isfinite(pValue)
                    && pValue >= 0.0 && pValue <= 1.0);
        };
        int segmentFrames = 0;
        for (const FactorTemporalSegment &segment : summary.temporalSegments)
            segmentFrames += segment.frameCount;
        if (identity.value(QStringLiteral("inferenceMethodVersion")).toString()
                != QStringLiteral("newey_west_bartlett_v1")
            || identity.value(QStringLiteral("inferenceMinimumFrames")).toInt()
                   != 20
            || std::abs(identity.value(QStringLiteral("confidenceLevel")).toDouble()
                        - 0.95) > 1e-12
            || identity.value(QStringLiteral("hacLagRule")).toString()
                   != QStringLiteral(
                       "floor_4_times_n_over_100_power_2_over_9")
            || identity.value(QStringLiteral("temporalSegmentCount")).toInt()
                   != 3
            || identity.value(QStringLiteral("temporalSegmentMethod")).toString()
                   != QStringLiteral("contiguous_equal_count")
            || summary.inferenceFrameCount != frameCount
            || summary.hacLag != expectedLag
            || std::abs(summary.confidenceLevel - 0.95) > 1e-12
            || !validInference(summary.icInferenceAvailable,
                               summary.icMeanStandardError,
                               summary.icConfidenceLower,
                               summary.icConfidenceUpper,
                               summary.icTwoSidedPValue)
            || !validInference(summary.rankIcInferenceAvailable,
                               summary.rankIcMeanStandardError,
                               summary.rankIcConfidenceLower,
                               summary.rankIcConfidenceUpper,
                               summary.rankIcTwoSidedPValue)
            || (frameCount < 20
                && (summary.icInferenceAvailable
                    || summary.rankIcInferenceAvailable))
            || (frameCount >= 9
                && (!summary.temporalStabilityAvailable
                    || summary.temporalSegments.size() != 3
                    || segmentFrames != frameCount))
            || (frameCount < 9
                && (summary.temporalStabilityAvailable
                    || !summary.temporalSegments.isEmpty()))) {
            if (error) *error = QStringLiteral("因子研究统计推断身份不完整");
            return false;
        }
    }
    if (methodVersion == QStringLiteral("factor_research_v6")) {
        const FactorOutOfSampleSummary &outOfSample = summary.outOfSample;
        const FactorOutOfSamplePolicy identityPolicy =
            FactorOutOfSamplePolicy::fromVariantMap(
                identity.value(QStringLiteral("outOfSamplePolicy")).toMap());
        QString policyError;
        const bool policyValid = identityPolicy.isValid(&policyError);
        const int frameCount = summary.timeSeries.size();
        const int expectedEvaluationFrames = policyValid && frameCount
                >= identityPolicy.initialTrainingFrames
                    + identityPolicy.evaluationFrames
            ? ((frameCount - identityPolicy.initialTrainingFrames)
               / identityPolicy.stepFrames) * identityPolicy.evaluationFrames
            : 0;
        const int expectedWindows = identityPolicy.evaluationFrames > 0
            ? expectedEvaluationFrames / identityPolicy.evaluationFrames : 0;
        const int expectedLag = expectedEvaluationFrames >= 20
            ? std::clamp(static_cast<int>(std::floor(
                  4.0 * std::pow(
                      static_cast<double>(expectedEvaluationFrames) / 100.0,
                      2.0 / 9.0))),
                         0, expectedEvaluationFrames - 1)
            : 0;
        const auto validInference = [](bool available, double standardError,
                                       double lower, double upper,
                                       double pValue) {
            return !available
                || (std::isfinite(standardError) && standardError > 0.0
                    && std::isfinite(lower) && std::isfinite(upper)
                    && lower <= upper && std::isfinite(pValue)
                    && pValue >= 0.0 && pValue <= 1.0);
        };
        bool seriesValid = true;
        for (int index = 0; seriesValid && index < frameCount; ++index) {
            const FactorResearchPoint &point = summary.timeSeries.at(index);
            seriesValid = point.factorTimestamp.isValid()
                && point.returnTimestamp.isValid()
                && std::isfinite(point.ic) && std::isfinite(point.rankIc)
                && (index == 0
                    || summary.timeSeries.at(index - 1).factorTimestamp
                           < point.factorTimestamp);
        }
        bool windowsValid = outOfSample.windows.size() == expectedWindows;
        for (int index = 0; windowsValid && index < expectedWindows; ++index) {
            const FactorOutOfSampleWindow &window =
                outOfSample.windows.at(index);
            const int trainingFrames = identityPolicy.initialTrainingFrames
                + index * identityPolicy.stepFrames;
            const int evaluationFrom = trainingFrames;
            const int evaluationTo = evaluationFrom
                + identityPolicy.evaluationFrames - 1;
            windowsValid = window.window == index + 1
                && window.trainingFrameCount == trainingFrames
                && window.evaluationFrameCount
                       == identityPolicy.evaluationFrames
                && window.trainingFromFactorTimestamp
                       == summary.timeSeries.first().factorTimestamp
                && window.trainingToFactorTimestamp
                       == summary.timeSeries.at(trainingFrames - 1)
                              .factorTimestamp
                && window.evaluationFromFactorTimestamp
                       == summary.timeSeries.at(evaluationFrom).factorTimestamp
                && window.evaluationToFactorTimestamp
                       == summary.timeSeries.at(evaluationTo).factorTimestamp
                && window.trainingToFactorTimestamp
                       < window.evaluationFromFactorTimestamp
                && std::isfinite(window.meanIc)
                && std::isfinite(window.meanRankIc);
        }
        const bool shouldBeAvailable = expectedWindows > 0;
        if (identity.value(QStringLiteral("outOfSampleMethodVersion")).toString()
                != QStringLiteral("expanding_oos_v1")
            || identity.value(
                   QStringLiteral("outOfSampleInferenceMethodVersion")).toString()
                   != QStringLiteral("newey_west_bartlett_v1")
            || !policyValid
            || identityPolicy.toVariantMap()
                   != outOfSample.policy.toVariantMap()
            || identity.value(QStringLiteral("outOfSamplePolicy")).toMap()
                   != outOfSample.policy.toVariantMap()
            || summary.toVariantMap()
                   != result.factorResearchSummary.toVariantMap()
            || !seriesValid
            || outOfSample.totalFrameCount != frameCount
            || outOfSample.available != shouldBeAvailable
            || outOfSample.evaluationFrameCount != expectedEvaluationFrames
            || outOfSample.inferenceFrameCount != expectedEvaluationFrames
            || outOfSample.hacLag != expectedLag
            || std::abs(outOfSample.confidenceLevel - 0.95) > 1e-12
            || !windowsValid
            || (shouldBeAvailable
                && (outOfSample.evaluationFromFactorTimestamp
                        != summary.timeSeries.at(
                               identityPolicy.initialTrainingFrames)
                               .factorTimestamp
                    || outOfSample.evaluationToFactorTimestamp
                        != summary.timeSeries.at(
                               identityPolicy.initialTrainingFrames
                               + expectedEvaluationFrames - 1)
                               .factorTimestamp
                    || !std::isfinite(outOfSample.meanIc)
                    || !std::isfinite(outOfSample.meanRankIc)))
            || (!shouldBeAvailable
                && (outOfSample.availabilityReason.trimmed().isEmpty()
                    || !outOfSample.windows.isEmpty()
                    || outOfSample.icInferenceAvailable
                    || outOfSample.rankIcInferenceAvailable))
            || !validInference(outOfSample.icInferenceAvailable,
                               outOfSample.icMeanStandardError,
                               outOfSample.icConfidenceLower,
                               outOfSample.icConfidenceUpper,
                               outOfSample.icTwoSidedPValue)
            || !validInference(outOfSample.rankIcInferenceAvailable,
                               outOfSample.rankIcMeanStandardError,
                               outOfSample.rankIcConfidenceLower,
                               outOfSample.rankIcConfidenceUpper,
                               outOfSample.rankIcTwoSidedPValue)
            || (expectedEvaluationFrames < 20
                && (outOfSample.icInferenceAvailable
                    || outOfSample.rankIcInferenceAvailable))) {
            if (error) *error = QStringLiteral("因子研究样本外身份不完整");
            return false;
        }
    }
    if (methodVersion == QStringLiteral("factor_research_v4")
        || methodVersion == QStringLiteral("factor_research_v5")
        || methodVersion == QStringLiteral("factor_research_v6")) {
        const FactorNeutralizationPolicy identityPolicy =
            FactorNeutralizationPolicy::fromVariantMap(
                identity.value(QStringLiteral("neutralizationPolicy")).toMap());
        QString policyError;
        if (!identityPolicy.isValid(&policyError)
            || identityPolicy.toVariantMap()
                   != result.definition.factorNeutralization.toVariantMap()
            || summary.neutralizationPolicy.toVariantMap()
                   != result.definition.factorNeutralization.toVariantMap()
            || identity.value(QStringLiteral("preprocessingOrder")).toString()
                   != QStringLiteral(
                       "pairwise_drop_then_global_winsorize_then_group_demean")) {
            if (error) *error = QStringLiteral("因子研究中性化策略身份不完整");
            return false;
        }
        if (identityPolicy.enabled()) {
            const QVariantMap industryIdentity = result.immutableIdentity
                .value(QStringLiteral("industrySnapshot")).toMap();
            const QString snapshotVersion = industryIdentity
                .value(QStringLiteral("snapshotVersion")).toString();
            const QDate asOfDate = industryIdentity
                .value(QStringLiteral("asOfDate")).toDate();
            if (identity.value(QStringLiteral("neutralizationDatasetId")).toString()
                    != result.definition.industryDatasetId
                || identity.value(
                       QStringLiteral("neutralizationSnapshotVersion")).toString()
                       != snapshotVersion
                || identity.value(QStringLiteral("neutralizationAsOfDate")).toDate()
                       != asOfDate
                || industryIdentity.value(QStringLiteral("datasetId")).toString()
                       != result.definition.industryDatasetId
                || !industryIdentity.value(
                       QStringLiteral("coverageComplete")).toBool()
                || !asOfDate.isValid() || !isSha256(snapshotVersion)
                || result.riskSummary.industrySnapshotVersion
                       != snapshotVersion) {
                if (error) *error = QStringLiteral("因子研究行业快照身份不完整");
                return false;
            }
        }
    } else if (result.definition.factorNeutralization.enabled()) {
        if (error) *error = QStringLiteral("因子研究中性化需要 v4/v5/v6 方法身份");
        return false;
    }
    if (error) error->clear();
    return true;
}

} // namespace

bool SqliteLocalDataStore::savePortfolioBacktestArchive(
    const PortfolioArchiveRecord &record, QString *error)
{
    if (error) error->clear();
    const PortfolioResult &result = record.result;
    if (record.resultId.trimmed().isEmpty() || !record.completedAt.isValid()
        || !result.definition.isValid() || result.equityCurve.isEmpty()) {
        const QString message = QStringLiteral("组合回测结果身份不完整，未写入归档");
        if (error) *error = message;
        emit writeFailed(message);
        return false;
    }
    QString crossSectionalIdentityError;
    if (!validateCrossSectionalArchiveIdentity(
            result, &crossSectionalIdentityError)) {
        const QString message = QStringLiteral("横截面回测%1，未写入归档")
                                    .arg(crossSectionalIdentityError);
        if (error) *error = message;
        emit writeFailed(message);
        return false;
    }
    QString industryIdentityError;
    if (!validateIndustryConstraintIdentity(result, &industryIdentityError)) {
        const QString message = QStringLiteral("组合%1，未写入归档")
                                    .arg(industryIdentityError);
        if (error) *error = message;
        emit writeFailed(message);
        return false;
    }
    QString factorIdentityError;
    if (!validateFactorExposureIdentity(result, &factorIdentityError)) {
        const QString message = QStringLiteral("组合%1，未写入归档")
                                    .arg(factorIdentityError);
        if (error) *error = message;
        emit writeFailed(message);
        return false;
    }
    QString factorResearchIdentityError;
    if (!validateFactorResearchIdentity(result, &factorResearchIdentityError)) {
        const QString message = QStringLiteral("组合%1，未写入归档")
                                    .arg(factorResearchIdentityError);
        if (error) *error = message;
        emit writeFailed(message);
        return false;
    }
    if (!m_database.transaction()) {
        if (error) *error = QStringLiteral("无法开始组合回测结果归档事务");
        emitSqlError(QStringLiteral("开始组合回测结果归档事务"));
        return false;
    }
    auto fail = [this, error](const QString &operation, const QSqlQuery &query) {
        m_database.rollback();
        const QString message = QStringLiteral("%1失败：%2")
                                    .arg(operation, query.lastError().text());
        if (error) *error = message;
        emit writeFailed(message);
        return false;
    };

    QSqlQuery resultQuery(m_database);
    resultQuery.prepare(QStringLiteral(
        "insert into portfolio_backtest_results(result_id, portfolio_id, name, "
        "completed_at_utc, definition_json, leg_count, period_seconds, bar_count, "
        "dataset_versions_json, result_kind, immutable_identity_json, "
        "dataset_identities_json, initial_capital, final_equity, total_return_percent, "
        "max_drawdown_percent, sharpe_ratio, total_commission, total_trades) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    resultQuery.addBindValue(record.resultId);
    resultQuery.addBindValue(result.definition.portfolioId);
    resultQuery.addBindValue(result.definition.name);
    resultQuery.addBindValue(record.completedAt.toUTC().toString(Qt::ISODateWithMs));
    resultQuery.addBindValue(compactJson(result.definition.toVariantMap()));
    resultQuery.addBindValue(result.definition.legs.size());
    resultQuery.addBindValue(result.periodSeconds);
    resultQuery.addBindValue(result.equityCurve.size());
    resultQuery.addBindValue(stringListToJson(result.datasetVersions));
    resultQuery.addBindValue(result.resultKind);
    resultQuery.addBindValue(compactJson(result.immutableIdentity));
    resultQuery.addBindValue(datasetIdentitiesToJson(result.datasetIdentities));
    resultQuery.addBindValue(result.summary.initialCapital);
    resultQuery.addBindValue(result.summary.finalEquity);
    resultQuery.addBindValue(result.summary.totalReturnPercent);
    resultQuery.addBindValue(result.summary.maxDrawdownPercent);
    resultQuery.addBindValue(result.summary.sharpeRatio);
    resultQuery.addBindValue(result.summary.totalCommission);
    resultQuery.addBindValue(result.summary.totalTrades);
    if (!resultQuery.exec()) return fail(QStringLiteral("写入组合回测结果"), resultQuery);

    QSqlQuery equityQuery(m_database);
    equityQuery.prepare(QStringLiteral(
        "insert into portfolio_equity_points(result_id, sequence_no, timestamp_utc, "
        "equity, cash, positions_json) values(?, ?, ?, ?, ?, ?)"));
    for (int index = 0; index < result.equityCurve.size(); ++index) {
        const PortfolioBar &bar = result.equityCurve.at(index);
        equityQuery.bindValue(0, record.resultId);
        equityQuery.bindValue(1, index);
        equityQuery.bindValue(2, bar.timestamp.toUTC().toString(Qt::ISODateWithMs));
        equityQuery.bindValue(3, bar.equity);
        equityQuery.bindValue(4, bar.cash);
        equityQuery.bindValue(5, positionsToJson(bar.positions));
        if (!equityQuery.exec()) return fail(QStringLiteral("写入组合权益曲线"), equityQuery);
    }

    QSqlQuery tradeQuery(m_database);
    tradeQuery.prepare(QStringLiteral(
        "insert into portfolio_trades(result_id, sequence_no, symbol, name, market, venue, "
        "timestamp_utc, side, quantity, price, commission, amount, reason) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (int index = 0; index < result.trades.size(); ++index) {
        const PortfolioTrade &trade = result.trades.at(index);
        tradeQuery.bindValue(0, record.resultId);
        tradeQuery.bindValue(1, index);
        tradeQuery.bindValue(2, trade.instrument.symbol);
        tradeQuery.bindValue(3, trade.instrument.name);
        tradeQuery.bindValue(4, static_cast<int>(trade.instrument.market));
        tradeQuery.bindValue(5, trade.instrument.venue);
        tradeQuery.bindValue(6, trade.timestamp.toUTC().toString(Qt::ISODateWithMs));
        tradeQuery.bindValue(7, trade.side);
        tradeQuery.bindValue(8, trade.quantity);
        tradeQuery.bindValue(9, trade.price);
        tradeQuery.bindValue(10, trade.commission);
        tradeQuery.bindValue(11, trade.amount);
        tradeQuery.bindValue(12, trade.reason);
        if (!tradeQuery.exec()) return fail(QStringLiteral("写入组合回测成交"), tradeQuery);
    }

    if (!m_database.commit()) {
        m_database.rollback();
        if (error) *error = QStringLiteral("提交组合回测结果归档事务失败");
        emitSqlError(QStringLiteral("提交组合回测结果归档事务"));
        return false;
    }
    return true;
}

QVector<PortfolioArchiveSummary>
SqliteLocalDataStore::loadPortfolioBacktestArchiveSummaries(int limit)
{
    QVector<PortfolioArchiveSummary> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select result_id, portfolio_id, name, completed_at_utc, leg_count, "
        "period_seconds, bar_count, dataset_versions_json, result_kind, "
        "immutable_identity_json, initial_capital, "
        "final_equity, total_return_percent, max_drawdown_percent, sharpe_ratio, "
        "total_commission, total_trades from portfolio_backtest_results "
        "order by completed_at_utc desc, rowid desc limit ?"));
    query.addBindValue(std::max(1, limit));
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取组合回测归档列表"));
        return result;
    }
    while (query.next()) {
        PortfolioArchiveSummary summary;
        summary.resultId = query.value(0).toString();
        summary.portfolioId = query.value(1).toString();
        summary.name = query.value(2).toString();
        summary.completedAt = QDateTime::fromString(
            query.value(3).toString(), Qt::ISODateWithMs);
        summary.legCount = query.value(4).toInt();
        summary.periodSeconds = query.value(5).toInt();
        summary.barCount = query.value(6).toInt();
        summary.datasetVersions =
            stringListFromJson(query.value(7).toString());
        summary.resultKind = query.value(8).toString();
        const QVariantMap immutableIdentity =
            variantMapFromJson(query.value(9).toString());
        summary.strategyId = immutableIdentity
            .value(QStringLiteral("strategyId")).toString();
        summary.strategyVersion = immutableIdentity
            .value(QStringLiteral("strategyVersion")).toInt();
        summary.summary.initialCapital = query.value(10).toDouble();
        summary.summary.finalEquity = query.value(11).toDouble();
        summary.summary.totalReturnPercent = query.value(12).toDouble();
        summary.summary.maxDrawdownPercent = query.value(13).toDouble();
        summary.summary.sharpeRatio = query.value(14).toDouble();
        summary.summary.totalCommission = query.value(15).toDouble();
        summary.summary.totalTrades = query.value(16).toInt();
        result.append(summary);
    }
    return result;
}

std::optional<PortfolioArchiveRecord>
SqliteLocalDataStore::loadPortfolioBacktestArchive(const QString &resultId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select portfolio_id, name, completed_at_utc, definition_json, "
        "dataset_versions_json, result_kind, immutable_identity_json, "
        "dataset_identities_json from portfolio_backtest_results where result_id = ?"));
    query.addBindValue(resultId);
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取组合回测归档身份"));
        return std::nullopt;
    }
    if (!query.next()) return std::nullopt;

    PortfolioArchiveRecord record;
    record.resultId = resultId;
    record.completedAt = QDateTime::fromString(
        query.value(2).toString(), Qt::ISODateWithMs);
    PortfolioDefinition &definition = record.result.definition;
    definition.portfolioId = query.value(0).toString();
    definition.name = query.value(1).toString();
    const QVariantMap definitionMap = QJsonDocument::fromJson(
        query.value(3).toString().toUtf8()).object().toVariantMap();
    const QVariantList legList = definitionMap.value(QStringLiteral("legs")).toList();
    for (const QVariant &value : legList) {
        const QVariantMap legMap = value.toMap();
        PortfolioLeg leg;
        leg.instrument.symbol =
            legMap.value(QStringLiteral("symbol")).toString();
        leg.instrument.name =
            legMap.value(QStringLiteral("name")).toString();
        leg.targetWeight =
            legMap.value(QStringLiteral("targetWeight")).toDouble();
    definition.legs.append(leg);
    }
    definition.periodSeconds =
        definitionMap.value(QStringLiteral("periodSeconds")).toInt();
    record.result.periodSeconds = definition.periodSeconds;
    definition.rebalanceEveryTradingDays =
        definitionMap.value(QStringLiteral("rebalanceEveryTradingDays")).toInt();
    definition.initialCapital =
        definitionMap.value(QStringLiteral("initialCapital")).toDouble();
    definition.commissionRate =
        definitionMap.value(QStringLiteral("commissionRate")).toDouble();
    definition.slippageRate =
        definitionMap.value(QStringLiteral("slippageRate")).toDouble();
    definition.lotSize =
        definitionMap.value(QStringLiteral("lotSize")).toInt();
    definition.tPlusOne =
        definitionMap.value(QStringLiteral("tPlusOne")).toBool();
    definition.maximumLegWeight =
        definitionMap.value(QStringLiteral("maximumLegWeight")).toDouble();
    definition.maximumActiveLegs =
        definitionMap.value(QStringLiteral("maximumActiveLegs")).toInt();
    definition.maximumConsecutiveMissingBars = definitionMap
        .value(QStringLiteral("maximumConsecutiveMissingBars")).toInt();
    definition.maximumPortfolioLossPercent = definitionMap
        .value(QStringLiteral("maximumPortfolioLossPercent")).toDouble();
    definition.maximumPortfolioDrawdownPercent = definitionMap
        .value(QStringLiteral("maximumPortfolioDrawdownPercent")).toDouble();
    definition.maximumIndustryWeight = definitionMap
        .value(QStringLiteral("maximumIndustryWeight")).toDouble();
    definition.industryDatasetId = definitionMap
        .value(QStringLiteral("industryDatasetId")).toString();
    definition.factorExposureAlias = definitionMap
        .value(QStringLiteral("factorExposureAlias")).toString();
    definition.maximumAbsoluteFactorExposure = definitionMap
        .value(QStringLiteral("maximumAbsoluteFactorExposure")).toDouble();
    definition.factorResearchAlias = definitionMap
        .value(QStringLiteral("factorResearchAlias")).toString();
    definition.factorDefinitionId = definitionMap
        .value(QStringLiteral("factorDefinitionId")).toString();
    definition.factorDefinitionVersion = definitionMap
        .value(QStringLiteral("factorDefinitionVersion")).toInt();
    definition.factorDefinitionSha256 = definitionMap
        .value(QStringLiteral("factorDefinitionSha256")).toString();
    definition.factorPreprocessing = FactorPreprocessingPolicy::fromVariantMap(
        definitionMap.value(QStringLiteral("factorPreprocessing")).toMap());
    definition.factorResearchCost = FactorResearchCostPolicy::fromVariantMap(
        definitionMap.value(QStringLiteral("factorResearchCost")).toMap());
    definition.factorNeutralization =
        FactorNeutralizationPolicy::fromVariantMap(
            definitionMap.value(QStringLiteral("factorNeutralization")).toMap());
    record.result.datasetVersions =
        stringListFromJson(query.value(4).toString());
    record.result.resultKind = query.value(5).toString();
    record.result.immutableIdentity =
        variantMapFromJson(query.value(6).toString());
    record.result.riskSummary = PortfolioRiskSummary::fromVariantMap(
        record.result.immutableIdentity
            .value(QStringLiteral("riskSummary")).toMap());
    record.result.factorResearchSummary = FactorResearchSummary::fromVariantMap(
        record.result.immutableIdentity.value(QStringLiteral("factorResearch"))
            .toMap().value(QStringLiteral("summary")).toMap());
    record.result.datasetIdentities =
        datasetIdentitiesFromJson(query.value(7).toString());
    QString crossSectionalIdentityError;
    if (!validateCrossSectionalArchiveIdentity(
            record.result, &crossSectionalIdentityError)) {
        emit writeFailed(QStringLiteral("横截面回测归档 %1 %2，拒绝恢复")
                             .arg(resultId, crossSectionalIdentityError));
        return std::nullopt;
    }
    QString industryIdentityError;
    if (!validateIndustryConstraintIdentity(
            record.result, &industryIdentityError)) {
        emit writeFailed(QStringLiteral("组合回测归档 %1 %2，拒绝恢复")
                             .arg(resultId, industryIdentityError));
        return std::nullopt;
    }
    QString factorIdentityError;
    if (!validateFactorExposureIdentity(record.result, &factorIdentityError)) {
        emit writeFailed(QStringLiteral("组合回测归档 %1 %2，拒绝恢复")
                             .arg(resultId, factorIdentityError));
        return std::nullopt;
    }
    QString factorResearchIdentityError;
    if (!validateFactorResearchIdentity(
            record.result, &factorResearchIdentityError)) {
        emit writeFailed(QStringLiteral("组合回测归档 %1 %2，拒绝恢复")
                             .arg(resultId, factorResearchIdentityError));
        return std::nullopt;
    }

    QSqlQuery equityQuery(m_database);
    equityQuery.prepare(QStringLiteral(
        "select timestamp_utc, equity, cash, positions_json "
        "from portfolio_equity_points where result_id = ? order by sequence_no asc"));
    equityQuery.addBindValue(resultId);
    if (!equityQuery.exec()) {
        emitSqlError(QStringLiteral("读取组合权益曲线"));
        return std::nullopt;
    }
    while (equityQuery.next()) {
        PortfolioBar bar;
        bar.timestamp = QDateTime::fromString(
            equityQuery.value(0).toString(), Qt::ISODateWithMs);
        bar.equity = equityQuery.value(1).toDouble();
        bar.cash = equityQuery.value(2).toDouble();
        bar.positions =
            positionsFromJson(equityQuery.value(3).toString());
        record.result.equityCurve.append(bar);
    }
    if (record.result.equityCurve.isEmpty()) {
        emit writeFailed(QStringLiteral(
            "组合回测归档 %1 缺少权益曲线，拒绝恢复").arg(resultId));
        return std::nullopt;
    }

    QSqlQuery tradeQuery(m_database);
    tradeQuery.prepare(QStringLiteral(
        "select symbol, name, market, venue, timestamp_utc, side, quantity, price, "
        "commission, amount, reason from portfolio_trades "
        "where result_id = ? order by sequence_no asc"));
    tradeQuery.addBindValue(resultId);
    if (!tradeQuery.exec()) {
        emitSqlError(QStringLiteral("读取组合回测成交"));
        return std::nullopt;
    }
    while (tradeQuery.next()) {
        PortfolioTrade trade;
        trade.instrument = {tradeQuery.value(0).toString(),
                            tradeQuery.value(1).toString(),
                            static_cast<Market>(tradeQuery.value(2).toInt()),
                            tradeQuery.value(3).toString()};
        trade.timestamp = QDateTime::fromString(
            tradeQuery.value(4).toString(), Qt::ISODateWithMs);
        trade.side = tradeQuery.value(5).toString();
        trade.quantity = tradeQuery.value(6).toDouble();
        trade.price = tradeQuery.value(7).toDouble();
        trade.commission = tradeQuery.value(8).toDouble();
        trade.amount = tradeQuery.value(9).toDouble();
        trade.reason = tradeQuery.value(10).toString();
        record.result.trades.append(trade);
    }
    return record;
}

bool SqliteLocalDataStore::materializeRawTickSnapshot(
    const BacktestResult &deferredResult, BacktestResult *materializedResult,
    QString *error)
{
    if (error) error->clear();
    if (!materializedResult) return false;
    const BacktestConfig &config = deferredResult.config;
    if (config.dataMode != BacktestDataMode::RawTicks
        || config.periodSeconds != 0 || config.frequency != QStringLiteral("tick")
        || config.actualBars <= 0 || config.datasetIdentity.isEmpty()) {
        if (error) *error = QStringLiteral("原始 Tick 归档身份不完整");
        return false;
    }
    if (!deferredResult.rawTickSnapshotDeferred) {
        if (deferredResult.rawTicks.size() != config.actualBars) {
            if (error) *error = QStringLiteral("原始 Tick 归档快照不完整");
            return false;
        }
        *materializedResult = deferredResult;
        return true;
    }
    const BacktestDatasetIdentity &identity = config.datasetIdentity;
    if (identity.rawTickTotal() != config.actualBars
        || computeBacktestDatasetVersion(identity) != identity.datasetVersion
        || !isSha256(config.dataVersion)) {
        if (error) *error = QStringLiteral("原始 Tick 回测结果与数据集身份不一致");
        return false;
    }

    const auto matches = [](const RawTickDayFingerprint &expected,
                            const RawTradeFingerprint &current) {
        if (current.count != expected.rawCount) return false;
        return !expected.rawContentSha256.isEmpty()
            ? current.contentSha256 == expected.rawContentSha256
            : current.maximumId == expected.rawMaximumId;
    };
    QVector<RawTickDayFingerprint> days = identity.days;
    std::sort(days.begin(), days.end(),
              [](const RawTickDayFingerprint &left,
                 const RawTickDayFingerprint &right) {
        return left.tradingDate < right.tradingDate;
    });

    BacktestResult snapshot = deferredResult;
    snapshot.rawTicks.clear();
    snapshot.rawTicks.reserve(config.actualBars);
    RawTickBacktestDataVersionBuilder dataVersionBuilder(
        config.instrument.symbol);
    bool dataVersionInputValid = true;
    for (const RawTickDayFingerprint &day : days) {
        const RawTradeFingerprint before = rawTradeFingerprint(
            config.instrument, day.tradingDate);
        if (!matches(day, before)) {
            if (error) {
                *error = QStringLiteral("归档拒绝：原始 Tick 身份已变化：%1")
                             .arg(day.tradingDate.toString(Qt::ISODate));
            }
            return false;
        }
        const int startSize = snapshot.rawTicks.size();
        const bool loaded = forEachRawTradeTick(
            config.instrument, day.tradingDate,
            [&snapshot, &dataVersionBuilder,
             &dataVersionInputValid](const RawTradeTick &tick) {
                if (!dataVersionBuilder.append(tick)) {
                    dataVersionInputValid = false;
                    return false;
                }
                snapshot.rawTicks.append(tick);
                return true;
            });
        const RawTradeFingerprint after = rawTradeFingerprint(
            config.instrument, day.tradingDate);
        if (!loaded || !dataVersionInputValid
            || snapshot.rawTicks.size() - startSize != day.rawCount
            || !matches(day, after)) {
            if (error) {
                *error = QStringLiteral(
                    "归档拒绝：原始 Tick 读取不完整或读取期间身份变化：%1")
                             .arg(day.tradingDate.toString(Qt::ISODate));
            }
            return false;
        }
    }
    const QString materializedDataVersion = dataVersionBuilder.finish();
    if (materializedDataVersion != config.dataVersion) {
        if (error) {
            *error = QStringLiteral(
                "归档拒绝：物化的原始 Tick 与执行数据版本不一致");
        }
        return false;
    }
    // 再校验一遍全部逐日身份，防止物化后续日期时较早日期被并发替换。
    for (const RawTickDayFingerprint &day : days) {
        if (!matches(day, rawTradeFingerprint(config.instrument, day.tradingDate))) {
            if (error) {
                *error = QStringLiteral("归档拒绝：物化期间数据集身份发生变化：%1")
                             .arg(day.tradingDate.toString(Qt::ISODate));
            }
            return false;
        }
    }
    if (snapshot.rawTicks.size() != config.actualBars
        || snapshot.rawTicks.isEmpty()
        || snapshot.rawTicks.first().timestamp != config.from
        || snapshot.rawTicks.last().timestamp != config.to) {
        if (error) *error = QStringLiteral("归档拒绝：原始 Tick 快照范围与已完成结果不一致");
        return false;
    }
    snapshot.rawTickSnapshotDeferred = false;
    *materializedResult = std::move(snapshot);
    return true;
}

bool SqliteLocalDataStore::saveBacktestArchive(const BacktestArchiveRecord &record)
{
    return saveBacktestArchiveImpl(record, false, {}, nullptr);
}

bool SqliteLocalDataStore::saveDeferredRawTickBacktestArchive(
    const BacktestArchiveRecord &record,
    const std::function<void(qint64)> &residencyObserver,
    QString *error)
{
    return saveBacktestArchiveImpl(record, true, residencyObserver, error);
}

bool SqliteLocalDataStore::saveBacktestArchiveImpl(
    const BacktestArchiveRecord &record, bool allowDeferredRawSnapshot,
    const std::function<void(qint64)> &residencyObserver,
    QString *error)
{
    if (error) error->clear();
    const BacktestConfig &config = record.result.config;
    const bool rawMode = config.dataMode == BacktestDataMode::RawTicks;
    const bool deferredRawMode = rawMode && record.result.rawTickSnapshotDeferred;
    const bool inputSnapshotComplete = rawMode
        ? (deferredRawMode
               ? allowDeferredRawSnapshot && record.result.rawTicks.isEmpty()
                   && config.datasetIdentity.rawTickTotal() == config.actualBars
                   && config.datasetIdentity.instrument.symbol
                       == config.instrument.symbol
                   && config.datasetIdentity.periodSeconds == 0
                   && computeBacktestDatasetVersion(config.datasetIdentity)
                       == config.datasetIdentity.datasetVersion
               : record.result.rawTicks.size() == config.actualBars
                   && !record.result.rawTickSnapshotDeferred)
            && record.result.candles.isEmpty() && config.periodSeconds == 0
            && config.frequency == QStringLiteral("tick")
        : record.result.candles.size() == config.actualBars
            && record.result.rawTicks.isEmpty() && config.periodSeconds > 0;
    if (record.resultId.trimmed().isEmpty() || config.taskId.trimmed().isEmpty()
        || !record.completedAt.isValid() || config.strategyId.trimmed().isEmpty()
        || config.instrument.symbol.trimmed().isEmpty() || config.frequency.trimmed().isEmpty()
        || config.actualBars <= 0 || record.result.equityCurve.isEmpty()
        || !inputSnapshotComplete
        || !isSha256(config.dataVersion)
        || (!config.datasetIdentity.isEmpty()
            && !isSha256(config.datasetIdentity.datasetVersion))
        || (config.strategyId.startsWith(QStringLiteral("custom_"))
            && (config.strategyVersion < 1 || config.ruleSnapshot.isEmpty()))) {
        const QString message = QStringLiteral("回测结果身份不完整，未写入归档");
        if (error) *error = message;
        emit writeFailed(message);
        return false;
    }
    const BacktestPosition &finalPosition = record.result.finalPosition;
    const bool hasFinalPosition = finalPosition.quantity > 0.0;
    // T+1 可使周期 K 与原始 Tick 回测都在样本末保留实际持仓；
    // 持仓字段完整性不依赖输入数据模式，不能为归档而强平或清空。
    if (!std::isfinite(finalPosition.quantity) || finalPosition.quantity < 0.0
        || !std::isfinite(finalPosition.averagePrice)
        || !std::isfinite(finalPosition.entryCommission)
        || finalPosition.entryCommission < 0.0
        || (hasFinalPosition
            && (finalPosition.averagePrice <= 0.0
                 || !finalPosition.entryTime.isValid()
                 || finalPosition.entryTime > config.to))) {
        const QString message = QStringLiteral("回测最终未平仓持仓无效，未写入归档");
        if (error) *error = message;
        emit writeFailed(message);
        return false;
    }

    QHash<QString, QVector<int>> rawIndexesByTimestamp;
    if (rawMode && !deferredRawMode) {
        QDateTime previousTime;
        qint64 previousSourceLine = -1;
        for (int index = 0; index < record.result.rawTicks.size(); ++index) {
            const RawTradeTick &tick = record.result.rawTicks.at(index);
            const bool ordered = previousTime.isNull() || tick.timestamp > previousTime
                || (tick.timestamp == previousTime && tick.sourceLine >= previousSourceLine);
            if (tick.instrument.symbol != config.instrument.symbol
                || !tick.timestamp.isValid() || !tick.tradingDate.isValid()
                || !std::isfinite(tick.price) || tick.price <= 0.0
                || !std::isfinite(tick.volumeShares) || tick.volumeShares < 0.0
                || !std::isfinite(tick.amount) || tick.amount < 0.0 || !ordered) {
                emit writeFailed(QStringLiteral("回测原始 Tick 快照无效，未写入归档"));
                return false;
            }
            previousTime = tick.timestamp;
            previousSourceLine = tick.sourceLine;
            rawIndexesByTimestamp[tick.timestamp.toUTC().toString(Qt::ISODateWithMs)]
                .append(index);
        }
    }
    QHash<QString, int> candleIndexByTimestamp;
    QHash<QString, QVector<int>> deferredSignalIndexesByTimestamp;
    QVector<int> signalIndexByCandle(record.result.candles.size(), -1);
    QVector<int> signalIndexByRawTick(record.result.rawTicks.size(), -1);
    QDateTime previousCandleTime;
    for (int index = 0; index < record.result.candles.size(); ++index) {
        const Candle &candle = record.result.candles.at(index);
        const QString timestamp = candle.startTime.toUTC().toString(Qt::ISODateWithMs);
        const bool pricesValid = std::isfinite(candle.open) && candle.open > 0.0
            && std::isfinite(candle.high) && candle.high > 0.0
            && std::isfinite(candle.low) && candle.low > 0.0
            && std::isfinite(candle.close) && candle.close > 0.0
            && candle.high >= std::max(candle.open, candle.close)
            && candle.low <= std::min(candle.open, candle.close)
            && candle.high >= candle.low;
        const bool volumesValid = std::isfinite(candle.volume) && candle.volume >= 0.0
            && std::isfinite(candle.buyVolume) && candle.buyVolume >= 0.0
            && std::isfinite(candle.sellVolume) && candle.sellVolume >= 0.0
            && std::isfinite(candle.neutralVolume) && candle.neutralVolume >= 0.0;
        if (candle.instrument.symbol != config.instrument.symbol
            || candle.periodSeconds != config.periodSeconds || !candle.startTime.isValid()
            || (!previousCandleTime.isNull() && candle.startTime <= previousCandleTime)
            || candleIndexByTimestamp.contains(timestamp) || !pricesValid || !volumesValid) {
            emit writeFailed(QStringLiteral("回测 K 线快照无效，未写入归档"));
            return false;
        }
        candleIndexByTimestamp.insert(timestamp, index);
        previousCandleTime = candle.startTime;
    }
    for (int index = 0; index < record.result.chartSignals.size(); ++index) {
        const TradeSignal &signal = record.result.chartSignals.at(index);
        const QString timestamp = signal.timestamp.toUTC().toString(Qt::ISODateWithMs);
        int rawIndex = -1;
        if (rawMode && !deferredRawMode) {
            const QVector<int> candidates = rawIndexesByTimestamp.value(timestamp);
            for (const int candidate : candidates) {
                if (signalIndexByRawTick.at(candidate) < 0) {
                    rawIndex = candidate;
                    break;
                }
            }
        }
        const auto candleIndex = candleIndexByTimestamp.constFind(timestamp);
        if (signal.instrument.symbol != config.instrument.symbol || !signal.timestamp.isValid()
            || !std::isfinite(signal.price) || signal.price <= 0.0
            || !std::isfinite(signal.volume) || signal.volume < 0.0
            || (rawMode ? (!deferredRawMode && rawIndex < 0)
                        : candleIndex == candleIndexByTimestamp.constEnd()
                            || signalIndexByCandle.at(*candleIndex) >= 0)) {
            const QString message = QStringLiteral("回测图表信号快照无效，未写入归档");
            if (error) *error = message;
            emit writeFailed(message);
            return false;
        }
        if (deferredRawMode) {
            deferredSignalIndexesByTimestamp[timestamp].append(index);
        } else if (rawMode) signalIndexByRawTick[rawIndex] = index;
        else signalIndexByCandle[*candleIndex] = index;
    }
    if (!m_database.transaction()) {
        if (error) *error = QStringLiteral("开始回测结果归档事务失败：%1")
                                .arg(m_database.lastError().text());
        emitSqlError(QStringLiteral("开始回测结果归档事务"));
        return false;
    }
    auto fail = [this, error](const QString &operation, const QSqlQuery &query) {
        m_database.rollback();
        const QString message = QStringLiteral("%1失败：%2")
                                    .arg(operation, query.lastError().text());
        if (error) *error = message;
        emit writeFailed(message);
        return false;
    };
    auto reject = [this, error](const QString &message) {
        m_database.rollback();
        if (error) *error = message;
        emit writeFailed(message);
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
        "dataset_calendar_coverage_known, candle_series_version, data_mode, "
        "raw_tick_series_version, final_position_quantity, "
        "final_position_average_price, final_position_entry_commission, "
        "final_position_entry_time_utc, final_position_entry_reason, sample_from_utc, sample_to_utc) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
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
        dataset.calendarCoverageKnown ? 1 : 0, rawMode ? 0 : 1,
        backtestDataModeCode(config.dataMode), rawMode ? 1 : 0,
        hasFinalPosition ? finalPosition.quantity : 0.0,
        hasFinalPosition ? finalPosition.averagePrice : 0.0,
        hasFinalPosition ? finalPosition.entryCommission : 0.0,
        hasFinalPosition
            ? finalPosition.entryTime.toUTC().toString(Qt::ISODateWithMs)
            : QStringLiteral(""),
        hasFinalPosition ? finalPosition.entryReason : QStringLiteral(""),
        config.sampleWindowKnown
            ? QVariant(config.sampleFrom.isValid()
                ? config.sampleFrom.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral(""))
            : QVariant{},
        config.sampleWindowKnown
            ? QVariant(config.sampleTo.isValid()
                ? config.sampleTo.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral(""))
            : QVariant{}
    };
    for (const QVariant &value : values) resultQuery.addBindValue(value);
    if (!resultQuery.exec()) return fail(QStringLiteral("写入回测结果身份"), resultQuery);

    QSqlQuery candleQuery(m_database);
    candleQuery.prepare(QStringLiteral(
        "insert into backtest_candles(result_id, sequence_no, start_time_utc, period_seconds, "
        "open, high, low, close, volume, buy_volume, sell_volume, neutral_volume, "
        "directional_volume_known, source, adjustment, downloaded_at_utc, has_signal, "
        "signal_side, signal_price, signal_volume, signal_reason) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (int index = 0; index < record.result.candles.size(); ++index) {
        const Candle &candle = record.result.candles.at(index);
        const int signalIndex = signalIndexByCandle.at(index);
        const bool hasSignal = signalIndex >= 0;
        const TradeSignal signal = hasSignal
            ? record.result.chartSignals.at(signalIndex) : TradeSignal{};
        const QVariantList candleValues {
            record.resultId, index,
            candle.startTime.toUTC().toString(Qt::ISODateWithMs), candle.periodSeconds,
            candle.open, candle.high, candle.low, candle.close, candle.volume,
            candle.buyVolume, candle.sellVolume, candle.neutralVolume,
            candle.directionalVolumeKnown ? 1 : 0, candle.source, candle.adjustment,
            candle.downloadedAt.isValid()
                ? candle.downloadedAt.toUTC().toString(Qt::ISODateWithMs)
                : QStringLiteral(""),
            hasSignal ? 1 : 0,
            hasSignal ? QVariant::fromValue(static_cast<int>(signal.side)) : QVariant{},
            hasSignal ? signal.price : 0.0, hasSignal ? signal.volume : 0.0,
            hasSignal ? signal.reason : QStringLiteral("")
        };
        for (int column = 0; column < candleValues.size(); ++column)
            candleQuery.bindValue(column, candleValues.at(column));
        if (!candleQuery.exec())
            return fail(QStringLiteral("写入回测 K 线与信号快照"), candleQuery);
    }

    QSqlQuery rawTickQuery(m_database);
    rawTickQuery.prepare(QStringLiteral(
        "insert into backtest_raw_ticks(result_id, sequence_no, timestamp_utc, trading_date, "
        "source_line, price, volume_shares, amount, direction, source, has_signal, "
        "signal_side, signal_price, signal_volume, signal_reason) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    const auto insertRawTick = [&](const RawTradeTick &tick, int index,
                                   int signalIndex) {
        const bool hasSignal = signalIndex >= 0;
        const TradeSignal signal = hasSignal
            ? record.result.chartSignals.at(signalIndex) : TradeSignal{};
        const QVariantList tickValues {
            record.resultId, index,
            tick.timestamp.toUTC().toString(Qt::ISODateWithMs),
            tick.tradingDate.toString(Qt::ISODate), tick.sourceLine,
            tick.price, tick.volumeShares, tick.amount,
            static_cast<int>(tick.direction), tick.source,
            hasSignal ? 1 : 0,
            hasSignal ? QVariant::fromValue(static_cast<int>(signal.side)) : QVariant{},
            hasSignal ? signal.price : 0.0,
            hasSignal ? signal.volume : 0.0,
            hasSignal ? signal.reason : QStringLiteral("")
        };
        for (int column = 0; column < tickValues.size(); ++column) {
            rawTickQuery.bindValue(column, tickValues.at(column));
        }
        return rawTickQuery.exec();
    };
    if (!deferredRawMode) {
        for (int index = 0; index < record.result.rawTicks.size(); ++index) {
            if (!insertRawTick(record.result.rawTicks.at(index), index,
                               signalIndexByRawTick.at(index))) {
                return fail(QStringLiteral("写入回测原始 Tick 与信号快照"),
                            rawTickQuery);
            }
        }
    } else {
        const BacktestDatasetIdentity &identity = config.datasetIdentity;
        QVector<RawTickDayFingerprint> days = identity.days;
        std::sort(days.begin(), days.end(),
                  [](const RawTickDayFingerprint &left,
                     const RawTickDayFingerprint &right) {
            return left.tradingDate < right.tradingDate;
        });
        const auto identityMatches = [](const RawTickDayFingerprint &expected,
                                        const RawTradeFingerprint &current) {
            if (current.count != expected.rawCount) return false;
            return !expected.rawContentSha256.isEmpty()
                ? current.contentSha256 == expected.rawContentSha256
                : current.maximumId == expected.rawMaximumId;
        };
        QHash<QString, int> consumedSignalsByTimestamp;
        RawTickBacktestDataVersionBuilder dataVersionBuilder(
            config.instrument.symbol);
        QDateTime firstTimestamp;
        QDateTime lastTimestamp;
        QDateTime previousTimestamp;
        qint64 previousSourceLine = -1;
        int writtenTicks = 0;
        int matchedSignals = 0;
        // 父记录、逐条原始事实、信号、权益与成交共用当前事务。事务中的首次
        // 写入先锁定一致视图，逐日读取只保留当前 RawTradeTick；任何身份、
        // 条数、范围、执行哈希或子表写入异常都会回滚父记录。
        for (const RawTickDayFingerprint &day : days) {
            const RawTradeFingerprint before = rawTradeFingerprint(
                config.instrument, day.tradingDate);
            if (!identityMatches(day, before)) {
                return reject(QStringLiteral(
                    "归档拒绝：原始 Tick 身份已变化：%1")
                                  .arg(day.tradingDate.toString(Qt::ISODate)));
            }
            const int dayStartCount = writtenTicks;
            bool residencyReported = false;
            QString streamError;
            const bool loaded = forEachRawTradeTick(
                config.instrument, day.tradingDate,
                [&](const RawTradeTick &tick) {
                    if (!residencyReported && residencyObserver) {
                        residencyReported = true;
                        residencyObserver(1);
                    }
                    const bool ordered = !previousTimestamp.isValid()
                        || tick.timestamp > previousTimestamp
                        || (tick.timestamp == previousTimestamp
                            && tick.sourceLine >= previousSourceLine);
                    if (tick.instrument.symbol != config.instrument.symbol
                        || tick.tradingDate != day.tradingDate
                        || !tick.timestamp.isValid()
                        || !std::isfinite(tick.price) || tick.price <= 0.0
                        || !std::isfinite(tick.volumeShares)
                        || tick.volumeShares < 0.0
                        || !std::isfinite(tick.amount) || tick.amount < 0.0
                        || !ordered || !dataVersionBuilder.append(tick)) {
                        streamError = QStringLiteral(
                            "归档拒绝：原始 Tick 流无效：%1")
                                          .arg(day.tradingDate.toString(Qt::ISODate));
                        return false;
                    }
                    const QString timestamp = tick.timestamp.toUTC().toString(
                        Qt::ISODateWithMs);
                    int signalIndex = -1;
                    const QVector<int> signalIndexes =
                        deferredSignalIndexesByTimestamp.value(timestamp);
                    const int consumed = consumedSignalsByTimestamp.value(timestamp);
                    if (consumed < signalIndexes.size()) {
                        signalIndex = signalIndexes.at(consumed);
                        consumedSignalsByTimestamp.insert(timestamp, consumed + 1);
                        ++matchedSignals;
                    }
                    if (!insertRawTick(tick, writtenTicks, signalIndex)) {
                        streamError = QStringLiteral(
                            "写入回测原始 Tick 与信号快照失败：%1")
                                          .arg(rawTickQuery.lastError().text());
                        return false;
                    }
                    if (!firstTimestamp.isValid()) firstTimestamp = tick.timestamp;
                    lastTimestamp = tick.timestamp;
                    previousTimestamp = tick.timestamp;
                    previousSourceLine = tick.sourceLine;
                    ++writtenTicks;
                    return true;
                });
            if (residencyReported && residencyObserver) residencyObserver(0);
            const RawTradeFingerprint after = rawTradeFingerprint(
                config.instrument, day.tradingDate);
            if (!loaded || !streamError.isEmpty()
                || writtenTicks - dayStartCount != day.rawCount
                || !identityMatches(day, after)) {
                if (streamError.isEmpty()) {
                    streamError = QStringLiteral(
                        "归档拒绝：原始 Tick 读取不完整或读取期间身份变化：%1")
                                      .arg(day.tradingDate.toString(Qt::ISODate));
                }
                return reject(streamError);
            }
        }
        const QString streamedDataVersion = dataVersionBuilder.finish();
        if (writtenTicks != config.actualBars
            || matchedSignals != record.result.chartSignals.size()
            || firstTimestamp != config.from || lastTimestamp != config.to
            || streamedDataVersion != config.dataVersion) {
            return reject(QStringLiteral(
                "归档拒绝：流式原始 Tick 快照与已完成结果身份不一致"));
        }
        for (const RawTickDayFingerprint &day : days) {
            if (!identityMatches(
                    day, rawTradeFingerprint(config.instrument, day.tradingDate))) {
                return reject(QStringLiteral(
                    "归档拒绝：流式写入期间数据集身份发生变化：%1")
                                  .arg(day.tradingDate.toString(Qt::ISODate)));
            }
        }
    }

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
            "from backtest_results order by completed_at_utc desc, rowid desc"))) {
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
    const QString &resultId, BacktestArchiveLoadMode mode)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select task_id, completed_at_utc, strategy_id, strategy_version, strategy_parameters_json, "
        "rule_snapshot_json, data_version, symbol, name, market, venue, frequency, period_seconds, "
        "requested_bars, actual_bars, from_utc, to_utc, initial_capital, commission_rate, "
        "slippage_rate, lot_size, summary_initial_capital, final_equity, total_return_percent, "
        "max_drawdown_percent, win_rate_percent, sharpe_ratio, total_commission, total_trades, "
        "winning_trades, losing_trades, dataset_identity_json, candle_series_version, "
        "data_mode, raw_tick_series_version, final_position_quantity, "
        "final_position_average_price, final_position_entry_commission, "
        "final_position_entry_time_utc, final_position_entry_reason, sample_from_utc, sample_to_utc "
        "from backtest_results "
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
    const QString sampleFromText = query.value(40).toString();
    const QString sampleToText = query.value(41).toString();
    config.sampleFrom = QDateTime::fromString(sampleFromText, Qt::ISODateWithMs);
    config.sampleTo = QDateTime::fromString(sampleToText, Qt::ISODateWithMs);
    config.sampleWindowKnown = !query.value(40).isNull() && !query.value(41).isNull();
    if ((!sampleFromText.isEmpty() && !config.sampleFrom.isValid())
        || (!sampleToText.isEmpty() && !config.sampleTo.isValid())
        || (config.sampleFrom.isValid() && config.sampleTo.isValid()
            && config.sampleFrom > config.sampleTo)) {
        emit writeFailed(QStringLiteral("回测归档 %1 的请求样本窗无效，拒绝恢复")
                             .arg(resultId));
        return std::nullopt;
    }
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
    const int candleSeriesVersion = query.value(32).toInt();
    config.dataMode = query.value(33).toString() == QStringLiteral("raw_ticks")
        ? BacktestDataMode::RawTicks : BacktestDataMode::Candles;
    const int rawTickSeriesVersion = query.value(34).toInt();
    BacktestPosition &finalPosition = record.result.finalPosition;
    finalPosition.quantity = query.value(35).toDouble();
    finalPosition.averagePrice = query.value(36).toDouble();
    finalPosition.entryCommission = query.value(37).toDouble();
    finalPosition.entryTime = QDateTime::fromString(
        query.value(38).toString(), Qt::ISODateWithMs);
    finalPosition.entryReason = query.value(39).toString();
    if (!std::isfinite(finalPosition.quantity) || finalPosition.quantity < 0.0
        || !std::isfinite(finalPosition.averagePrice)
        || !std::isfinite(finalPosition.entryCommission)
        || finalPosition.entryCommission < 0.0
        || (finalPosition.quantity > 0.0
            && (finalPosition.averagePrice <= 0.0
                || !finalPosition.entryTime.isValid()
                || finalPosition.entryTime > config.to))) {
        emit writeFailed(QStringLiteral("回测归档 %1 的最终未平仓持仓无效，拒绝恢复")
                             .arg(resultId));
        return std::nullopt;
    }

    QSqlQuery candleQuery(m_database);
    candleQuery.prepare(QStringLiteral(
        "select start_time_utc, period_seconds, open, high, low, close, volume, "
        "buy_volume, sell_volume, neutral_volume, directional_volume_known, source, adjustment, "
        "downloaded_at_utc, has_signal, signal_side, signal_price, signal_volume, signal_reason "
        "from backtest_candles where result_id = ? order by sequence_no asc"));
    candleQuery.addBindValue(resultId);
    if (!candleQuery.exec()) {
        emitSqlError(QStringLiteral("读取回测 K 线与信号快照"));
        return std::nullopt;
    }
    while (candleQuery.next()) {
        Candle candle;
        candle.instrument = config.instrument;
        candle.startTime = QDateTime::fromString(candleQuery.value(0).toString(),
                                                 Qt::ISODateWithMs);
        candle.periodSeconds = candleQuery.value(1).toInt();
        candle.open = candleQuery.value(2).toDouble();
        candle.high = candleQuery.value(3).toDouble();
        candle.low = candleQuery.value(4).toDouble();
        candle.close = candleQuery.value(5).toDouble();
        candle.volume = candleQuery.value(6).toDouble();
        candle.buyVolume = candleQuery.value(7).toDouble();
        candle.sellVolume = candleQuery.value(8).toDouble();
        candle.neutralVolume = candleQuery.value(9).toDouble();
        candle.directionalVolumeKnown = candleQuery.value(10).toBool();
        candle.source = candleQuery.value(11).toString();
        candle.adjustment = candleQuery.value(12).toString();
        candle.downloadedAt = QDateTime::fromString(candleQuery.value(13).toString(),
                                                    Qt::ISODateWithMs);
        record.result.candles.append(candle);
        if (candleQuery.value(14).toBool()) {
            TradeSignal signal;
            signal.instrument = config.instrument;
            signal.timestamp = candle.startTime;
            signal.side = static_cast<SignalSide>(candleQuery.value(15).toInt());
            signal.price = candleQuery.value(16).toDouble();
            signal.volume = candleQuery.value(17).toDouble();
            signal.reason = candleQuery.value(18).toString();
            record.result.chartSignals.append(signal);
        }
    }
    if (candleSeriesVersion > 0
        && record.result.candles.size() != config.actualBars) {
        emit writeFailed(QStringLiteral("回测归档 %1 的 K 线快照不完整，拒绝恢复")
                             .arg(resultId));
        return std::nullopt;
    }

    if (rawTickSeriesVersion > 0) {
        QSqlQuery rawTickMetadataQuery(m_database);
        rawTickMetadataQuery.prepare(QStringLiteral(
            "select count(*), min(sequence_no), max(sequence_no), "
            "sum(case when has_signal <> 0 then 1 else 0 end) "
            "from backtest_raw_ticks where result_id = ?"));
        rawTickMetadataQuery.addBindValue(resultId);
        if (!rawTickMetadataQuery.exec() || !rawTickMetadataQuery.next()) {
            emitSqlError(QStringLiteral("读取回测原始 Tick 快照元数据"));
            return std::nullopt;
        }
        const int storedTickCount = rawTickMetadataQuery.value(0).toInt();
        const int firstSequence = rawTickMetadataQuery.value(1).toInt();
        const int lastSequence = rawTickMetadataQuery.value(2).toInt();
        const int storedSignalCount = rawTickMetadataQuery.value(3).toInt();
        if (storedTickCount != config.actualBars || firstSequence != 0
            || lastSequence != config.actualBars - 1) {
            emit writeFailed(QStringLiteral(
                "回测归档 %1 的原始 Tick 快照不完整，拒绝恢复")
                                 .arg(resultId));
            return std::nullopt;
        }
        record.result.rawTickSnapshotAvailable = true;

        if (mode == BacktestArchiveLoadMode::CompleteInputSnapshot) {
            QSqlQuery rawTickQuery(m_database);
            rawTickQuery.setForwardOnly(true);
            rawTickQuery.prepare(QStringLiteral(
                "select timestamp_utc, trading_date, source_line, price, volume_shares, amount, "
                "direction, source from backtest_raw_ticks where result_id = ? "
                "order by sequence_no asc"));
            rawTickQuery.addBindValue(resultId);
            if (!rawTickQuery.exec()) {
                emitSqlError(QStringLiteral("读取完整回测原始 Tick 快照"));
                return std::nullopt;
            }
            record.result.rawTicks.reserve(storedTickCount);
            while (rawTickQuery.next()) {
                RawTradeTick tick;
                tick.instrument = config.instrument;
                tick.timestamp = QDateTime::fromString(
                    rawTickQuery.value(0).toString(), Qt::ISODateWithMs);
                tick.tradingDate = QDate::fromString(
                    rawTickQuery.value(1).toString(), Qt::ISODate);
                tick.sourceLine = rawTickQuery.value(2).toLongLong();
                tick.price = rawTickQuery.value(3).toDouble();
                tick.volumeShares = rawTickQuery.value(4).toDouble();
                tick.amount = rawTickQuery.value(5).toDouble();
                tick.direction = static_cast<RawTradeDirection>(
                    rawTickQuery.value(6).toInt());
                tick.source = rawTickQuery.value(7).toString();
                record.result.rawTicks.append(tick);
            }
            if (record.result.rawTicks.size() != storedTickCount) {
                emit writeFailed(QStringLiteral(
                    "回测归档 %1 的原始 Tick 快照读取不完整，拒绝恢复")
                                     .arg(resultId));
                return std::nullopt;
            }
            record.result.rawTickSnapshotLoaded = true;
        }

        QSqlQuery rawSignalQuery(m_database);
        rawSignalQuery.prepare(QStringLiteral(
            "select timestamp_utc, signal_side, signal_price, signal_volume, signal_reason "
            "from backtest_raw_ticks where result_id = ? and has_signal <> 0 "
            "order by sequence_no asc"));
        rawSignalQuery.addBindValue(resultId);
        if (!rawSignalQuery.exec()) {
            emitSqlError(QStringLiteral("读取回测原始 Tick 信号快照"));
            return std::nullopt;
        }
        while (rawSignalQuery.next()) {
            TradeSignal signal;
            signal.instrument = config.instrument;
            signal.timestamp = QDateTime::fromString(
                rawSignalQuery.value(0).toString(), Qt::ISODateWithMs);
            signal.side = static_cast<SignalSide>(rawSignalQuery.value(1).toInt());
            signal.price = rawSignalQuery.value(2).toDouble();
            signal.volume = rawSignalQuery.value(3).toDouble();
            signal.reason = rawSignalQuery.value(4).toString();
            record.result.chartSignals.append(signal);
        }
        if (record.result.chartSignals.size() != storedSignalCount) {
            emit writeFailed(QStringLiteral(
                "回测归档 %1 的原始 Tick 信号快照读取不完整，拒绝恢复")
                                 .arg(resultId));
            return std::nullopt;
        }
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
    record.result.equityObservationCount = config.actualBars;
    record.result.equityCurveSampled = config.dataMode == BacktestDataMode::RawTicks
        && record.result.equityCurve.size() < record.result.equityObservationCount;
    return record;
}

std::optional<BacktestRawTickPage>
SqliteLocalDataStore::loadBacktestRawTickPage(
    const QString &resultId, int offset, int pageSize)
{
    constexpr int kMaximumArchiveRawTickPageSize = 4096;
    if (resultId.trimmed().isEmpty() || offset < 0 || pageSize <= 0
        || pageSize > kMaximumArchiveRawTickPageSize) {
        emit writeFailed(QStringLiteral(
            "回测归档原始 Tick 分页参数无效（每页最多 %1 条）")
                             .arg(kMaximumArchiveRawTickPageSize));
        return std::nullopt;
    }

    QSqlQuery identityQuery(m_database);
    identityQuery.prepare(QStringLiteral(
        "select symbol, name, market, venue, actual_bars, data_mode, "
        "raw_tick_series_version from backtest_results where result_id = ?"));
    identityQuery.addBindValue(resultId);
    if (!identityQuery.exec()) {
        emitSqlError(QStringLiteral("读取分页回测原始 Tick 身份"));
        return std::nullopt;
    }
    if (!identityQuery.next()
        || identityQuery.value(5).toString() != QStringLiteral("raw_ticks")
        || identityQuery.value(6).toInt() <= 0) {
        return std::nullopt;
    }

    BacktestRawTickPage page;
    page.resultId = resultId;
    page.instrument = {identityQuery.value(0).toString(),
                       identityQuery.value(1).toString(),
                       static_cast<Market>(identityQuery.value(2).toInt()),
                       identityQuery.value(3).toString()};
    const int expectedTotal = identityQuery.value(4).toInt();
    if (expectedTotal <= 0) {
        emit writeFailed(QStringLiteral("回测归档 %1 的原始 Tick 身份无效")
                             .arg(resultId));
        return std::nullopt;
    }
    // 轻量结果恢复已对整段 count/min/max 做一次完整性校验。分页读取只按父记录
    // 的不可变 actualBars 验证当前窗口，避免每翻一页都重新扫描全部子表。
    page.totalCount = expectedTotal;
    page.offset = offset;

    QSqlQuery pageQuery(m_database);
    pageQuery.setForwardOnly(true);
    pageQuery.prepare(QStringLiteral(
        "select timestamp_utc, trading_date, source_line, price, volume_shares, amount, "
        "direction, source from backtest_raw_ticks where result_id = ? "
        "order by sequence_no asc limit ? offset ?"));
    pageQuery.addBindValue(resultId);
    pageQuery.addBindValue(pageSize);
    pageQuery.addBindValue(offset);
    if (!pageQuery.exec()) {
        emitSqlError(QStringLiteral("分页读取回测原始 Tick 快照"));
        return std::nullopt;
    }
    page.ticks.reserve(std::min(pageSize, std::max(0, page.totalCount - offset)));
    while (pageQuery.next()) {
        RawTradeTick tick;
        tick.instrument = page.instrument;
        tick.timestamp = QDateTime::fromString(
            pageQuery.value(0).toString(), Qt::ISODateWithMs);
        tick.tradingDate = QDate::fromString(
            pageQuery.value(1).toString(), Qt::ISODate);
        tick.sourceLine = pageQuery.value(2).toLongLong();
        tick.price = pageQuery.value(3).toDouble();
        tick.volumeShares = pageQuery.value(4).toDouble();
        tick.amount = pageQuery.value(5).toDouble();
        tick.direction = static_cast<RawTradeDirection>(pageQuery.value(6).toInt());
        tick.source = pageQuery.value(7).toString();
        page.ticks.append(tick);
    }
    const int expectedPageCount = std::min(
        pageSize, std::max(0, page.totalCount - offset));
    if (page.ticks.size() != expectedPageCount) {
        emit writeFailed(QStringLiteral(
            "回测归档 %1 的原始 Tick 分页读取不完整")
                             .arg(resultId));
        return std::nullopt;
    }
    page.hasMore = offset + page.ticks.size() < page.totalCount;
    return page;
}

bool SqliteLocalDataStore::saveValidationRun(const ValidationRun &run)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "insert into backtest_validation_runs(name, strategy_id, strategy_version, "
        "instrument_symbol, period_seconds, sample_from_utc, sample_to_utc, requested_bars, "
        "total_return_percent, max_drawdown_percent, sharpe_ratio, total_trades, completed_at_utc) "
        "values(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(run.name);
    query.addBindValue(run.strategyId);
    query.addBindValue(run.strategyVersion);
    query.addBindValue(run.instrumentSymbol);
    query.addBindValue(run.periodSeconds);
    query.addBindValue(run.sampleFrom.isValid()
                           ? run.sampleFrom.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral(""));
    query.addBindValue(run.sampleTo.isValid()
                           ? run.sampleTo.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral(""));
    query.addBindValue(run.requestedBars);
    query.addBindValue(run.totalReturnPercent);
    query.addBindValue(run.maxDrawdownPercent);
    query.addBindValue(run.sharpeRatio);
    query.addBindValue(run.totalTrades);
    query.addBindValue(run.completedAt.toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        emit writeFailed(QStringLiteral("保存回测验证记录失败：%1")
                             .arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<ValidationRun> SqliteLocalDataStore::loadValidationRuns(int limit)
{
    QVector<ValidationRun> runs;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "select name, strategy_id, strategy_version, instrument_symbol, period_seconds, "
        "sample_from_utc, sample_to_utc, requested_bars, total_return_percent, "
        "max_drawdown_percent, sharpe_ratio, total_trades, completed_at_utc "
        "from backtest_validation_runs order by total_return_percent desc limit ?"));
    query.addBindValue(limit);
    if (!query.exec()) {
        emitSqlError(QStringLiteral("读取回测验证记录"));
        return runs;
    }
    while (query.next()) {
        ValidationRun run;
        run.name = query.value(0).toString();
        run.strategyId = query.value(1).toString();
        run.strategyVersion = query.value(2).toInt();
        run.instrumentSymbol = query.value(3).toString();
        run.periodSeconds = query.value(4).toInt();
        const QString sampleFrom = query.value(5).toString();
        const QString sampleTo = query.value(6).toString();
        run.sampleFrom = sampleFrom.isEmpty()
            ? QDateTime() : QDateTime::fromString(sampleFrom, Qt::ISODateWithMs);
        run.sampleTo = sampleTo.isEmpty()
            ? QDateTime() : QDateTime::fromString(sampleTo, Qt::ISODateWithMs);
        run.requestedBars = query.value(7).toInt();
        run.totalReturnPercent = query.value(8).toDouble();
        run.maxDrawdownPercent = query.value(9).toDouble();
        run.sharpeRatio = query.value(10).toDouble();
        run.totalTrades = query.value(11).toInt();
        run.completedAt = QDateTime::fromString(query.value(12).toString(), Qt::ISODateWithMs);
        runs.append(run);
    }
    return runs;
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
            "create index if not exists idx_raw_trade_content_order "
            "on raw_trade_ticks(symbol, trading_date, timestamp_utc, source, source_line, id)"),
        QStringLiteral(
            "create table if not exists ashare_instrument_day_status("
            "symbol text not null, name text not null default '', venue text not null default '', "
            "trading_date text not null, state integer not null, reason text not null default '', "
            "source text not null, source_version text not null, updated_at_utc text not null, "
            "import_run_id text not null default '', "
            "primary key(symbol, trading_date), "
            "check(state in (1, 2)))"),
        QStringLiteral(
            "create table if not exists ashare_instrument_day_import_runs("
            "run_id text primary key, imported_at_utc text not null, "
            "file_name text not null, source text not null, source_version text not null, "
            "content_sha256 text not null, lines_read integer not null, "
            "record_count integer not null, first_date text not null, last_date text not null, "
            "new_record_count integer not null default 0, "
            "unchanged_record_count integer not null default 0, "
            "corrected_record_count integer not null default 0, "
            "revision_reason text not null default '', "
            "status text not null, error text not null default '', "
            "unique(source, source_version, content_sha256), "
            "check(source in ('SSE', 'SZSE')))"),
        QStringLiteral(
            "create table if not exists ashare_instrument_day_import_items("
            "run_id text not null, sequence integer not null, "
            "symbol text not null, name text not null default '', "
            "venue text not null default '', trading_date text not null, "
            "state integer not null, reason text not null default '', "
            "source text not null, source_version text not null, "
            "change_kind text not null, previous_import_run_id text not null default '', "
            "primary key(run_id, sequence), unique(run_id, symbol, trading_date), "
            "foreign key(run_id) references ashare_instrument_day_import_runs(run_id) on delete restrict, "
            "check(state in (1, 2)), "
            "check(change_kind in ('new', 'unchanged', 'corrected')))"),
        QStringLiteral(
            "create index if not exists idx_ashare_day_status_date "
            "on ashare_instrument_day_status(trading_date, symbol)"),
        QStringLiteral(
            "create table if not exists ashare_industry_datasets("
            "dataset_id text primary key, classification text not null, "
            "source text not null, source_version text not null, "
            "content_sha256 text not null, imported_at_utc text not null, "
            "record_count integer not null, file_name text not null default '', "
            "lines_read integer not null default 0, "
            "revision_reason text not null default '', "
            "previous_dataset_id text not null default '', "
            "unique(classification, source, source_version, content_sha256))"),
        QStringLiteral(
            "create table if not exists ashare_industry_memberships("
            "dataset_id text not null, sequence integer not null, "
            "symbol text not null, name text not null default '', "
            "venue text not null default '', industry_code text not null, "
            "industry_name text not null, effective_from text not null, "
            "effective_to text not null default '', "
            "primary key(dataset_id, sequence), "
            "unique(dataset_id, symbol, effective_from), "
            "foreign key(dataset_id) references ashare_industry_datasets(dataset_id) "
            "on delete restrict)"),
        QStringLiteral(
            "create index if not exists idx_ashare_industry_resolve "
            "on ashare_industry_memberships(dataset_id, symbol, effective_from, effective_to)"),
        QStringLiteral(
            "create table if not exists factor_definition_versions("
            "factor_id text not null, version integer not null, name text not null, "
            "alias text not null, source_strategy_id text not null, "
            "source_strategy_version integer not null, definition_json text not null, "
            "preprocessing_json text not null, content_sha256 text not null, "
            "created_at_utc text not null, primary key(factor_id, version), "
            "unique(factor_id, content_sha256), check(version > 0))"),
        QStringLiteral(
            "create index if not exists idx_factor_definitions_created "
            "on factor_definition_versions(created_at_utc desc, factor_id, version desc)"),
        QStringLiteral(
            "create table if not exists factor_candidate_versions("
            "candidate_id text not null, version integer not null, name text not null, "
            "hypothesis text not null, source_kind text not null, source_reference text not null default '', "
            "factor_definition_json text not null, proposed_at_utc text not null, "
            "frozen_at_utc text, content_sha256 text not null, "
            "primary key(candidate_id, version), unique(candidate_id, content_sha256), "
            "check(version > 0), check(source_kind in ('manual', 'authorized_import')))"),
        QStringLiteral(
            "create index if not exists idx_factor_candidates_proposed "
            "on factor_candidate_versions(proposed_at_utc desc, candidate_id, version desc)"),
        QStringLiteral(
            "create table if not exists factor_candidate_evaluations("
            "evaluation_id text primary key, candidate_id text not null, "
            "candidate_version integer not null, candidate_content_sha256 text not null, "
            "archive_result_id text not null, archive_completed_at_utc text not null, "
            "archive_factor_id text not null, archive_factor_version integer not null, "
            "research_method_version text not null, evaluation_role text not null, "
            "preregistered integer not null, archive_identity_sha256 text not null, "
            "linked_at_utc text not null, "
            "unique(candidate_id, candidate_version, archive_result_id, evaluation_role), "
            "foreign key(candidate_id, candidate_version) references "
            "factor_candidate_versions(candidate_id, version) on delete restrict, "
            "check(evaluation_role in ('prospective_holdout', 'retrospective_reference')), "
            "check(preregistered in (0, 1)))"),
        QStringLiteral(
            "create index if not exists idx_factor_candidate_evaluations_linked "
            "on factor_candidate_evaluations(candidate_id, candidate_version, linked_at_utc desc)"),
        QStringLiteral(
            "create table if not exists external_factor_datasets("
            "dataset_id text primary key, factor_key text not null, "
            "factor_definition_sha256 text not null, source text not null, "
            "source_version text not null, source_reference text not null, "
            "content_sha256 text not null, imported_at_utc text not null, "
            "record_count integer not null, symbol_count integer not null, "
            "timestamp_count integer not null, first_timestamp_utc text not null, "
            "last_timestamp_utc text not null, unique(factor_key, "
            "factor_definition_sha256, source, source_version, content_sha256), "
            "check(source='authorized_csv'), check(record_count > 0), "
            "check(symbol_count > 0), check(timestamp_count > 0))"),
        QStringLiteral(
            "create table if not exists external_factor_observations("
            "dataset_id text not null, timestamp_utc text not null, "
            "symbol text not null, value real not null, "
            "primary key(dataset_id, timestamp_utc, symbol), "
            "foreign key(dataset_id) references external_factor_datasets(dataset_id) "
            "on delete restrict)"),
        QStringLiteral(
            "create index if not exists idx_external_factor_observations_lookup "
            "on external_factor_observations(dataset_id, timestamp_utc, symbol)"),
        QStringLiteral(
            "create table if not exists factor_candidate_dataset_bindings("
            "binding_id text primary key, candidate_id text not null, "
            "candidate_version integer not null, candidate_content_sha256 text not null, "
            "factor_definition_sha256 text not null, dataset_id text not null, "
            "dataset_content_sha256 text not null, bound_at_utc text not null, "
            "unique(candidate_id, candidate_version, dataset_id), "
            "foreign key(candidate_id, candidate_version) references "
            "factor_candidate_versions(candidate_id, version) on delete restrict, "
            "foreign key(dataset_id) references external_factor_datasets(dataset_id) "
            "on delete restrict)"),
        QStringLiteral(
            "create index if not exists idx_factor_candidate_dataset_bindings "
            "on factor_candidate_dataset_bindings(candidate_id, candidate_version, "
            "bound_at_utc desc)"),
        QStringLiteral(
            "create table if not exists external_factor_research_archives("
            "result_id text primary key, completed_at_utc text not null, "
            "candidate_id text not null, candidate_version integer not null, "
            "dataset_id text not null, factor_key text not null, "
            "period_seconds integer not null, evaluated_frame_count integer not null, "
            "mean_ic real not null, mean_rank_ic real not null, "
            "identity_sha256 text not null unique, record_json text not null, "
            "foreign key(candidate_id, candidate_version) references "
            "factor_candidate_versions(candidate_id, version) on delete restrict, "
            "foreign key(dataset_id) references external_factor_datasets(dataset_id) "
            "on delete restrict, check(period_seconds > 0), "
            "check(evaluated_frame_count > 0))"),
        QStringLiteral(
            "create index if not exists idx_external_factor_research_completed "
            "on external_factor_research_archives(completed_at_utc desc, result_id)"),
        QStringLiteral(
            "create table if not exists raw_tick_parameter_sensitivity_results("
            "result_id text primary key, completed_at_utc text not null, "
            "strategy_id text not null, symbol text not null, "
            "training_start_date text not null, training_end_date text not null, "
            "validation_start_date text not null, validation_end_date text not null, "
            "candidate_count integer not null, validation_return_percent real not null, "
            "dataset_version text not null, identity_sha256 text not null unique, "
            "record_json text not null, check(candidate_count > 0))"),
        QStringLiteral(
            "create index if not exists idx_raw_tick_sensitivity_completed "
            "on raw_tick_parameter_sensitivity_results(completed_at_utc desc, result_id)"),
        QStringLiteral(
            "create table if not exists raw_tick_content_fingerprints("
            "symbol text not null, trading_date text not null, raw_count integer not null default 0, "
            "raw_max_id integer not null default 0, content_sha256 text not null default '', "
            "dirty integer not null default 1, computed_at_utc text not null default '', "
            "primary key(symbol, trading_date))"),
        QStringLiteral(
            "create table if not exists raw_tick_day_quality_audits("
            "symbol text not null, trading_date text not null, "
            "raw_content_sha256 text not null, audit_version integer not null, "
            "lines_read integer not null, valid_records integer not null, "
            "skipped_records integer not null, out_of_order_records integer not null, "
            "quality_status text not null, source text not null, audited_at_utc text not null, "
            "primary key(symbol, trading_date, raw_content_sha256, audit_version), "
            "check(valid_records > 0), check(skipped_records >= 0), "
            "check(out_of_order_records >= 0), check(audit_version > 0))"),
        QStringLiteral(
            "create index if not exists idx_raw_tick_day_quality_current "
            "on raw_tick_day_quality_audits(symbol, trading_date, audited_at_utc desc)"),
        QStringLiteral(
            "create trigger if not exists trg_raw_tick_content_insert after insert on raw_trade_ticks "
            "begin insert into raw_tick_content_fingerprints(symbol, trading_date, dirty) "
            "values(new.symbol, new.trading_date, 1) on conflict(symbol, trading_date) "
            "do update set dirty=1; end"),
        QStringLiteral(
            "create trigger if not exists trg_raw_tick_content_delete after delete on raw_trade_ticks "
            "begin insert into raw_tick_content_fingerprints(symbol, trading_date, dirty) "
            "values(old.symbol, old.trading_date, 1) on conflict(symbol, trading_date) "
            "do update set dirty=1; end"),
        QStringLiteral(
            "create trigger if not exists trg_raw_tick_content_update after update on raw_trade_ticks "
            "begin insert into raw_tick_content_fingerprints(symbol, trading_date, dirty) "
            "values(old.symbol, old.trading_date, 1) on conflict(symbol, trading_date) "
            "do update set dirty=1; "
            "insert into raw_tick_content_fingerprints(symbol, trading_date, dirty) "
            "values(new.symbol, new.trading_date, 1) on conflict(symbol, trading_date) "
            "do update set dirty=1; end"),
        QStringLiteral(
            "create table if not exists raw_tick_candle_cache_state("
            "symbol text not null, trading_date text not null, period_seconds integer not null,"
            "raw_count integer not null, raw_max_id integer not null, bar_count integer not null,"
            "raw_content_sha256 text not null default '', generated_at_utc text not null, "
            "cache_version integer not null,"
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
            "dataset_calendar_coverage_known integer not null default 0, "
            "candle_series_version integer not null default 0, "
            "data_mode text not null default 'candles', "
            "raw_tick_series_version integer not null default 0, "
            "final_position_quantity real not null default 0, "
            "final_position_average_price real not null default 0, "
            "final_position_entry_commission real not null default 0, "
            "final_position_entry_time_utc text not null default '', "
            "final_position_entry_reason text not null default '', "
            "sample_from_utc text, sample_to_utc text)"),
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
            "create table if not exists backtest_candles("
            "result_id text not null, sequence_no integer not null, start_time_utc text not null, "
            "period_seconds integer not null, open real not null, high real not null, "
            "low real not null, close real not null, volume real not null, "
            "buy_volume real not null default 0, sell_volume real not null default 0, "
            "neutral_volume real not null default 0, directional_volume_known integer not null default 0, "
            "source text not null default '', adjustment text not null default 'none', "
            "downloaded_at_utc text not null default '', has_signal integer not null default 0, "
            "signal_side integer, signal_price real not null default 0, "
            "signal_volume real not null default 0, signal_reason text not null default '', "
            "primary key(result_id, sequence_no), unique(result_id, start_time_utc), "
            "foreign key(result_id) references backtest_results(result_id) on delete cascade)"),
        QStringLiteral(
            "create table if not exists backtest_raw_ticks("
            "result_id text not null, sequence_no integer not null, timestamp_utc text not null, "
            "trading_date text not null, source_line integer not null, price real not null, "
            "volume_shares real not null, amount real not null, direction integer not null, "
            "source text not null default '', has_signal integer not null default 0, "
            "signal_side integer, signal_price real not null default 0, "
            "signal_volume real not null default 0, signal_reason text not null default '', "
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
            "on tickdata_import_run_items(run_id, status, trading_date)"),
        QStringLiteral(
            "create table if not exists batch_backtest_runs("
            "run_id text primary key, created_at_utc text not null, completed_at_utc text, "
            "strategy_id text not null, strategy_parameters_json text not null default '', "
            "data_mode text not null default 'candles', start_date text not null default '', "
            "end_date text not null default '', frequency text not null, "
            "period_seconds integer not null, bar_count integer not null, "
            "initial_capital real not null, commission_rate real not null, "
            "slippage_rate real not null, lot_size integer not null, "
            "holdout_trading_days integer not null default 0, "
            "walk_forward_fold_count integer not null default 0, "
            "total_symbols integer not null, completed_symbols integer not null default 0, "
            "succeeded_symbols integer not null default 0, "
            "failed_symbols integer not null default 0, "
            "status text not null, error text not null default '')"),
        QStringLiteral(
            "create index if not exists idx_batch_backtest_runs_created "
            "on batch_backtest_runs(created_at_utc desc, run_id desc)"),
        QStringLiteral(
            "create table if not exists batch_backtest_run_items("
            "run_id text not null, symbol text not null, name text not null, "
            "market integer not null, venue text not null, status text not null, "
            "error text not null default '', data_version text not null default '', "
            "dataset_version text not null default '', primary key(run_id, symbol), "
            "foreign key(run_id) references batch_backtest_runs(run_id) on delete cascade)"),
        QStringLiteral(
            "create index if not exists idx_batch_backtest_run_items_status "
            "on batch_backtest_run_items(run_id, status, symbol)"),
        QStringLiteral(
            "create table if not exists portfolio_backtest_results("
            "result_id text primary key, portfolio_id text not null, name text not null, "
            "completed_at_utc text not null, definition_json text not null, "
            "leg_count integer not null, period_seconds integer not null, "
            "bar_count integer not null, dataset_versions_json text not null default '[]', "
            "result_kind text not null default 'fixed_portfolio', "
            "immutable_identity_json text not null default '{}', "
            "dataset_identities_json text not null default '[]', "
            "initial_capital real not null, final_equity real not null, "
            "total_return_percent real not null, max_drawdown_percent real not null, "
            "sharpe_ratio real not null, total_commission real not null, "
            "total_trades integer not null)"),
        QStringLiteral(
            "create index if not exists idx_portfolio_results_completed "
            "on portfolio_backtest_results(completed_at_utc desc, result_id desc)"),
        QStringLiteral(
            "create table if not exists portfolio_equity_points("
            "result_id text not null, sequence_no integer not null, "
            "timestamp_utc text not null, equity real not null, cash real not null, "
            "positions_json text not null default '[]', primary key(result_id, sequence_no), "
            "foreign key(result_id) references portfolio_backtest_results(result_id) "
            "on delete cascade)"),
        QStringLiteral(
            "create table if not exists portfolio_trades("
            "result_id text not null, sequence_no integer not null, "
            "symbol text not null, name text not null, market integer not null, "
            "venue text not null, timestamp_utc text not null, side text not null, "
            "quantity real not null, price real not null, commission real not null, "
            "amount real not null, reason text not null, "
            "primary key(result_id, sequence_no), "
            "foreign key(result_id) references portfolio_backtest_results(result_id) "
            "on delete cascade)"),
        QStringLiteral(
            "create table if not exists backtest_validation_runs("
            "run_id integer primary key autoincrement,"
            "name text not null, strategy_id text not null, strategy_version integer not null,"
            "instrument_symbol text not null, period_seconds integer not null,"
            "sample_from_utc text not null default '', sample_to_utc text not null default '',"
            "requested_bars integer not null default 0,"
            "total_return_percent real not null, max_drawdown_percent real not null,"
            "sharpe_ratio real not null, total_trades integer not null,"
            "completed_at_utc text not null)"),
        QStringLiteral(
            "create index if not exists idx_validation_runs_return "
            "on backtest_validation_runs(total_return_percent desc)")
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
        || !ensureColumn(QStringLiteral("raw_tick_candle_cache_state"),
                         QStringLiteral("raw_content_sha256"),
                         QStringLiteral("text not null default ''"))
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
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("candle_series_version"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("data_mode"),
                         QStringLiteral("text not null default 'candles'"))
        || !ensureColumn(QStringLiteral("backtest_results"),
                         QStringLiteral("raw_tick_series_version"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("backtest_results"),
                         QStringLiteral("final_position_quantity"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("backtest_results"),
                         QStringLiteral("final_position_average_price"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("backtest_results"),
                         QStringLiteral("final_position_entry_commission"),
                         QStringLiteral("real not null default 0"))
        || !ensureColumn(QStringLiteral("backtest_results"),
                         QStringLiteral("final_position_entry_time_utc"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("backtest_results"),
                         QStringLiteral("final_position_entry_reason"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("sample_from_utc"),
                         QStringLiteral("text"))
        || !ensureColumn(QStringLiteral("backtest_results"), QStringLiteral("sample_to_utc"),
                         QStringLiteral("text"))
        || !ensureColumn(QStringLiteral("batch_backtest_runs"),
                         QStringLiteral("data_mode"),
                         QStringLiteral("text not null default 'candles'"))
        || !ensureColumn(QStringLiteral("batch_backtest_runs"),
                         QStringLiteral("start_date"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("batch_backtest_runs"),
                         QStringLiteral("end_date"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("batch_backtest_runs"),
                         QStringLiteral("holdout_trading_days"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("batch_backtest_runs"),
                         QStringLiteral("walk_forward_fold_count"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("portfolio_backtest_results"),
                         QStringLiteral("result_kind"),
                         QStringLiteral("text not null default 'fixed_portfolio'"))
        || !ensureColumn(QStringLiteral("portfolio_backtest_results"),
                         QStringLiteral("immutable_identity_json"),
                         QStringLiteral("text not null default '{}'"))
        || !ensureColumn(QStringLiteral("portfolio_backtest_results"),
                         QStringLiteral("dataset_identities_json"),
                         QStringLiteral("text not null default '[]'"))
        || !ensureColumn(QStringLiteral("ashare_instrument_day_status"),
                         QStringLiteral("import_run_id"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("ashare_instrument_day_import_runs"),
                         QStringLiteral("new_record_count"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("ashare_instrument_day_import_runs"),
                         QStringLiteral("unchanged_record_count"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("ashare_instrument_day_import_runs"),
                         QStringLiteral("corrected_record_count"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("ashare_instrument_day_import_runs"),
                         QStringLiteral("revision_reason"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("ashare_industry_datasets"),
                         QStringLiteral("file_name"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("ashare_industry_datasets"),
                         QStringLiteral("lines_read"),
                         QStringLiteral("integer not null default 0"))
        || !ensureColumn(QStringLiteral("ashare_industry_datasets"),
                         QStringLiteral("revision_reason"),
                         QStringLiteral("text not null default ''"))
        || !ensureColumn(QStringLiteral("ashare_industry_datasets"),
                         QStringLiteral("previous_dataset_id"),
                         QStringLiteral("text not null default ''"))) {
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
        "insert into schema_versions(component, version, updated_at_utc) values('backtest_archive', 3, ?) "
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
