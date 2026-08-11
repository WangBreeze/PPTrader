#include "services/NasSyncStateStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <utility>

namespace pp {

namespace {

constexpr qint64 MaximumStateBytes = 16 * 1024 * 1024;
constexpr int MaximumStateItems = 12000;

bool validDigest(const QString &digest)
{
    if (digest.size() != 64) return false;
    for (const QChar character : digest) {
        if (!character.isDigit() && (character < QLatin1Char('a')
                                     || character > QLatin1Char('f'))) return false;
    }
    return true;
}

bool validSymbol(const QString &symbol)
{
    if (symbol.size() != 6) return false;
    for (const QChar character : symbol) {
        if (!character.isDigit()) return false;
    }
    return true;
}

bool validEntry(const NasCatalogEntry &entry)
{
    const QString digest = entry.sha256.toLower();
    const QString expectedPath = QStringLiteral("/v1/ticks/%1/%2")
        .arg(entry.symbol, entry.tradingDate.toString(QStringLiteral("yyyyMMdd")));
    return validSymbol(entry.symbol) && entry.tradingDate.isValid()
        && entry.format == QStringLiteral("tick_csv") && entry.size > 0
        && validDigest(digest)
        && entry.version == QStringLiteral("sha256:") + digest
        && entry.downloadPath == expectedPath;
}

QJsonObject itemObject(const NasSyncPlanItem &item)
{
    return {{QStringLiteral("symbol"), item.entry.symbol},
            {QStringLiteral("tradingDate"), item.entry.tradingDate.toString(Qt::ISODate)},
            {QStringLiteral("format"), item.entry.format},
            {QStringLiteral("size"), item.entry.size},
            {QStringLiteral("sha256"), item.entry.sha256},
            {QStringLiteral("version"), item.entry.version},
            {QStringLiteral("downloadPath"), item.entry.downloadPath},
            {QStringLiteral("status"), static_cast<int>(item.status)},
            {QStringLiteral("rawTicksWritten"), item.rawTicksWritten},
            {QStringLiteral("candleBarsWritten"), item.candleBarsWritten},
            {QStringLiteral("fromCache"), item.fromCache},
            {QStringLiteral("transferElapsedMilliseconds"), item.transferElapsedMilliseconds},
            {QStringLiteral("parseElapsedMilliseconds"), item.parseElapsedMilliseconds},
            {QStringLiteral("rawDatabaseElapsedMilliseconds"), item.rawDatabaseElapsedMilliseconds},
            {QStringLiteral("candleDatabaseElapsedMilliseconds"), item.candleDatabaseElapsedMilliseconds},
            {QStringLiteral("importElapsedMilliseconds"), item.importElapsedMilliseconds},
            {QStringLiteral("error"), item.error}};
}

} // namespace

void NasSyncStateStore::setFilePath(const QString &filePath)
{
    m_filePath = QFileInfo(filePath).absoluteFilePath();
}

QString NasSyncStateStore::filePath() const { return m_filePath; }

bool NasSyncStateStore::save(const NasPersistedSyncState &state, QString *error) const
{
    if (error) error->clear();
    if (m_filePath.isEmpty() || state.items.size() > MaximumStateItems) {
        if (error) *error = QStringLiteral("NAS 同步恢复状态路径或条目数无效");
        return false;
    }
    QSet<QString> symbols;
    QJsonArray symbolArray;
    for (const QString &symbol : state.symbols) {
        if (!validSymbol(symbol) || symbols.contains(symbol)) continue;
        symbols.insert(symbol);
        symbolArray.append(symbol);
    }
    QJsonArray itemArray;
    for (const NasSyncPlanItem &item : state.items) {
        if (!validEntry(item.entry)) {
            if (error) *error = QStringLiteral("NAS 同步恢复状态包含无效条目");
            return false;
        }
        itemArray.append(itemObject(item));
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("symbols"), symbolArray);
    root.insert(QStringLiteral("items"), itemArray);
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (payload.size() > MaximumStateBytes) {
        if (error) *error = QStringLiteral("NAS 同步恢复状态超过 16 MiB");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(m_filePath).absolutePath())) {
        if (error) *error = QStringLiteral("无法创建 NAS 同步恢复状态目录");
        return false;
    }
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size()
        || !file.commit()) {
        if (error) *error = QStringLiteral("无法原子保存 NAS 同步恢复状态");
        return false;
    }
    return true;
}

bool NasSyncStateStore::load(NasPersistedSyncState *state, QString *error) const
{
    if (error) error->clear();
    if (!state) return false;
    *state = {};
    QFile file(m_filePath);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaximumStateBytes) {
        if (error) *error = QStringLiteral("NAS 同步恢复状态不可读或超过限制");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("NAS 同步恢复状态 JSON 无效");
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != 1
        || items.size() > MaximumStateItems) {
        if (error) *error = QStringLiteral("NAS 同步恢复状态版本或条目数不受支持");
        return false;
    }
    for (const QJsonValue &value : root.value(QStringLiteral("symbols")).toArray()) {
        const QString symbol = value.toString();
        if (validSymbol(symbol) && !state->symbols.contains(symbol))
            state->symbols.append(symbol);
    }
    for (const QJsonValue &value : items) {
        const QJsonObject object = value.toObject();
        NasSyncPlanItem item;
        item.entry.symbol = object.value(QStringLiteral("symbol")).toString();
        item.entry.tradingDate = QDate::fromString(
            object.value(QStringLiteral("tradingDate")).toString(), Qt::ISODate);
        item.entry.format = object.value(QStringLiteral("format")).toString();
        item.entry.size = object.value(QStringLiteral("size")).toInteger();
        item.entry.sha256 = object.value(QStringLiteral("sha256")).toString().toLower();
        item.entry.version = object.value(QStringLiteral("version")).toString();
        item.entry.downloadPath = object.value(QStringLiteral("downloadPath")).toString();
        const int status = object.value(QStringLiteral("status")).toInt(-1);
        if (!validEntry(item.entry) || status < static_cast<int>(NasSyncItemStatus::Ready)
            || status > static_cast<int>(NasSyncItemStatus::Cancelled)) {
            if (error) *error = QStringLiteral("NAS 同步恢复状态包含无效字段");
            *state = {};
            return false;
        }
        item.status = static_cast<NasSyncItemStatus>(status);
        if (item.status == NasSyncItemStatus::Downloading
            || item.status == NasSyncItemStatus::Importing) {
            item.status = NasSyncItemStatus::Cancelled;
            item.error = QStringLiteral("应用退出时尚未完成，可重试");
        } else {
            item.error = object.value(QStringLiteral("error")).toString();
        }
        item.rawTicksWritten = object.value(QStringLiteral("rawTicksWritten")).toInteger();
        item.candleBarsWritten = object.value(QStringLiteral("candleBarsWritten")).toInt();
        item.fromCache = object.value(QStringLiteral("fromCache")).toBool();
        item.transferElapsedMilliseconds = object.value(QStringLiteral("transferElapsedMilliseconds")).toInteger();
        item.parseElapsedMilliseconds = object.value(QStringLiteral("parseElapsedMilliseconds")).toInteger();
        item.rawDatabaseElapsedMilliseconds = object.value(QStringLiteral("rawDatabaseElapsedMilliseconds")).toInteger();
        item.candleDatabaseElapsedMilliseconds = object.value(QStringLiteral("candleDatabaseElapsedMilliseconds")).toInteger();
        item.importElapsedMilliseconds = object.value(QStringLiteral("importElapsedMilliseconds")).toInteger();
        state->items.append(item);
    }
    if (state->symbols.isEmpty()) {
        for (const NasSyncPlanItem &item : std::as_const(state->items)) {
            if (!state->symbols.contains(item.entry.symbol))
                state->symbols.append(item.entry.symbol);
        }
    }
    return true;
}

bool NasSyncStateStore::clear(QString *error) const
{
    if (error) error->clear();
    if (m_filePath.isEmpty() || !QFile::exists(m_filePath)) return true;
    if (QFile::remove(m_filePath)) return true;
    if (error) *error = QStringLiteral("无法清除 NAS 同步恢复状态");
    return false;
}

} // namespace pp
