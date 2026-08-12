#include "strategy/CustomStrategyCompiler.h"

#include "strategy/CustomStrategyValidator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>

#include <algorithm>
#include <cmath>

namespace pp {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

class PlanBuilder {
public:
    PlanBuilder(const QVariantMap &parameters, CompiledCustomStrategy *plan,
                bool v2, QString *error)
        : m_parameters(parameters), m_plan(plan), m_v2(v2), m_error(error)
    {
    }

    int compileCondition(const QJsonObject &object)
    {
        const QString op = object.value(QStringLiteral("op")).toString();
        CompiledCustomCondition condition;
        if (op == QStringLiteral("and") || op == QStringLiteral("or")) {
            condition.op = op == QStringLiteral("and") ? CustomConditionOp::And : CustomConditionOp::Or;
            const int row = appendCondition(condition);
            for (const QJsonValue &child : object.value(QStringLiteral("conditions")).toArray()) {
                const int childRow = compileCondition(child.toObject());
                if (childRow < 0) return -1;
                m_plan->conditions[row].children.append(childRow);
            }
            return row;
        }
        if (op == QStringLiteral("not")) {
            condition.op = CustomConditionOp::Not;
            const int row = appendCondition(condition);
            const int child = compileCondition(object.value(QStringLiteral("condition")).toObject());
            if (child < 0) return -1;
            m_plan->conditions[row].children.append(child);
            return row;
        }

        static const QHash<QString, CustomConditionOp> comparisons {
            {QStringLiteral("gt"), CustomConditionOp::Greater},
            {QStringLiteral("gte"), CustomConditionOp::GreaterOrEqual},
            {QStringLiteral("lt"), CustomConditionOp::Less},
            {QStringLiteral("lte"), CustomConditionOp::LessOrEqual},
            {QStringLiteral("eq"), CustomConditionOp::Equal},
            {QStringLiteral("crossesAbove"), CustomConditionOp::CrossesAbove},
            {QStringLiteral("crossesBelow"), CustomConditionOp::CrossesBelow}
        };
        if (!comparisons.contains(op)) {
            fail(m_error, QStringLiteral("编译时遇到未知条件：%1").arg(op));
            return -1;
        }
        condition.op = comparisons.value(op);
        condition.leftOperand = compileOperand(object.value(QStringLiteral("left")).toObject());
        condition.rightOperand = compileOperand(object.value(QStringLiteral("right")).toObject());
        if (condition.leftOperand < 0 || condition.rightOperand < 0) return -1;
        if (condition.op == CustomConditionOp::CrossesAbove
            || condition.op == CustomConditionOp::CrossesBelow) {
            m_hasCross = true;
        }
        return appendCondition(condition);
    }

    void preRegisterAliases(const QJsonObject &condition)
    {
        const QString op = condition.value(QStringLiteral("op")).toString();
        if (op == QStringLiteral("and") || op == QStringLiteral("or")) {
            const QJsonArray children = condition.value(QStringLiteral("conditions")).toArray();
            for (const QJsonValue &child : children) {
                if (child.isObject()) preRegisterAliases(child.toObject());
            }
            return;
        }
        if (op == QStringLiteral("not")) {
            if (condition.value(QStringLiteral("condition")).isObject()) {
                preRegisterAliases(condition.value(QStringLiteral("condition")).toObject());
            }
            return;
        }
        registerOperandAlias(condition.value(QStringLiteral("left")).toObject());
        registerOperandAlias(condition.value(QStringLiteral("right")).toObject());
    }

    void preRegisterSeries(const QJsonArray &series)
    {
        for (const QJsonValue &item : series) {
            if (item.isObject()) registerOperandAlias(item.toObject());
        }
    }

    bool hasCross() const { return m_hasCross; }

private:
    void registerOperandAlias(const QJsonObject &operand)
    {
        if (m_v2 && operand.contains(QStringLiteral("indicator"))
            && operand.contains(QStringLiteral("alias"))) {
            compileIndicator(operand);
        }
    }

    int appendCondition(const CompiledCustomCondition &condition)
    {
        const int row = m_plan->conditions.size();
        m_plan->conditions.append(condition);
        return row;
    }

    double resolveNumber(const QJsonValue &value, bool integer, bool *ok)
    {
        double result = 0.0;
        if (value.isDouble()) {
            result = value.toDouble();
        } else if (value.isObject()) {
            const QString key = value.toObject().value(QStringLiteral("parameter")).toString();
            bool converted = false;
            result = m_parameters.value(key).toDouble(&converted);
            if (!converted) {
                *ok = false;
                return 0.0;
            }
        } else {
            *ok = false;
            return 0.0;
        }
        *ok = std::isfinite(result) && (!integer || std::floor(result) == result);
        return result;
    }

    int compileOperand(const QJsonObject &object)
    {
        CompiledCustomOperand operand;
        if (object.contains(QStringLiteral("value"))) {
            operand.kind = CustomOperandKind::Constant;
            operand.constant = object.value(QStringLiteral("value")).toDouble();
        } else if (object.contains(QStringLiteral("parameter"))) {
            operand.kind = CustomOperandKind::Constant;
            operand.constant = m_parameters.value(
                object.value(QStringLiteral("parameter")).toString()).toDouble();
        } else if (object.contains(QStringLiteral("field"))) {
            static const QHash<QString, CustomOperandKind> fields {
                {QStringLiteral("open"), CustomOperandKind::Open},
                {QStringLiteral("high"), CustomOperandKind::High},
                {QStringLiteral("low"), CustomOperandKind::Low},
                {QStringLiteral("close"), CustomOperandKind::Close},
                {QStringLiteral("volume"), CustomOperandKind::Volume}
            };
            operand.kind = fields.value(object.value(QStringLiteral("field")).toString());
        } else if (object.contains(QStringLiteral("series"))) {
            if (!m_v2) {
                fail(m_error, QStringLiteral("编译时遇到 schemaVersion=1 的序列引用"));
                return -1;
            }
            const QString alias = object.value(QStringLiteral("series")).toString();
            const auto row = m_aliasRows.constFind(alias);
            if (row == m_aliasRows.constEnd()) {
                fail(m_error, QStringLiteral("编译时遇到未定义的指标别名：%1").arg(alias));
                return -1;
            }
            operand.kind = CustomOperandKind::Indicator;
            operand.indicatorIndex = row.value();
        } else {
            const int indicatorIndex = compileIndicator(object);
            if (indicatorIndex < 0) return -1;
            operand.kind = CustomOperandKind::Indicator;
            operand.indicatorIndex = indicatorIndex;
        }
        const int row = m_plan->operands.size();
        m_plan->operands.append(operand);
        return row;
    }

    int compileIndicator(const QJsonObject &object)
    {
        CompiledCustomIndicator indicator;
        const QString name = object.value(QStringLiteral("indicator")).toString().toUpper();
        if (m_v2 && object.contains(QStringLiteral("alias"))) {
            indicator.alias = object.value(QStringLiteral("alias")).toString();
        }
        bool ok = true;
        if (name == QStringLiteral("SMA") || name == QStringLiteral("EMA")
            || name == QStringLiteral("RSI") || name == QStringLiteral("ATR")
            || name == QStringLiteral("VOLUME_SMA")
            || (m_v2 && name == QStringLiteral("ROC"))) {
            if (name == QStringLiteral("SMA")) indicator.kind = CustomIndicatorKind::Sma;
            else if (name == QStringLiteral("EMA")) indicator.kind = CustomIndicatorKind::Ema;
            else if (name == QStringLiteral("RSI")) indicator.kind = CustomIndicatorKind::Rsi;
            else if (name == QStringLiteral("ATR")) indicator.kind = CustomIndicatorKind::Atr;
            else if (name == QStringLiteral("ROC")) indicator.kind = CustomIndicatorKind::Roc;
            else indicator.kind = CustomIndicatorKind::VolumeSma;
            indicator.period = static_cast<int>(resolveNumber(object.value(QStringLiteral("period")), true, &ok));
        } else if (name == QStringLiteral("MACD")) {
            indicator.kind = CustomIndicatorKind::Macd;
            indicator.fastPeriod = static_cast<int>(resolveNumber(object.value(QStringLiteral("fastPeriod")), true, &ok));
            bool optionOk = true;
            indicator.slowPeriod = static_cast<int>(resolveNumber(object.value(QStringLiteral("slowPeriod")), true, &optionOk));
            ok = ok && optionOk;
            indicator.signalPeriod = static_cast<int>(resolveNumber(object.value(QStringLiteral("signalPeriod")), true, &optionOk));
            ok = ok && optionOk;
            indicator.output = object.value(QStringLiteral("output")).toString();
            ok = ok && indicator.fastPeriod < indicator.slowPeriod;
        } else if (name == QStringLiteral("BOLLINGER")) {
            indicator.kind = CustomIndicatorKind::Bollinger;
            indicator.period = static_cast<int>(resolveNumber(object.value(QStringLiteral("period")), true, &ok));
            bool optionOk = true;
            indicator.standardDeviations = resolveNumber(
                object.value(QStringLiteral("standardDeviations")), false, &optionOk);
            ok = ok && optionOk;
            indicator.output = object.value(QStringLiteral("output")).toString();
        } else if (m_v2 && (name == QStringLiteral("VWAP")
                            || name == QStringLiteral("PP_FLOW"))) {
            indicator.kind = name == QStringLiteral("VWAP")
                ? CustomIndicatorKind::Vwap : CustomIndicatorKind::PpFlow;
        } else {
            ok = false;
        }
        if (!ok) {
            fail(m_error, QStringLiteral("指标参数编译失败：%1").arg(name));
            return -1;
        }
        const QString key = indicator.cacheKey();
        const auto existing = m_indicatorRows.constFind(key);
        if (existing != m_indicatorRows.constEnd()) {
            if (!indicator.alias.isEmpty()) m_aliasRows.insert(indicator.alias, existing.value());
            return existing.value();
        }
        const int row = m_plan->indicators.size();
        m_plan->indicators.append(indicator);
        m_indicatorRows.insert(key, row);
        if (!indicator.alias.isEmpty()) m_aliasRows.insert(indicator.alias, row);
        return row;
    }

    const QVariantMap &m_parameters;
    CompiledCustomStrategy *m_plan = nullptr;
    bool m_v2 = false;
    QString *m_error = nullptr;
    QHash<QString, int> m_indicatorRows;
    QHash<QString, int> m_aliasRows;
    bool m_hasCross = false;
};

} // namespace

int CompiledCustomIndicator::minimumBars() const
{
    switch (kind) {
    case CustomIndicatorKind::Rsi: return period + 1;
    case CustomIndicatorKind::Macd:
        return output == QStringLiteral("macd") ? slowPeriod : slowPeriod + signalPeriod - 1;
    case CustomIndicatorKind::Roc:
        return period + 1;
    case CustomIndicatorKind::Sma:
    case CustomIndicatorKind::Ema:
    case CustomIndicatorKind::Bollinger:
    case CustomIndicatorKind::Atr:
    case CustomIndicatorKind::VolumeSma:
        return period;
    case CustomIndicatorKind::Vwap:
        return 1;
    case CustomIndicatorKind::PpFlow:
        return 2;
    }
    return 1;
}

QString CompiledCustomIndicator::cacheKey() const
{
    return QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
        .arg(static_cast<int>(kind)).arg(period).arg(fastPeriod).arg(slowPeriod)
        .arg(signalPeriod).arg(standardDeviations, 0, 'g', 16).arg(output);
}

QVariantMap CompiledCustomRules::toVariantMap() const
{
    return {{QStringLiteral("positionFraction"), positionFraction},
            {QStringLiteral("stopLossPercent"), stopLossPercent},
            {QStringLiteral("takeProfitPercent"), takeProfitPercent},
            {QStringLiteral("maximumHoldingBars"), maximumHoldingBars},
            {QStringLiteral("tPlusOne"), tPlusOne}};
}

std::optional<CompiledCustomStrategy> CustomStrategyCompiler::compile(
    const CustomStrategyDefinition &definition, const QVariantMap &runtimeParameters,
    QString *error) const
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition validated;
    if (!validator.validate(definition.document, &validated, error)) return std::nullopt;

    QVariantMap normalized;
    QSet<QString> knownKeys;
    for (const QJsonValue &value : validated.document.value(QStringLiteral("parameters")).toArray()) {
        const QJsonObject spec = value.toObject();
        const QString key = spec.value(QStringLiteral("key")).toString();
        knownKeys.insert(key);
        const QVariant input = runtimeParameters.value(
            key, spec.value(QStringLiteral("defaultValue")).toVariant());
        bool ok = false;
        const double number = input.toDouble(&ok);
        const double minimum = spec.value(QStringLiteral("minimumValue")).toDouble();
        const double maximum = spec.value(QStringLiteral("maximumValue")).toDouble();
        const bool integer = spec.value(QStringLiteral("type")).toString() == QStringLiteral("integer");
        if (!ok || !std::isfinite(number) || number < minimum || number > maximum
            || (integer && std::floor(number) != number)) {
            fail(error, QStringLiteral("运行参数无效：%1").arg(key));
            return std::nullopt;
        }
        normalized.insert(key, integer ? QVariant(static_cast<int>(number)) : QVariant(number));
    }
    for (auto it = runtimeParameters.constBegin(); it != runtimeParameters.constEnd(); ++it) {
        if (!knownKeys.contains(it.key())) {
            fail(error, QStringLiteral("运行参数不存在：%1").arg(it.key()));
            return std::nullopt;
        }
    }

    CompiledCustomStrategy plan;
    plan.strategyId = validated.strategyId;
    plan.name = validated.name;
    plan.version = validated.version;
    plan.normalizedParameters = normalized;
    const QJsonObject position = validated.document.value(QStringLiteral("position")).toObject();
    if (!position.isEmpty()) {
        plan.rules.positionFraction = position.value(QStringLiteral("percent")).toDouble(1.0);
    }
    const QJsonObject risk = validated.document.value(QStringLiteral("risk")).toObject();
    plan.rules.stopLossPercent = risk.value(QStringLiteral("stopLossPercent")).toDouble(0.0);
    plan.rules.takeProfitPercent = risk.value(QStringLiteral("takeProfitPercent")).toDouble(0.0);
    plan.rules.maximumHoldingBars = risk.value(QStringLiteral("maximumHoldingBars")).toInt(0);
    plan.rules.tPlusOne = risk.value(QStringLiteral("tPlusOne")).toBool(true);
    PlanBuilder builder(normalized, &plan, validated.schemaVersion == 2, error);
    if (validated.schemaVersion == 2) {
        builder.preRegisterSeries(
            validated.document.value(QStringLiteral("series")).toArray());
        builder.preRegisterAliases(
            validated.document.value(QStringLiteral("entry")).toObject());
        builder.preRegisterAliases(
            validated.document.value(QStringLiteral("exit")).toObject());
    }
    plan.entryCondition = builder.compileCondition(
        validated.document.value(QStringLiteral("entry")).toObject());
    plan.exitCondition = builder.compileCondition(
        validated.document.value(QStringLiteral("exit")).toObject());
    if (plan.entryCondition < 0 || plan.exitCondition < 0) return std::nullopt;

    int warmup = 1;
    for (const CompiledCustomIndicator &indicator : plan.indicators) {
        warmup = std::max(warmup, indicator.minimumBars());
    }
    plan.minimumWarmupBars = warmup + (builder.hasCross() ? 1 : 0);
    if (error) error->clear();
    return plan;
}

} // namespace pp
