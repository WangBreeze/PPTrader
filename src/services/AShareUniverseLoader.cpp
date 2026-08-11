#include "services/AShareUniverseLoader.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace pp {

QString AShareUniverseLoader::findDefaultCachePath()
{
    const QStringList candidates {
        QCoreApplication::applicationDirPath() + QStringLiteral("/data/astock_cache.json"),
        QStringLiteral("data/astock_cache.json"),
        QStringLiteral("../data/astock_cache.json"),
        QStringLiteral("../../data/astock_cache.json"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../data/astock_cache.json"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../data/astock_cache.json")
    };
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile()) return info.absoluteFilePath();
    }
    return {};
}

QString AShareUniverseLoader::venueForCode(const QString &code,
                                            const QString &marketLabel)
{
    const QString normalizedCode = code.trimmed();
    if (normalizedCode.size() != 6) return {};
    for (const QChar character : normalizedCode) {
        if (!character.isDigit()) return {};
    }

    const QString prefix = normalizedCode.left(3);
    const bool isShanghai = prefix == QStringLiteral("600")
        || prefix == QStringLiteral("601") || prefix == QStringLiteral("603")
        || prefix == QStringLiteral("605") || prefix == QStringLiteral("688");
    const bool isShenzhen = prefix == QStringLiteral("000")
        || prefix == QStringLiteral("001") || prefix == QStringLiteral("002")
        || prefix == QStringLiteral("003") || prefix == QStringLiteral("300")
        || prefix == QStringLiteral("301");
    const bool isBeijing = normalizedCode.startsWith(QStringLiteral("43"))
        || normalizedCode.startsWith(QStringLiteral("82"))
        || normalizedCode.startsWith(QStringLiteral("83"))
        || normalizedCode.startsWith(QStringLiteral("87"))
        || normalizedCode.startsWith(QStringLiteral("88"))
        || normalizedCode.startsWith(QStringLiteral("92"));

    const QString label = marketLabel.trimmed();
    if (isShanghai && (label.isEmpty() || label.contains(QStringLiteral("上海"))
        || label.contains(QStringLiteral("上交"), Qt::CaseInsensitive)
        || label.compare(QStringLiteral("SSE"), Qt::CaseInsensitive) == 0)) {
        return QStringLiteral("上交所");
    }
    if (isShenzhen && (label.isEmpty() || label.contains(QStringLiteral("深圳"))
        || label.contains(QStringLiteral("深交"), Qt::CaseInsensitive)
        || label.compare(QStringLiteral("SZSE"), Qt::CaseInsensitive) == 0)) {
        return QStringLiteral("深交所");
    }
    if (isBeijing && (label.isEmpty() || label.contains(QStringLiteral("北京"))
        || label.contains(QStringLiteral("北交")))) {
        return QStringLiteral("北交所");
    }
    return {};
}

AShareUniverseLoadResult AShareUniverseLoader::load(const QString &cachePath)
{
    AShareUniverseLoadResult result;
    result.cachePath = cachePath.trimmed().isEmpty() ? findDefaultCachePath()
                                                     : QFileInfo(cachePath).absoluteFilePath();
    if (result.cachePath.isEmpty()) {
        result.error = QStringLiteral("找不到 A 股股票池缓存 data/astock_cache.json");
        return result;
    }

    QFile file(result.cachePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法读取 A 股股票池缓存：%1").arg(file.errorString());
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        result.error = QStringLiteral("A 股股票池缓存格式无效：%1")
            .arg(parseError.errorString());
        return result;
    }

    const QJsonArray rows = document.array();
    result.sourceCount = rows.size();
    result.instruments.reserve(rows.size());
    QSet<QString> seen;
    for (const QJsonValue &value : rows) {
        const QJsonObject object = value.toObject();
        const QString code = object.value(QStringLiteral("code")).toString().trimmed();
        const QString venue = venueForCode(
            code, object.value(QStringLiteral("market")).toString());
        if (venue.isEmpty() || seen.contains(code)) {
            ++result.rejectedCount;
            continue;
        }
        QString name = object.value(QStringLiteral("name")).toString();
        name.remove(QChar::Null);
        name = name.trimmed();
        if (name.isEmpty()) name = code;
        seen.insert(code);
        result.instruments.append({code, name, Market::AShare, venue});
    }

    std::sort(result.instruments.begin(), result.instruments.end(),
              [](const Instrument &left, const Instrument &right) {
                  return left.symbol < right.symbol;
              });
    if (result.instruments.isEmpty()) {
        result.error = QStringLiteral("A 股股票池缓存没有通过校验的股票代码");
    }
    return result;
}

} // namespace pp
