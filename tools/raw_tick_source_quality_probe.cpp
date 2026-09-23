#include "services/RawTickSourceQualityAdapter.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

#include <optional>
#include <utility>

namespace {

std::optional<pp::RawTradeDirection> directionFromText(const QString &value)
{
    const QString normalized = value.trimmed().toUpper();
    if (normalized == QStringLiteral("B")) return pp::RawTradeDirection::Buy;
    if (normalized == QStringLiteral("S")) return pp::RawTradeDirection::Sell;
    if (normalized == QStringLiteral("M")) return pp::RawTradeDirection::Neutral;
    return std::nullopt;
}

std::optional<pp::RawTickSourceVolumeUnit> volumeUnitFromText(
    const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("lots"))
        return pp::RawTickSourceVolumeUnit::Lots;
    if (normalized == QStringLiteral("shares"))
        return pp::RawTickSourceVolumeUnit::Shares;
    if (normalized == QStringLiteral("unknown"))
        return pp::RawTickSourceVolumeUnit::Unknown;
    return std::nullopt;
}

bool parseRequests(const QByteArray &json,
                   QVector<pp::RawTickSourceQualityRequest> *requests,
                   QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("请求 JSON 无效：%1").arg(parseError.errorString());
        return false;
    }
    const QJsonArray files = document.object().value(QStringLiteral("files")).toArray();
    if (files.isEmpty()) {
        if (error) *error = QStringLiteral("请求 JSON 的 files 必须是非空数组");
        return false;
    }
    QVector<pp::RawTickSourceQualityRequest> parsed;
    parsed.reserve(files.size());
    for (int index = 0; index < files.size(); ++index) {
        const QJsonObject value = files.at(index).toObject();
        pp::RawTickSourceQualityRequest request;
        request.filePath = value.value(QStringLiteral("path")).toString();
        request.instrument.symbol = value.value(QStringLiteral("symbol")).toString();
        request.instrument.name = value.value(QStringLiteral("name")).toString();
        request.instrument.market = pp::Market::AShare;
        request.instrument.venue = value.value(QStringLiteral("venue")).toString();
        request.tradingDate = QDate::fromString(
            value.value(QStringLiteral("tradingDate")).toString(), Qt::ISODate);
        const auto volumeUnit = volumeUnitFromText(
            value.value(QStringLiteral("volumeUnit")).toString());
        if (!volumeUnit.has_value()) {
            if (error) *error = QStringLiteral(
                "files[%1].volumeUnit 必须为 lots、shares 或 unknown").arg(index);
            return false;
        }
        request.volumeUnit = *volumeUnit;
        request.directionMappingVersion =
            value.value(QStringLiteral("directionMappingVersion")).toString();
        request.maximumDiagnostics = value.value(
            QStringLiteral("maximumDiagnostics")).toInt(200);
        const QJsonObject mappings = value.value(
            QStringLiteral("directionMappings")).toObject();
        for (auto it = mappings.constBegin(); it != mappings.constEnd(); ++it) {
            const auto direction = directionFromText(it.value().toString());
            if (!direction.has_value()) {
                if (error) *error = QStringLiteral(
                    "files[%1].directionMappings.%2 必须映射到 B、S 或 M")
                    .arg(index).arg(it.key());
                return false;
            }
            request.explicitDirectionMappings.insert(it.key(), *direction);
        }
        parsed.append(std::move(request));
    }
    if (requests) *requests = std::move(parsed);
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral(
        "pptrader_raw_tick_source_quality_probe"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "PPTrader 原始 Tick 源文件只读质量预检"));
    parser.addHelpOption();
    QCommandLineOption requestOption(
        {QStringLiteral("r"), QStringLiteral("request")},
        QStringLiteral("显式文件请求 JSON；不会扫描其父目录"),
        QStringLiteral("path"));
    QCommandLineOption outputOption(
        {QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("报告 JSON 输出路径；省略时写标准输出"),
        QStringLiteral("path"));
    parser.addOption(requestOption);
    parser.addOption(outputOption);
    parser.process(application);

    QTextStream errorStream(stderr);
    const QString requestPath = parser.value(requestOption).trimmed();
    if (requestPath.isEmpty()) {
        errorStream << QStringLiteral("缺少 --request 请求文件\n");
        return 1;
    }
    QFile requestFile(requestPath);
    if (!requestFile.open(QIODevice::ReadOnly)) {
        errorStream << QStringLiteral("无法读取请求文件\n");
        return 1;
    }
    QVector<pp::RawTickSourceQualityRequest> requests;
    QString parseError;
    if (!parseRequests(requestFile.readAll(), &requests, &parseError)) {
        errorStream << parseError << Qt::endl;
        return 1;
    }

    const pp::RawTickSourceQualityManifest manifest =
        pp::RawTickSourceQualityAdapter::inspectBatch(requests);
    const QByteArray output = manifest.toJson();
    const QString outputPath = parser.value(outputOption).trimmed();
    if (outputPath.isEmpty()) {
        QTextStream(stdout) << QString::fromUtf8(output);
    } else {
        QSaveFile outputFile(outputPath);
        if (!outputFile.open(QIODevice::WriteOnly)
            || outputFile.write(output) != output.size()
            || !outputFile.commit()) {
            errorStream << QStringLiteral("无法原子写入报告文件\n");
            return 1;
        }
    }
    return manifest.rejectedFiles == 0 ? 0 : 2;
}
