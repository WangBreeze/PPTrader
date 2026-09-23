#include "strategy/CustomStrategyValidator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <optional>

namespace pp {

namespace {

constexpr int kMaximumExpressionDepth = 8;
constexpr int kMaximumExpressionNodes = 64;

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool hasOnlyKeys(const QJsonObject &object, const QSet<QString> &allowed,
                 const QString &path, QString *error)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowed.contains(it.key())) {
            return fail(error, QStringLiteral("%1 包含不允许的字段：%2").arg(path, it.key()));
        }
    }
    return true;
}

bool finiteNumber(const QJsonValue &value, double *number)
{
    if (!value.isDouble()) return false;
    const double candidate = value.toDouble();
    if (!std::isfinite(candidate)) return false;
    if (number) *number = candidate;
    return true;
}

bool validText(const QJsonObject &object, const QString &key, int minimumLength,
               int maximumLength, QString *value, QString *error)
{
    const QString text = object.value(key).toString().trimmed();
    if (text.size() < minimumLength || text.size() > maximumLength) {
        return fail(error, QStringLiteral("%1 长度必须在 %2–%3 之间")
                               .arg(key).arg(minimumLength).arg(maximumLength));
    }
    if (value) *value = text;
    return true;
}

bool validAliasKey(const QString &alias, QString *error)
{
    static const QRegularExpression aliasPattern(
        QStringLiteral("^[a-z][A-Za-z0-9_]{0,31}$"));
    if (alias.isEmpty() || !aliasPattern.match(alias).hasMatch()) {
        return fail(error, QStringLiteral("指标别名必须是小写字母开头的 1–32 位字母、数字、下划线"));
    }
    return true;
}

bool collectOperandAlias(const QJsonObject &operand, QSet<QString> *aliases,
                         QString *error)
{
    if (!operand.contains(QStringLiteral("indicator"))
        || !operand.contains(QStringLiteral("alias"))) {
        return true;
    }
    const QString alias = operand.value(QStringLiteral("alias")).toString();
    if (!validAliasKey(alias, error)) return false;
    if (aliases->contains(alias)) {
        return fail(error, QStringLiteral("指标别名重复：%1").arg(alias));
    }
    aliases->insert(alias);
    return true;
}

bool collectConditionAliases(const QJsonObject &condition, QSet<QString> *aliases,
                             QString *error)
{
    const QString op = condition.value(QStringLiteral("op")).toString();
    if (op == QStringLiteral("and") || op == QStringLiteral("or")) {
        const QJsonArray children = condition.value(QStringLiteral("conditions")).toArray();
        for (const QJsonValue &child : children) {
            if (!child.isObject()
                || !collectConditionAliases(child.toObject(), aliases, error)) {
                return false;
            }
        }
        return true;
    }
    if (op == QStringLiteral("not")) {
        return condition.value(QStringLiteral("condition")).isObject()
            && collectConditionAliases(
                condition.value(QStringLiteral("condition")).toObject(), aliases, error);
    }
    return collectOperandAlias(condition.value(QStringLiteral("left")).toObject(),
                               aliases, error)
        && collectOperandAlias(condition.value(QStringLiteral("right")).toObject(),
                               aliases, error);
}

bool validateSeriesItem(const QJsonObject &item, const QString &path,
                        QSet<QString> *aliases, QString *error)
{
    if (!item.contains(QStringLiteral("alias"))
        || !item.contains(QStringLiteral("indicator"))) {
        return fail(error, QStringLiteral("%1 必须包含 alias 和 indicator").arg(path));
    }
    const QString alias = item.value(QStringLiteral("alias")).toString();
    if (!validAliasKey(alias, error)) return false;
    if (aliases->contains(alias)) {
        return fail(error, QStringLiteral("指标别名重复：%1").arg(alias));
    }
    aliases->insert(alias);
    return true;
}

QDateTime parseTimestamp(const QJsonValue &value)
{
    if (!value.isString()) return {};
    QDateTime result = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    if (!result.isValid()) result = QDateTime::fromString(value.toString(), Qt::ISODate);
    return result.isValid() ? result.toUTC() : QDateTime{};
}

} // namespace

bool CustomStrategyValidator::parseAndValidate(const QString &json,
                                               CustomStrategyDefinition *definition,
                                               QString *error) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(error, QStringLiteral("策略 JSON 无效（位置 %1）：%2")
                               .arg(parseError.offset).arg(parseError.errorString()));
    }
    return validate(document.object(), definition, error);
}

bool CustomStrategyValidator::validate(const QJsonObject &input,
                                       CustomStrategyDefinition *definition,
                                       QString *error) const
{
    static const QSet<QString> allowedTopLevel {
        QStringLiteral("schemaVersion"), QStringLiteral("strategyId"),
        QStringLiteral("version"), QStringLiteral("name"),
        QStringLiteral("description"), QStringLiteral("author"),
        QStringLiteral("market"), QStringLiteral("executionScope"),
        QStringLiteral("createdAt"), QStringLiteral("updatedAt"),
        QStringLiteral("parameters"), QStringLiteral("position"), QStringLiteral("risk"),
        QStringLiteral("entry"), QStringLiteral("exit"), QStringLiteral("series"),
        QStringLiteral("signal"), QStringLiteral("universe"),
        QStringLiteral("crossIndicators"), QStringLiteral("selection")
    };
    if (!hasOnlyKeys(input, allowedTopLevel, QStringLiteral("策略定义"), error)) return false;
    const int schemaVersion = input.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (schemaVersion != 1 && schemaVersion != 2 && schemaVersion != 3) {
        return fail(error, QStringLiteral("仅支持 schemaVersion=1/2/3"));
    }
    const bool v2 = schemaVersion >= 2;
    const bool v3 = schemaVersion == 3;
    if (input.contains(QStringLiteral("series")) && !v2) {
        return fail(error, QStringLiteral("series 需要 schemaVersion=2"));
    }
    if (input.contains(QStringLiteral("signal")) && !v2) {
        return fail(error, QStringLiteral("signal 需要 schemaVersion=2"));
    }
    if ((input.contains(QStringLiteral("universe"))
         || input.contains(QStringLiteral("crossIndicators"))
         || input.contains(QStringLiteral("selection"))) && !v3) {
        return fail(error, QStringLiteral("universe/crossIndicators/selection 需要 schemaVersion=3"));
    }
    if (v2 && input.contains(QStringLiteral("signal"))) {
        if (!input.value(QStringLiteral("signal")).isObject()) {
            return fail(error, QStringLiteral("signal 必须是对象"));
        }
        const QJsonObject signal = input.value(QStringLiteral("signal")).toObject();
        if (!hasOnlyKeys(signal, {QStringLiteral("priceMode")},
                         QStringLiteral("signal"), error)) {
            return false;
        }
        const QString priceMode = signal.value(QStringLiteral("priceMode"))
            .toString(QStringLiteral("triggerClose"));
        if (priceMode != QStringLiteral("triggerClose")
            && priceMode != QStringLiteral("nextOpen")) {
            return fail(error, QStringLiteral("signal.priceMode 仅支持 triggerClose/nextOpen"));
        }
    }

    CustomStrategyDefinition result;
    result.schemaVersion = schemaVersion;
    if (!validText(input, QStringLiteral("strategyId"), 7, 64, &result.strategyId, error)) {
        return false;
    }
    static const QRegularExpression idPattern(QStringLiteral("^custom_[a-z0-9][a-z0-9_]*$"));
    if (!idPattern.match(result.strategyId).hasMatch()) {
        return fail(error, QStringLiteral("strategyId 必须使用 custom_ 前缀和小写字母、数字、下划线"));
    }
    result.version = input.value(QStringLiteral("version")).toInt(-1);
    if (result.version < 1 || result.version > 1000000) {
        return fail(error, QStringLiteral("version 必须在 1–1000000 之间"));
    }
    if (!validText(input, QStringLiteral("name"), 1, 64, &result.name, error)
        || !validText(input, QStringLiteral("author"), 1, 64, &result.author, error)) {
        return false;
    }
    result.description = input.value(QStringLiteral("description")).toString().trimmed();
    if (result.description.size() > 500) {
        return fail(error, QStringLiteral("description 不能超过 500 个字符"));
    }
    result.market = input.value(QStringLiteral("market")).toString();
    if (result.market != QStringLiteral("A股")) {
        return fail(error, QStringLiteral("首版自定义策略仅支持 A股"));
    }
    result.executionScope = input.value(QStringLiteral("executionScope")).toString();
    if (result.executionScope != QStringLiteral("backtest_only")
        && result.executionScope != QStringLiteral("mock_execution")) {
        return fail(error,
                    QStringLiteral("executionScope 仅支持 backtest_only 或 mock_execution"));
    }
    result.createdAt = parseTimestamp(input.value(QStringLiteral("createdAt")));
    if (!result.createdAt.isValid()) {
        return fail(error, QStringLiteral("createdAt 必须是有效的 ISO 8601 时间"));
    }
    result.updatedAt = input.contains(QStringLiteral("updatedAt"))
        ? parseTimestamp(input.value(QStringLiteral("updatedAt"))) : result.createdAt;
    if (!result.updatedAt.isValid() || result.updatedAt < result.createdAt) {
        return fail(error, QStringLiteral("updatedAt 必须有效且不早于 createdAt"));
    }

    if (input.contains(QStringLiteral("position"))) {
        if (!input.value(QStringLiteral("position")).isObject()) {
            return fail(error, QStringLiteral("position 必须是对象"));
        }
        const QJsonObject position = input.value(QStringLiteral("position")).toObject();
        const QString positionType = position.value(QStringLiteral("type"))
            .toString(QStringLiteral("fixedPercent"));
        if (positionType == QStringLiteral("fixedPercent")) {
            if (!hasOnlyKeys(position, {QStringLiteral("type"), QStringLiteral("percent")},
                             QStringLiteral("position"), error)) {
                return false;
            }
            double percent = 0.0;
            if (!finiteNumber(position.value(QStringLiteral("percent")), &percent)
                || percent < 0.01 || percent > 1.0) {
                return fail(error, QStringLiteral("position.percent 必须在 0.01–1.0 之间"));
            }
        } else if (positionType == QStringLiteral("equalWeight") && v3) {
            if (!hasOnlyKeys(position, {QStringLiteral("type")}, QStringLiteral("position"), error)) {
                return false;
            }
        } else if (positionType == QStringLiteral("fixedAmount")) {
            if (!hasOnlyKeys(position, {QStringLiteral("type"), QStringLiteral("amount")},
                             QStringLiteral("position"), error)) {
                return false;
            }
            double amount = 0.0;
            if (!finiteNumber(position.value(QStringLiteral("amount")), &amount)
                || amount <= 0.0) {
                return fail(error, QStringLiteral("position.amount 必须是正数"));
            }
        } else {
            return fail(error, QStringLiteral("position.type 仅支持 fixedPercent/fixedAmount/equalWeight"));
        }
    }

    if (input.contains(QStringLiteral("risk"))) {
        if (!input.value(QStringLiteral("risk")).isObject()) {
            return fail(error, QStringLiteral("risk 必须是对象"));
        }
        const QJsonObject risk = input.value(QStringLiteral("risk")).toObject();
        if (!hasOnlyKeys(risk,
                         {QStringLiteral("stopLossPercent"),
                          QStringLiteral("takeProfitPercent"),
                          QStringLiteral("maximumHoldingBars"),
                          QStringLiteral("tPlusOne"),
                          QStringLiteral("maxPositionPercent")},
                         QStringLiteral("risk"), error)) {
            return false;
        }
        auto validatePercent = [&](const QString &key, double maximum) {
            if (!risk.contains(key)) return true;
            double value = 0.0;
            return finiteNumber(risk.value(key), &value) && value >= 0.0 && value <= maximum;
        };
        if (!validatePercent(QStringLiteral("stopLossPercent"), 100.0)) {
            return fail(error, QStringLiteral("risk.stopLossPercent 必须在 0–100 之间"));
        }
        if (!validatePercent(QStringLiteral("takeProfitPercent"), 1000.0)) {
            return fail(error, QStringLiteral("risk.takeProfitPercent 必须在 0–1000 之间"));
        }
        if (!validatePercent(QStringLiteral("maxPositionPercent"), 100.0)) {
            return fail(error, QStringLiteral("risk.maxPositionPercent 必须在 0–100 之间"));
        }
        if (risk.contains(QStringLiteral("maximumHoldingBars"))) {
            double bars = 0.0;
            if (!finiteNumber(risk.value(QStringLiteral("maximumHoldingBars")), &bars)
                || std::floor(bars) != bars || bars < 0.0 || bars > 5000.0) {
                return fail(error, QStringLiteral("risk.maximumHoldingBars 必须是 0–5000 的整数"));
            }
        }
        if (risk.contains(QStringLiteral("tPlusOne"))
            && (!risk.value(QStringLiteral("tPlusOne")).isBool()
                || !risk.value(QStringLiteral("tPlusOne")).toBool())) {
            return fail(error, QStringLiteral("A 股自定义策略必须启用 risk.tPlusOne"));
        }
    }

    if (!input.value(QStringLiteral("parameters")).isArray()) {
        return fail(error, QStringLiteral("parameters 必须是数组"));
    }
    ParameterMap parameters;
    if (!validateParameters(input.value(QStringLiteral("parameters")).toArray(),
                            &parameters, error)) {
        return false;
    }
    if (!input.value(QStringLiteral("entry")).isObject()
        || !input.value(QStringLiteral("exit")).isObject()) {
        return fail(error, QStringLiteral("entry 和 exit 必须是条件对象"));
    }
    int nodeCount = 0;
    QSet<QString> aliases;
    QSet<QString> crossAliases;
    if (v2 && input.contains(QStringLiteral("series"))) {
        const QJsonArray series = input.value(QStringLiteral("series")).toArray();
        if (series.size() < 1 || series.size() > 16) {
            return fail(error, QStringLiteral("series 必须包含 1–16 项"));
        }
        for (int index = 0; index < series.size(); ++index) {
            if (!series.at(index).isObject()) {
                return fail(error, QStringLiteral("series[%1] 必须是对象").arg(index));
            }
            const QJsonObject item = series.at(index).toObject();
            const QString path = QStringLiteral("series[%1]").arg(index);
            if (!validateSeriesItem(item, path, &aliases, error)) return false;
            QJsonObject operand = item;
            operand.remove(QStringLiteral("alias"));
            if (!validateOperand(operand, parameters, path, 1, v2, aliases,
                                 crossAliases, &nodeCount, error)) {
                return false;
            }
        }
    }
    if (v3) {
        if (!input.value(QStringLiteral("universe")).isObject()) {
            return fail(error, QStringLiteral("schemaVersion=3 需要 universe 对象"));
        }
        const QJsonObject universe = input.value(QStringLiteral("universe")).toObject();
        if (!hasOnlyKeys(universe, {QStringLiteral("mode"), QStringLiteral("symbols")},
                         QStringLiteral("universe"), error)) {
            return false;
        }
        if (universe.value(QStringLiteral("mode")).toString() != QStringLiteral("explicit")) {
            return fail(error, QStringLiteral("universe.mode 首期仅支持 explicit"));
        }
        static const QRegularExpression symbolPattern(QStringLiteral("^[0-9]{6}$"));
        const QJsonArray universeSymbols = universe.value(QStringLiteral("symbols")).toArray();
        if (universeSymbols.size() < 1 || universeSymbols.size() > 512) {
            return fail(error, QStringLiteral("universe.symbols 必须包含 1–512 项"));
        }
        for (int index = 0; index < universeSymbols.size(); ++index) {
            if (!symbolPattern.match(universeSymbols.at(index).toString()).hasMatch()) {
                return fail(error, QStringLiteral("universe.symbols[%1] 必须是 6 位数字代码").arg(index));
            }
        }

        if (!input.value(QStringLiteral("crossIndicators")).isArray()) {
            return fail(error, QStringLiteral("schemaVersion=3 需要 crossIndicators 数组"));
        }
        const QJsonArray crossIndicators = input.value(QStringLiteral("crossIndicators")).toArray();
        if (crossIndicators.size() < 1 || crossIndicators.size() > 32) {
            return fail(error, QStringLiteral("crossIndicators 必须包含 1–32 项"));
        }
        static const QSet<QString> crossIndicatorNames {
            QStringLiteral("RANK"), QStringLiteral("ZSCORE"), QStringLiteral("PERCENTILE"),
            QStringLiteral("MEAN"), QStringLiteral("MEDIAN"), QStringLiteral("PEER")
        };
        for (int index = 0; index < crossIndicators.size(); ++index) {
            if (!crossIndicators.at(index).isObject()) {
                return fail(error, QStringLiteral("crossIndicators[%1] 必须是对象").arg(index));
            }
            const QJsonObject item = crossIndicators.at(index).toObject();
            const QString path = QStringLiteral("crossIndicators[%1]").arg(index);
            const QString alias = item.value(QStringLiteral("alias")).toString();
            if (!validAliasKey(alias, error)) return false;
            if (crossAliases.contains(alias)) {
                return fail(error, QStringLiteral("横截面指标别名重复：%1").arg(alias));
            }
            crossAliases.insert(alias);
            const QString name = item.value(QStringLiteral("indicator")).toString().toUpper();
            if (!crossIndicatorNames.contains(name)) {
                return fail(error, QStringLiteral("%1 使用了未知横截面指标：%2").arg(path, name));
            }
            if (name == QStringLiteral("PEER")) {
                if (!hasOnlyKeys(item, {QStringLiteral("alias"), QStringLiteral("indicator"),
                                        QStringLiteral("symbol"), QStringLiteral("field")},
                                 path, error)) {
                    return false;
                }
                if (!symbolPattern.match(item.value(QStringLiteral("symbol")).toString()).hasMatch()) {
                    return fail(error, QStringLiteral("%1.symbol 必须是 6 位数字代码").arg(path));
                }
                static const QSet<QString> fields {QStringLiteral("open"), QStringLiteral("high"),
                                                   QStringLiteral("low"), QStringLiteral("close"),
                                                   QStringLiteral("volume")};
                if (!fields.contains(item.value(QStringLiteral("field")).toString())) {
                    return fail(error, QStringLiteral("%1.field 必须是 OHLCV 字段").arg(path));
                }
            } else {
                QSet<QString> allowed {QStringLiteral("alias"), QStringLiteral("indicator"),
                                       QStringLiteral("of")};
                if (name == QStringLiteral("RANK")) allowed.insert(QStringLiteral("ascending"));
                if (!hasOnlyKeys(item, allowed, path, error)) return false;
                if (!item.value(QStringLiteral("of")).isObject()) {
                    return fail(error, QStringLiteral("%1.of 必须是对象").arg(path));
                }
                const QJsonObject of = item.value(QStringLiteral("of")).toObject();
                if (of.contains(QStringLiteral("field"))) {
                    static const QSet<QString> fields {QStringLiteral("open"), QStringLiteral("high"),
                                                       QStringLiteral("low"), QStringLiteral("close"),
                                                       QStringLiteral("volume")};
                    if (!hasOnlyKeys(of, {QStringLiteral("field")}, path + QStringLiteral(".of"), error)
                        || !fields.contains(of.value(QStringLiteral("field")).toString())) {
                        return fail(error, QStringLiteral("%1.of.field 必须是 OHLCV 字段").arg(path));
                    }
                } else if (of.contains(QStringLiteral("indicator"))) {
                    if (!validateOperand(of, parameters, path + QStringLiteral(".of"), 1, v2,
                                         aliases, crossAliases, &nodeCount, error)) {
                        return false;
                    }
                } else {
                    return fail(error, QStringLiteral("%1.of 必须是 field 或 indicator").arg(path));
                }
                if (name == QStringLiteral("RANK")
                    && item.contains(QStringLiteral("ascending"))
                    && !item.value(QStringLiteral("ascending")).isBool()) {
                    return fail(error, QStringLiteral("%1.ascending 必须是布尔值").arg(path));
                }
            }
        }

        if (input.contains(QStringLiteral("selection"))) {
            if (!input.value(QStringLiteral("selection")).isObject()) {
                return fail(error, QStringLiteral("selection 必须是对象"));
            }
            const QJsonObject selection = input.value(QStringLiteral("selection")).toObject();
            if (!hasOnlyKeys(selection, {QStringLiteral("mode"), QStringLiteral("rankBy"),
                                         QStringLiteral("count"), QStringLiteral("ascending")},
                             QStringLiteral("selection"), error)) {
                return false;
            }
            if (selection.value(QStringLiteral("mode")).toString() != QStringLiteral("topN")) {
                return fail(error, QStringLiteral("selection.mode 首期仅支持 topN"));
            }
            const QString rankBy = selection.value(QStringLiteral("rankBy")).toString();
            if (!crossAliases.contains(rankBy)) {
                return fail(error, QStringLiteral("selection.rankBy 引用了未定义的横截面指标：%1").arg(rankBy));
            }
            double count = 0.0;
            if (!finiteNumber(selection.value(QStringLiteral("count")), &count)
                || std::floor(count) != count || count < 1.0 || count > 512.0) {
                return fail(error, QStringLiteral("selection.count 必须是 1–512 的整数"));
            }
            if (selection.contains(QStringLiteral("ascending"))
                && !selection.value(QStringLiteral("ascending")).isBool()) {
                return fail(error, QStringLiteral("selection.ascending 必须是布尔值"));
            }
        }
    }
    if (v2
        && (!collectConditionAliases(input.value(QStringLiteral("entry")).toObject(),
                                     &aliases, error)
            || !collectConditionAliases(input.value(QStringLiteral("exit")).toObject(),
                                        &aliases, error))) {
        return false;
    }
    if (!validateCondition(input.value(QStringLiteral("entry")).toObject(), parameters,
                           QStringLiteral("entry"), 1, v2, aliases, crossAliases,
                           &nodeCount, error)
        || !validateCondition(input.value(QStringLiteral("exit")).toObject(), parameters,
                              QStringLiteral("exit"), 1, v2, aliases, crossAliases,
                              &nodeCount, error)) {
        return false;
    }

    QJsonObject normalized = input;
    normalized.insert(QStringLiteral("strategyId"), result.strategyId);
    normalized.insert(QStringLiteral("name"), result.name);
    normalized.insert(QStringLiteral("author"), result.author);
    normalized.insert(QStringLiteral("description"), result.description);
    normalized.insert(QStringLiteral("createdAt"), result.createdAt.toString(Qt::ISODateWithMs));
    normalized.insert(QStringLiteral("updatedAt"), result.updatedAt.toString(Qt::ISODateWithMs));
    result.document = normalized;
    if (definition) *definition = result;
    if (error) error->clear();
    return true;
}

bool CustomStrategyValidator::validateParameters(const QJsonArray &items,
                                                 ParameterMap *parameterMap,
                                                 QString *error) const
{
    if (items.size() > 32) return fail(error, QStringLiteral("parameters 最多允许 32 项"));
    static const QRegularExpression keyPattern(QStringLiteral("^[a-z][A-Za-z0-9_]{0,31}$"));
    static const QSet<QString> allowedKeys {
        QStringLiteral("key"), QStringLiteral("displayName"), QStringLiteral("type"),
        QStringLiteral("defaultValue"), QStringLiteral("minimumValue"),
        QStringLiteral("maximumValue"), QStringLiteral("step")
    };
    for (int index = 0; index < items.size(); ++index) {
        if (!items.at(index).isObject()) {
            return fail(error, QStringLiteral("parameters[%1] 必须是对象").arg(index));
        }
        const QJsonObject item = items.at(index).toObject();
        const QString path = QStringLiteral("parameters[%1]").arg(index);
        if (!hasOnlyKeys(item, allowedKeys, path, error)) return false;
        const QString key = item.value(QStringLiteral("key")).toString();
        if (!keyPattern.match(key).hasMatch() || parameterMap->contains(key)) {
            return fail(error, QStringLiteral("%1.key 非法或重复").arg(path));
        }
        const QString displayName = item.value(QStringLiteral("displayName")).toString().trimmed();
        if (displayName.isEmpty() || displayName.size() > 64) {
            return fail(error, QStringLiteral("%1.displayName 长度必须在 1–64 之间").arg(path));
        }
        const QString type = item.value(QStringLiteral("type")).toString();
        if (type != QStringLiteral("integer") && type != QStringLiteral("real")) {
            return fail(error, QStringLiteral("%1.type 仅支持 integer/real").arg(path));
        }
        double defaultValue = 0.0;
        double minimumValue = 0.0;
        double maximumValue = 0.0;
        double step = 0.0;
        if (!finiteNumber(item.value(QStringLiteral("defaultValue")), &defaultValue)
            || !finiteNumber(item.value(QStringLiteral("minimumValue")), &minimumValue)
            || !finiteNumber(item.value(QStringLiteral("maximumValue")), &maximumValue)
            || !finiteNumber(item.value(QStringLiteral("step")), &step)
            || minimumValue > maximumValue || defaultValue < minimumValue
            || defaultValue > maximumValue || step <= 0.0) {
            return fail(error, QStringLiteral("%1 的默认值、范围或步长无效").arg(path));
        }
        if (type == QStringLiteral("integer")
            && (std::floor(defaultValue) != defaultValue || std::floor(minimumValue) != minimumValue
                || std::floor(maximumValue) != maximumValue || std::floor(step) != step)) {
            return fail(error, QStringLiteral("%1 的 integer 参数必须全部为整数").arg(path));
        }
        parameterMap->insert(key, {type, defaultValue, minimumValue, maximumValue});
    }
    return true;
}

bool CustomStrategyValidator::validateCondition(const QJsonObject &condition,
                                                const ParameterMap &parameters,
                                                const QString &path, int depth, bool v2,
                                                const QSet<QString> &aliases,
                                                const QSet<QString> &crossAliases,
                                                int *nodeCount, QString *error) const
{
    if (depth > kMaximumExpressionDepth) {
        return fail(error, QStringLiteral("%1 超过最大表达式深度 %2").arg(path).arg(kMaximumExpressionDepth));
    }
    if (++(*nodeCount) > kMaximumExpressionNodes) {
        return fail(error, QStringLiteral("条件表达式最多允许 %1 个节点").arg(kMaximumExpressionNodes));
    }
    const QString op = condition.value(QStringLiteral("op")).toString();
    if (op == QStringLiteral("and") || op == QStringLiteral("or")) {
        if (!hasOnlyKeys(condition, {QStringLiteral("op"), QStringLiteral("conditions")}, path, error)) return false;
        const QJsonArray children = condition.value(QStringLiteral("conditions")).toArray();
        if (children.size() < 2 || children.size() > 16) {
            return fail(error, QStringLiteral("%1.conditions 必须包含 2–16 项").arg(path));
        }
        for (int index = 0; index < children.size(); ++index) {
            if (!children.at(index).isObject()
                || !validateCondition(children.at(index).toObject(), parameters,
                                      QStringLiteral("%1.conditions[%2]").arg(path).arg(index),
                                      depth + 1, v2, aliases, crossAliases, nodeCount, error)) {
                if (error && error->isEmpty()) *error = QStringLiteral("%1.conditions[%2] 必须是条件对象").arg(path).arg(index);
                return false;
            }
        }
        return true;
    }
    if (op == QStringLiteral("not")) {
        if (!hasOnlyKeys(condition, {QStringLiteral("op"), QStringLiteral("condition")}, path, error)
            || !condition.value(QStringLiteral("condition")).isObject()) {
            return fail(error, QStringLiteral("%1.condition 必须是条件对象").arg(path));
        }
        return validateCondition(condition.value(QStringLiteral("condition")).toObject(), parameters,
                                 path + QStringLiteral(".condition"), depth + 1, v2,
                                 aliases, crossAliases, nodeCount, error);
    }
    static const QSet<QString> comparisonOps {
        QStringLiteral("gt"), QStringLiteral("gte"), QStringLiteral("lt"),
        QStringLiteral("lte"), QStringLiteral("eq"), QStringLiteral("crossesAbove"),
        QStringLiteral("crossesBelow")
    };
    if (!comparisonOps.contains(op)) {
        return fail(error, QStringLiteral("%1.op 是未知或不允许的操作符：%2").arg(path, op));
    }
    if (!hasOnlyKeys(condition, {QStringLiteral("op"), QStringLiteral("left"), QStringLiteral("right")}, path, error)
        || !condition.value(QStringLiteral("left")).isObject()
        || !condition.value(QStringLiteral("right")).isObject()) {
        return fail(error, QStringLiteral("%1.left/right 必须是操作数对象").arg(path));
    }
    return validateOperand(condition.value(QStringLiteral("left")).toObject(), parameters,
                           path + QStringLiteral(".left"), depth + 1, v2, aliases,
                           crossAliases, nodeCount, error)
        && validateOperand(condition.value(QStringLiteral("right")).toObject(), parameters,
                           path + QStringLiteral(".right"), depth + 1, v2, aliases,
                           crossAliases, nodeCount, error);
}

bool CustomStrategyValidator::validateOperand(const QJsonObject &operand,
                                              const ParameterMap &parameters,
                                              const QString &path, int depth, bool v2,
                                              const QSet<QString> &aliases,
                                              const QSet<QString> &crossAliases,
                                              int *nodeCount, QString *error) const
{
    if (depth > kMaximumExpressionDepth || ++(*nodeCount) > kMaximumExpressionNodes) {
        return fail(error, QStringLiteral("%1 超过表达式复杂度限制").arg(path));
    }
    if (operand.contains(QStringLiteral("value"))) {
        if (!hasOnlyKeys(operand, {QStringLiteral("value")}, path, error)
            || !finiteNumber(operand.value(QStringLiteral("value")), nullptr)) {
            return fail(error, QStringLiteral("%1.value 必须是有限数字").arg(path));
        }
        return true;
    }
    if (operand.contains(QStringLiteral("parameter"))) {
        if (!hasOnlyKeys(operand, {QStringLiteral("parameter")}, path, error)) return false;
        const QString key = operand.value(QStringLiteral("parameter")).toString();
        return parameters.contains(key)
            ? true : fail(error, QStringLiteral("%1 引用了未知参数：%2").arg(path, key));
    }
    if (operand.contains(QStringLiteral("field"))) {
        if (!hasOnlyKeys(operand, {QStringLiteral("field")}, path, error)) return false;
        static const QSet<QString> fields {QStringLiteral("open"), QStringLiteral("high"),
                                           QStringLiteral("low"), QStringLiteral("close"),
                                           QStringLiteral("volume")};
        const QString field = operand.value(QStringLiteral("field")).toString();
        return fields.contains(field)
            ? true : fail(error, QStringLiteral("%1 引用了未知或未来字段：%2").arg(path, field));
    }
    if (operand.contains(QStringLiteral("series"))) {
        if (!v2) {
            return fail(error, QStringLiteral("%1 需要 schemaVersion=2 才能引用指标序列").arg(path));
        }
        if (!hasOnlyKeys(operand, {QStringLiteral("series")}, path, error)) return false;
        const QString alias = operand.value(QStringLiteral("series")).toString();
        return aliases.contains(alias)
            ? true : fail(error, QStringLiteral("%1 引用了未定义的指标别名：%2").arg(path, alias));
    }
    if (operand.contains(QStringLiteral("cross"))) {
        if (!hasOnlyKeys(operand, {QStringLiteral("cross")}, path, error)) return false;
        const QString alias = operand.value(QStringLiteral("cross")).toString();
        return crossAliases.contains(alias)
            ? true : fail(error, QStringLiteral("%1 引用了未定义的横截面指标：%2").arg(path, alias));
    }
    if (!operand.contains(QStringLiteral("indicator"))) {
        return fail(error, QStringLiteral("%1 不是受支持的操作数").arg(path));
    }

    const QString indicator = operand.value(QStringLiteral("indicator")).toString().toUpper();
    if (operand.contains(QStringLiteral("alias")) && !v2) {
        return fail(error, QStringLiteral("%1 需要 schemaVersion=2 才能定义指标别名").arg(path));
    }
    auto validateNumericOption = [&](const QString &key, double minimum, double maximum) {
        const QJsonValue value = operand.value(key);
        double number = 0.0;
        if (finiteNumber(value, &number)) return number >= minimum && number <= maximum;
        if (!value.isObject()) return false;
        const QJsonObject reference = value.toObject();
        if (!hasOnlyKeys(reference, {QStringLiteral("parameter")}, path + QStringLiteral(".") + key, error)) return false;
        const auto it = parameters.constFind(reference.value(QStringLiteral("parameter")).toString());
        return it != parameters.constEnd() && it->minimumValue >= minimum && it->maximumValue <= maximum;
    };
    auto defaultNumericOption = [&](const QString &key) -> std::optional<double> {
        const QJsonValue value = operand.value(key);
        double number = 0.0;
        if (finiteNumber(value, &number)) return number;
        if (!value.isObject()) return std::nullopt;
        const auto it = parameters.constFind(
            value.toObject().value(QStringLiteral("parameter")).toString());
        return it == parameters.constEnd() ? std::nullopt
                                           : std::optional<double>{it->defaultValue};
    };

    auto validateIntegerOption = [&](const QString &key, int minimum, int maximum) {
        if (!validateNumericOption(key, minimum, maximum)) return false;
        const QJsonValue value = operand.value(key);
        if (value.isDouble()) return std::floor(value.toDouble()) == value.toDouble();
        const auto it = parameters.constFind(
            value.toObject().value(QStringLiteral("parameter")).toString());
        return it != parameters.constEnd() && it->type == QStringLiteral("integer");
    };
    if (v2 && (indicator == QStringLiteral("WMA") || indicator == QStringLiteral("BIAS")
               || indicator == QStringLiteral("CCI") || indicator == QStringLiteral("WR")
               || indicator == QStringLiteral("MFI"))) {
        QSet<QString> allowed {QStringLiteral("indicator"), QStringLiteral("period"),
                               QStringLiteral("alias")};
        if (!hasOnlyKeys(operand, allowed, path, error)
            || !validateIntegerOption(QStringLiteral("period"), 2, 500)) {
            return fail(error, QStringLiteral("%1.%2 的 period 必须是 2–500 的整数").arg(path, indicator));
        }
        return true;
    }
    if (v2 && indicator == QStringLiteral("KDJ")) {
        if (!hasOnlyKeys(operand, {QStringLiteral("indicator"), QStringLiteral("period"),
                                  QStringLiteral("kPeriod"), QStringLiteral("dPeriod"),
                                  QStringLiteral("output"), QStringLiteral("alias")}, path, error)) return false;
        for (const QString &key : {QStringLiteral("period"), QStringLiteral("kPeriod"),
                                   QStringLiteral("dPeriod")}) {
            const bool window = key == QStringLiteral("period");
            if (!validateIntegerOption(key, window ? 2 : 1, window ? 500 : 100)) {
                return fail(error, QStringLiteral("%1.KDJ 的 %2 必须为范围内的整数").arg(path, key));
            }
        }
        if (!QSet<QString>{QStringLiteral("k"), QStringLiteral("d"), QStringLiteral("j")}
                 .contains(operand.value(QStringLiteral("output")).toString())) {
            return fail(error, QStringLiteral("%1.KDJ 的输出必须是 k/d/j").arg(path));
        }
        return true;
    }
    if (v2 && indicator == QStringLiteral("DONCHIAN")) {
        if (!hasOnlyKeys(operand, {QStringLiteral("indicator"), QStringLiteral("period"),
                                  QStringLiteral("output"), QStringLiteral("alias")}, path, error)
            || !validateIntegerOption(QStringLiteral("period"), 2, 500)
            || !QSet<QString>{QStringLiteral("upper"), QStringLiteral("middle"), QStringLiteral("lower")}
                    .contains(operand.value(QStringLiteral("output")).toString())) {
            return fail(error, QStringLiteral("%1 的 DONCHIAN 配置无效：整数周期 2–500，输出 upper/middle/lower").arg(path));
        }
        return true;
    }
    if (indicator == QStringLiteral("SMA") || indicator == QStringLiteral("EMA")
        || indicator == QStringLiteral("RSI") || indicator == QStringLiteral("ATR")
        || indicator == QStringLiteral("VOLUME_SMA")
        || (v2 && indicator == QStringLiteral("ROC"))) {
        QSet<QString> allowed {QStringLiteral("indicator"), QStringLiteral("period")};
        if (v2) allowed.insert(QStringLiteral("alias"));
        if (!hasOnlyKeys(operand, allowed, path, error)
            || !validateNumericOption(QStringLiteral("period"), 2.0, 500.0)) {
            return fail(error, QStringLiteral("%1.%2 的 period 必须在 2–500 之间").arg(path, indicator));
        }
        return true;
    }
    if (indicator == QStringLiteral("MACD")) {
        QSet<QString> allowed {QStringLiteral("indicator"), QStringLiteral("fastPeriod"),
                               QStringLiteral("slowPeriod"), QStringLiteral("signalPeriod"),
                               QStringLiteral("output")};
        if (v2) allowed.insert(QStringLiteral("alias"));
        const auto fastDefault = defaultNumericOption(QStringLiteral("fastPeriod"));
        const auto slowDefault = defaultNumericOption(QStringLiteral("slowPeriod"));
        if (!hasOnlyKeys(operand, allowed, path, error)
            || !validateNumericOption(QStringLiteral("fastPeriod"), 2.0, 120.0)
            || !validateNumericOption(QStringLiteral("slowPeriod"), 3.0, 250.0)
            || !validateNumericOption(QStringLiteral("signalPeriod"), 2.0, 120.0)
            || !fastDefault || !slowDefault || *fastDefault >= *slowDefault
            || !QSet<QString>{QStringLiteral("macd"), QStringLiteral("signal"), QStringLiteral("histogram")}
                    .contains(operand.value(QStringLiteral("output")).toString())) {
            return fail(error, QStringLiteral("%1 的 MACD 配置无效").arg(path));
        }
        return true;
    }
    if (indicator == QStringLiteral("BOLLINGER")) {
        QSet<QString> allowed {QStringLiteral("indicator"), QStringLiteral("period"),
                               QStringLiteral("standardDeviations"), QStringLiteral("output")};
        if (v2) allowed.insert(QStringLiteral("alias"));
        if (!hasOnlyKeys(operand, allowed, path, error)
            || !validateNumericOption(QStringLiteral("period"), 2.0, 500.0)
            || !validateNumericOption(QStringLiteral("standardDeviations"), 0.1, 10.0)
            || !QSet<QString>{QStringLiteral("upper"), QStringLiteral("middle"), QStringLiteral("lower")}
                    .contains(operand.value(QStringLiteral("output")).toString())) {
            return fail(error, QStringLiteral("%1 的 BOLLINGER 配置无效").arg(path));
        }
        return true;
    }
    if (v2 && (indicator == QStringLiteral("VWAP")
               || indicator == QStringLiteral("PP_FLOW") || indicator == QStringLiteral("OBV"))) {
        QSet<QString> allowed {QStringLiteral("indicator")};
        if (v2) allowed.insert(QStringLiteral("alias"));
        if (!hasOnlyKeys(operand, allowed, path, error)) return false;
        return true;
    }
    return fail(error, QStringLiteral("%1 使用了未知指标：%2").arg(path, indicator));
}

QVariantMap CustomStrategyValidator::schemaDescriptor() const
{
    const auto integerParameter = [](const QString &key, const QString &displayName,
                                     int defaultValue, int minimumValue,
                                     int maximumValue) {
        return QVariantMap{{QStringLiteral("key"), key},
                           {QStringLiteral("displayName"), displayName},
                           {QStringLiteral("type"), QStringLiteral("integer")},
                           {QStringLiteral("defaultValue"), defaultValue},
                           {QStringLiteral("minimumValue"), minimumValue},
                           {QStringLiteral("maximumValue"), maximumValue},
                           {QStringLiteral("step"), 1}};
    };
    const auto realParameter = [](const QString &key, const QString &displayName,
                                  double defaultValue, double minimumValue,
                                  double maximumValue, double step) {
        return QVariantMap{{QStringLiteral("key"), key},
                           {QStringLiteral("displayName"), displayName},
                           {QStringLiteral("type"), QStringLiteral("real")},
                           {QStringLiteral("defaultValue"), defaultValue},
                           {QStringLiteral("minimumValue"), minimumValue},
                           {QStringLiteral("maximumValue"), maximumValue},
                           {QStringLiteral("step"), step}};
    };
    const auto choiceParameter = [](const QString &key, const QString &displayName,
                                    const QString &defaultValue,
                                    const QStringList &choices,
                                    const QStringList &choiceLabels) {
        return QVariantMap{{QStringLiteral("key"), key},
                           {QStringLiteral("displayName"), displayName},
                           {QStringLiteral("type"), QStringLiteral("choice")},
                           {QStringLiteral("defaultValue"), defaultValue},
                           {QStringLiteral("choices"), choices},
                           {QStringLiteral("choiceLabels"), choiceLabels}};
    };
    const auto periodSchema = [&integerParameter](const QString &name,
                                                  const QString &displayName,
                                                  int defaultPeriod = 14) {
        return QVariantMap{{QStringLiteral("name"), name},
                           {QStringLiteral("displayName"), displayName},
                           {QStringLiteral("parameters"), QVariantList{
                               integerParameter(QStringLiteral("period"),
                                                QStringLiteral("周期"), defaultPeriod, 2, 500)}}};
    };

    const QVariantList indicatorSchemas {
        periodSchema(QStringLiteral("SMA"), QStringLiteral("简单移动平均")),
        periodSchema(QStringLiteral("EMA"), QStringLiteral("指数移动平均")),
        periodSchema(QStringLiteral("RSI"), QStringLiteral("相对强弱指标")),
        QVariantMap{{QStringLiteral("name"), QStringLiteral("MACD")},
                    {QStringLiteral("displayName"), QStringLiteral("MACD")},
                    {QStringLiteral("parameters"), QVariantList{
                        integerParameter(QStringLiteral("fastPeriod"),
                                         QStringLiteral("快速周期"), 12, 2, 120),
                        integerParameter(QStringLiteral("slowPeriod"),
                                         QStringLiteral("慢速周期"), 26, 3, 250),
                        integerParameter(QStringLiteral("signalPeriod"),
                                         QStringLiteral("信号周期"), 9, 2, 120),
                        choiceParameter(QStringLiteral("output"),
                                        QStringLiteral("输出序列"),
                                        QStringLiteral("histogram"),
                                        {QStringLiteral("macd"), QStringLiteral("signal"),
                                         QStringLiteral("histogram")},
                                        {QStringLiteral("MACD 线"), QStringLiteral("信号线"),
                                         QStringLiteral("柱状图")})}},
                    {QStringLiteral("relations"), QVariantList{QVariantMap{
                        {QStringLiteral("left"), QStringLiteral("fastPeriod")},
                        {QStringLiteral("op"), QStringLiteral("lt")},
                        {QStringLiteral("right"), QStringLiteral("slowPeriod")},
                        {QStringLiteral("message"), QStringLiteral("快速周期必须小于慢速周期")}}}}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("BOLLINGER")},
                    {QStringLiteral("displayName"), QStringLiteral("布林带")},
                    {QStringLiteral("parameters"), QVariantList{
                        integerParameter(QStringLiteral("period"), QStringLiteral("周期"),
                                         20, 2, 500),
                        realParameter(QStringLiteral("standardDeviations"),
                                      QStringLiteral("标准差倍数"), 2.0, 0.1, 10.0, 0.1),
                        choiceParameter(QStringLiteral("output"),
                                        QStringLiteral("输出序列"),
                                        QStringLiteral("middle"),
                                        {QStringLiteral("upper"), QStringLiteral("middle"),
                                         QStringLiteral("lower")},
                                        {QStringLiteral("上轨"), QStringLiteral("中轨"),
                                         QStringLiteral("下轨")})}}},
        periodSchema(QStringLiteral("ATR"), QStringLiteral("平均真实波幅")),
        periodSchema(QStringLiteral("VOLUME_SMA"), QStringLiteral("成交量移动平均")),
        periodSchema(QStringLiteral("ROC"), QStringLiteral("变动率")),
        periodSchema(QStringLiteral("WMA"), QStringLiteral("线性加权移动平均")),
        periodSchema(QStringLiteral("BIAS"), QStringLiteral("乖离率（百分比）")),
        periodSchema(QStringLiteral("CCI"), QStringLiteral("商品通道指标（典型价绝对偏差）"), 20),
        periodSchema(QStringLiteral("WR"), QStringLiteral("威廉指标（−100～0）")),
        periodSchema(QStringLiteral("MFI"), QStringLiteral("资金流量指标（N 次比较需 N+1 根 Bar）")),
        QVariantMap{{QStringLiteral("name"), QStringLiteral("KDJ")},
                    {QStringLiteral("displayName"), QStringLiteral("随机指标（K/D 初值 50，递归平滑）")},
                    {QStringLiteral("parameters"), QVariantList{
                        integerParameter(QStringLiteral("period"), QStringLiteral("RSV 周期"), 9, 2, 500),
                        integerParameter(QStringLiteral("kPeriod"), QStringLiteral("K 平滑周期"), 3, 1, 100),
                        integerParameter(QStringLiteral("dPeriod"), QStringLiteral("D 平滑周期"), 3, 1, 100),
                        choiceParameter(QStringLiteral("output"), QStringLiteral("输出序列"),
                                        QStringLiteral("k"),
                                        {QStringLiteral("k"), QStringLiteral("d"), QStringLiteral("j")},
                                        {QStringLiteral("K 线"), QStringLiteral("D 线"), QStringLiteral("J 线（不截断）")})}}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("OBV")},
                    {QStringLiteral("displayName"), QStringLiteral("能量潮（首根为零，不按日重置）")},
                    {QStringLiteral("parameters"), QVariantList{}}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("DONCHIAN")},
                    {QStringLiteral("displayName"), QStringLiteral("唐奇安通道（含当前 Bar）")},
                    {QStringLiteral("parameters"), QVariantList{
                        integerParameter(QStringLiteral("period"), QStringLiteral("周期"), 20, 2, 500),
                        choiceParameter(QStringLiteral("output"), QStringLiteral("输出序列"),
                                        QStringLiteral("middle"),
                                        {QStringLiteral("upper"), QStringLiteral("middle"), QStringLiteral("lower")},
                                        {QStringLiteral("上轨"), QStringLiteral("中轨"), QStringLiteral("下轨")})}}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("VWAP")},
                    {QStringLiteral("displayName"), QStringLiteral("会话成交量加权均价")},
                    {QStringLiteral("parameters"), QVariantList{}}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("PP_FLOW")},
                    {QStringLiteral("displayName"), QStringLiteral("价量趋势流")},
                    {QStringLiteral("parameters"), QVariantList{}}},
    };

    return {{QStringLiteral("schemaVersion"), 3},
            {QStringLiteral("stage"), QStringLiteral("backtest_ready")},
            {QStringLiteral("executionScope"), QStringLiteral("backtest_only")},
            {QStringLiteral("executable"), true},
            {QStringLiteral("markets"), QStringList{QStringLiteral("A股")}},
            {QStringLiteral("indicators"), QStringList{QStringLiteral("SMA"), QStringLiteral("EMA"),
                QStringLiteral("RSI"), QStringLiteral("MACD"), QStringLiteral("BOLLINGER"),
                QStringLiteral("ATR"), QStringLiteral("VOLUME_SMA"), QStringLiteral("ROC"),
                QStringLiteral("VWAP"), QStringLiteral("PP_FLOW"), QStringLiteral("WMA"),
                QStringLiteral("BIAS"), QStringLiteral("CCI"), QStringLiteral("WR"),
                QStringLiteral("DONCHIAN"), QStringLiteral("OBV"), QStringLiteral("MFI"),
                QStringLiteral("KDJ")}},
            {QStringLiteral("indicatorSchemas"), indicatorSchemas},
            {QStringLiteral("crossIndicators"), QStringList{QStringLiteral("RANK"), QStringLiteral("ZSCORE"),
                QStringLiteral("PERCENTILE"), QStringLiteral("MEAN"), QStringLiteral("MEDIAN"),
                QStringLiteral("PEER")}},
            {QStringLiteral("comparisonOperators"), QStringList{QStringLiteral("gt"), QStringLiteral("gte"),
                QStringLiteral("lt"), QStringLiteral("lte"), QStringLiteral("eq"),
                QStringLiteral("crossesAbove"), QStringLiteral("crossesBelow")}},
            {QStringLiteral("booleanOperators"), QStringList{QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("not")}},
            {QStringLiteral("fields"), QStringList{QStringLiteral("open"), QStringLiteral("high"),
                QStringLiteral("low"), QStringLiteral("close"), QStringLiteral("volume")}},
            {QStringLiteral("maximumDepth"), kMaximumExpressionDepth},
            {QStringLiteral("maximumNodes"), kMaximumExpressionNodes}};
}

} // namespace pp
