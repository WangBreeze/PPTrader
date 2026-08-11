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
        QStringLiteral("entry"), QStringLiteral("exit")
    };
    if (!hasOnlyKeys(input, allowedTopLevel, QStringLiteral("策略定义"), error)) return false;
    if (input.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
        return fail(error, QStringLiteral("仅支持 schemaVersion=1"));
    }

    CustomStrategyDefinition result;
    result.schemaVersion = 1;
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
    if (result.executionScope != QStringLiteral("backtest_only")) {
        return fail(error, QStringLiteral("自定义策略必须声明 executionScope=backtest_only"));
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
        if (!hasOnlyKeys(position,
                         {QStringLiteral("type"), QStringLiteral("percent")},
                         QStringLiteral("position"), error)
            || position.value(QStringLiteral("type")).toString()
                != QStringLiteral("fixedPercent")) {
            return fail(error, QStringLiteral("position.type 仅支持 fixedPercent"));
        }
        double percent = 0.0;
        if (!finiteNumber(position.value(QStringLiteral("percent")), &percent)
            || percent < 0.01 || percent > 1.0) {
            return fail(error, QStringLiteral("position.percent 必须在 0.01–1.0 之间"));
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
                          QStringLiteral("tPlusOne")},
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
    if (!validateCondition(input.value(QStringLiteral("entry")).toObject(), parameters,
                           QStringLiteral("entry"), 1, &nodeCount, error)
        || !validateCondition(input.value(QStringLiteral("exit")).toObject(), parameters,
                              QStringLiteral("exit"), 1, &nodeCount, error)) {
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
                                                const QString &path, int depth,
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
                                      depth + 1, nodeCount, error)) {
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
                                 path + QStringLiteral(".condition"), depth + 1, nodeCount, error);
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
                           path + QStringLiteral(".left"), depth + 1, nodeCount, error)
        && validateOperand(condition.value(QStringLiteral("right")).toObject(), parameters,
                           path + QStringLiteral(".right"), depth + 1, nodeCount, error);
}

bool CustomStrategyValidator::validateOperand(const QJsonObject &operand,
                                              const ParameterMap &parameters,
                                              const QString &path, int depth,
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
    if (!operand.contains(QStringLiteral("indicator"))) {
        return fail(error, QStringLiteral("%1 不是受支持的操作数").arg(path));
    }

    const QString indicator = operand.value(QStringLiteral("indicator")).toString().toUpper();
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

    if (indicator == QStringLiteral("SMA") || indicator == QStringLiteral("EMA")
        || indicator == QStringLiteral("RSI") || indicator == QStringLiteral("ATR")
        || indicator == QStringLiteral("VOLUME_SMA")) {
        if (!hasOnlyKeys(operand, {QStringLiteral("indicator"), QStringLiteral("period")}, path, error)
            || !validateNumericOption(QStringLiteral("period"), 2.0, 500.0)) {
            return fail(error, QStringLiteral("%1.%2 的 period 必须在 2–500 之间").arg(path, indicator));
        }
        return true;
    }
    if (indicator == QStringLiteral("MACD")) {
        const auto fastDefault = defaultNumericOption(QStringLiteral("fastPeriod"));
        const auto slowDefault = defaultNumericOption(QStringLiteral("slowPeriod"));
        if (!hasOnlyKeys(operand, {QStringLiteral("indicator"), QStringLiteral("fastPeriod"),
                                   QStringLiteral("slowPeriod"), QStringLiteral("signalPeriod"),
                                   QStringLiteral("output")}, path, error)
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
        if (!hasOnlyKeys(operand, {QStringLiteral("indicator"), QStringLiteral("period"),
                                   QStringLiteral("standardDeviations"), QStringLiteral("output")}, path, error)
            || !validateNumericOption(QStringLiteral("period"), 2.0, 500.0)
            || !validateNumericOption(QStringLiteral("standardDeviations"), 0.1, 10.0)
            || !QSet<QString>{QStringLiteral("upper"), QStringLiteral("middle"), QStringLiteral("lower")}
                    .contains(operand.value(QStringLiteral("output")).toString())) {
            return fail(error, QStringLiteral("%1 的 BOLLINGER 配置无效").arg(path));
        }
        return true;
    }
    return fail(error, QStringLiteral("%1 使用了未知指标：%2").arg(path, indicator));
}

QVariantMap CustomStrategyValidator::schemaDescriptor() const
{
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("stage"), QStringLiteral("backtest_ready")},
            {QStringLiteral("executionScope"), QStringLiteral("backtest_only")},
            {QStringLiteral("executable"), true},
            {QStringLiteral("markets"), QStringList{QStringLiteral("A股")}},
            {QStringLiteral("indicators"), QStringList{QStringLiteral("SMA"), QStringLiteral("EMA"),
                QStringLiteral("RSI"), QStringLiteral("MACD"), QStringLiteral("BOLLINGER"),
                QStringLiteral("ATR"), QStringLiteral("VOLUME_SMA")}},
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
