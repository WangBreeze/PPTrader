#pragma once

#include <QJsonArray>
#include <QString>

class QAbstractItemModel;

namespace pp::batchBacktestEvidence {

QJsonArray canonicalRows(const QAbstractItemModel *model);
QString canonicalDigest(const QAbstractItemModel *model);
qint64 totalRawTickCount(const QAbstractItemModel *model);

} // namespace pp::batchBacktestEvidence
