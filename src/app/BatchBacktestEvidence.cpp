#include "app/BatchBacktestEvidence.h"

#include <QAbstractItemModel>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <algorithm>

namespace pp::batchBacktestEvidence {

namespace {

const QList<QByteArray> &stableRoleNames()
{
    static const QList<QByteArray> names{
        "symbol", "name", "dataMode", "startDate", "endDate", "frequency",
        "requestedBars", "actualBars", "succeeded", "error", "dataVersion",
        "datasetVersion", "datasetDayCount", "datasetRawTickCount",
        "datasetGapDayCount", "datasetStaleDayCount", "dataQualityStatus",
        "dataQualityEligible", "dataQualityAssessment", "finalEquity",
        "totalReturnPercent", "maxDrawdownPercent", "winRatePercent",
        "sharpeRatio", "totalCommission", "totalTrades", "hasOpenPosition",
        "openPositionQuantity", "openPositionAveragePrice",
        "openPositionEntryTime"
    };
    return names;
}

QHash<QByteArray, int> rolesByName(const QAbstractItemModel *model)
{
    QHash<QByteArray, int> result;
    if (!model) return result;
    const QHash<int, QByteArray> roles = model->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        result.insert(it.value(), it.key());
    return result;
}

} // namespace

QJsonArray canonicalRows(const QAbstractItemModel *model)
{
    if (!model) return {};
    const QHash<QByteArray, int> roleLookup = rolesByName(model);
    QVector<QVariantMap> rows;
    rows.reserve(model->rowCount());
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        QVariantMap stableRow;
        for (const QByteArray &name : stableRoleNames()) {
            const auto roleIt = roleLookup.constFind(name);
            stableRow.insert(QString::fromLatin1(name),
                             roleIt == roleLookup.cend()
                                 ? QVariant{}
                                 : model->data(index, roleIt.value()));
        }
        rows.append(std::move(stableRow));
    }
    std::sort(rows.begin(), rows.end(), [](const QVariantMap &left,
                                           const QVariantMap &right) {
        const QString leftSymbol = left.value(QStringLiteral("symbol")).toString();
        const QString rightSymbol = right.value(QStringLiteral("symbol")).toString();
        if (leftSymbol != rightSymbol) return leftSymbol < rightSymbol;
        return QJsonDocument(QJsonObject::fromVariantMap(left)).toJson(QJsonDocument::Compact)
            < QJsonDocument(QJsonObject::fromVariantMap(right)).toJson(QJsonDocument::Compact);
    });

    QJsonArray result;
    for (const QVariantMap &row : rows)
        result.append(QJsonObject::fromVariantMap(row));
    return result;
}

QString canonicalDigest(const QAbstractItemModel *model)
{
    const QByteArray bytes = QJsonDocument(canonicalRows(model))
                                 .toJson(QJsonDocument::Compact);
    return QStringLiteral("sha256:")
        + QString::fromLatin1(QCryptographicHash::hash(
                                  bytes, QCryptographicHash::Sha256)
                                  .toHex());
}

qint64 totalRawTickCount(const QAbstractItemModel *model)
{
    if (!model) return 0;
    const QHash<QByteArray, int> roleLookup = rolesByName(model);
    const auto roleIt = roleLookup.constFind("datasetRawTickCount");
    if (roleIt == roleLookup.cend()) return 0;
    qint64 total = 0;
    for (int row = 0; row < model->rowCount(); ++row)
        total += model->data(model->index(row, 0), roleIt.value()).toLongLong();
    return total;
}

} // namespace pp::batchBacktestEvidence
