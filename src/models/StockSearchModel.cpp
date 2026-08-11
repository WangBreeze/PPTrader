#include "models/StockSearchModel.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace pp {

static QString findCacheFile()
{
    const QStringList candidates = {
        QStringLiteral("data/astock_cache.json"),
        QStringLiteral("../data/astock_cache.json"),
        QStringLiteral("../../data/astock_cache.json"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../data/astock_cache.json"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../data/astock_cache.json"),
    };

    for (const auto &path : candidates) {
        if (QFileInfo::exists(path)) {
            return QFileInfo(path).absoluteFilePath();
        }
    }

    return {};
}

StockSearchModel::StockSearchModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int StockSearchModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_filtered.size();
}

QVariant StockSearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_filtered.size()) {
        return {};
    }

    const auto &entry = m_filtered.at(index.row());
    switch (role) {
    case CodeRole:
        return entry.code;
    case NameRole:
        return entry.name;
    case MarketRole:
        return entry.market;
    case Qt::DisplayRole:
        return QStringLiteral("%1  %2").arg(entry.code, entry.name);
    default:
        return {};
    }
}

QHash<int, QByteArray> StockSearchModel::roleNames() const
{
    return {
        {CodeRole, "code"},
        {NameRole, "name"},
        {MarketRole, "market"},
    };
}

int StockSearchModel::resultCount() const
{
    return m_filtered.size();
}

void StockSearchModel::loadFromJson(const QString &path)
{
    const QString cachePath = path.isEmpty() ? findCacheFile() : path;
    if (cachePath.isEmpty()) {
        return;
    }

    QFile file(cachePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray()) {
        return;
    }

    m_all.clear();
    m_all.reserve(doc.array().size());

    for (const auto &val : doc.array()) {
        const QJsonObject obj = val.toObject();
        StockEntry entry;
        entry.code = obj.value(QStringLiteral("code")).toString();
        entry.name = obj.value(QStringLiteral("name")).toString();
        entry.market = obj.value(QStringLiteral("market")).toString();
        if (!entry.code.isEmpty()) {
            m_all.append(std::move(entry));
        }
    }
}

void StockSearchModel::setFilterText(const QString &text)
{
    const QString trimmed = text.trimmed();

    beginResetModel();
    if (trimmed.isEmpty()) {
        m_filtered.clear();
    } else {
        m_filtered.clear();
        for (const auto &entry : m_all) {
            if (entry.code.contains(trimmed) || entry.name.contains(trimmed)) {
                m_filtered.append(entry);
                if (m_filtered.size() >= 50) {
                    break;
                }
            }
        }
    }
    endResetModel();

    emit resultCountChanged();
}

QVariantMap StockSearchModel::get(int index) const
{
    QVariantMap map;
    if (index >= 0 && index < m_filtered.size()) {
        const auto &entry = m_filtered.at(index);
        map[QStringLiteral("code")] = entry.code;
        map[QStringLiteral("name")] = entry.name;
        map[QStringLiteral("market")] = entry.market;
    }
    return map;
}

QString StockSearchModel::codeAt(int index) const
{
    if (index >= 0 && index < m_filtered.size()) {
        return m_filtered.at(index).code;
    }
    return {};
}

} // namespace pp
