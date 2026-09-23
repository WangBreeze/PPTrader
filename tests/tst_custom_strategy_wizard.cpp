#include "strategy/CustomStrategyValidator.h"

#include <QFile>
#include <QJsonDocument>
#include <QJSEngine>
#include <QTest>

namespace pp {

class CustomStrategyWizardTest final : public QObject {
    Q_OBJECT

private slots:
    void buildsValidatedSingleInstrumentDraftWithoutEmbeddingDatasetIdentity();
    void preservesRequestedImmutableVersion();
    void migratesLegacyDraftToCurrentPublicShape();
    void migratesLegacyConditionsToExecutableOperandKinds();
    void validatesSampleWindowBeforeControllerRequest();
    void buildsIndicatorRowsFromCppSchema();
    void preservesCompleteIndicatorRolesWithoutSerializingUnusedParameters();
    void rejectsIndicatorParameterViolationsBeforePreview();
    void fillsMissingIndicatorParametersDuringDraftMigration();
    void blocksInvalidOrDuplicateIndicatorRowsAtStepGate();
    void migratesFlatConditionsAndPreservesNestedBooleanExpressions();
    void editsNestedConditionTreeByStablePaths();
    void removingIndicatorPrunesNestedReferences();
    void conditionRowsExposeCompleteDelegateRoleShape();
    void buildsValidatedCrossSectionalDraftFromLocalCatalog();
    void rejectsCrossSectionalMixedPeriodAndInsufficientBars();
};

static bool loadContractEngine(QJSEngine *engine, const QString &relativePath)
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR) + relativePath);
    if (!engine || !contract.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QJSValue result = engine->evaluate(QString::fromUtf8(contract.readAll()),
                                             contract.fileName());
    return !result.isError();
}

static QVariantMap crossDataset(const QString &symbol, int periodSeconds,
                                int candleCount, const QString &version)
{
    return {{QStringLiteral("symbol"), symbol},
            {QStringLiteral("name"), QStringLiteral("测试标的")},
            {QStringLiteral("frequency"), periodSeconds == 60
                ? QStringLiteral("1m") : QStringLiteral("5m")},
            {QStringLiteral("periodSeconds"), periodSeconds},
            {QStringLiteral("candleCount"), candleCount},
            {QStringLiteral("source"), QStringLiteral("raw_tick_cache_v1")},
            {QStringLiteral("datasetVersion"), version},
            {QStringLiteral("datasetStaleDayCount"), 0},
            {QStringLiteral("backtestEligible"), true}};
}

static QVariantMap crossDraft(const QStringList &symbols, int periodSeconds,
                              int barCount)
{
    return {{QStringLiteral("strategyId"), QStringLiteral("custom_cross_qml")},
            {QStringLiteral("version"), 1},
            {QStringLiteral("name"), QStringLiteral("横截面向导测试")},
            {QStringLiteral("periodSeconds"), periodSeconds},
            {QStringLiteral("barCount"), barCount},
            {QStringLiteral("symbols"), symbols},
            {QStringLiteral("indicators"), QVariantList{QVariantMap{
                {QStringLiteral("alias"), QStringLiteral("mom_rank")},
                {QStringLiteral("indicatorName"), QStringLiteral("RANK")},
                {QStringLiteral("sourceKind"), QStringLiteral("ROC")},
                {QStringLiteral("sourceField"), QStringLiteral("close")},
                {QStringLiteral("periodValue"), 20},
                {QStringLiteral("peerSymbol"), QString{}},
                {QStringLiteral("ascendingValue"), false}}}},
            {QStringLiteral("rankBy"), QStringLiteral("mom_rank")},
            {QStringLiteral("topN"), 2},
            {QStringLiteral("selectionAscending"), false},
            {QStringLiteral("entryAlias"), QStringLiteral("mom_rank")},
            {QStringLiteral("entryOp"), QStringLiteral("gte")},
            {QStringLiteral("entryValue"), 0.8},
            {QStringLiteral("exitAlias"), QStringLiteral("mom_rank")},
            {QStringLiteral("exitOp"), QStringLiteral("lt")},
            {QStringLiteral("exitValue"), 0.0}};
}

void CustomStrategyWizardTest::buildsValidatedCrossSectionalDraftFromLocalCatalog()
{
    QJSEngine engine;
    QVERIFY2(loadContractEngine(
        &engine, QStringLiteral("/qml/components/CrossSectionalBacktestContract.js")),
        "多股向导契约脚本应可加载");
    const QJSValue validate = engine.globalObject().property(QStringLiteral("validateDraft"));
    const QJSValue build = engine.globalObject().property(QStringLiteral("buildDefinition"));
    QVERIFY(validate.isCallable());
    QVERIFY(build.isCallable());

    const QVariantList catalog{
        crossDataset(QStringLiteral("600001"), 60, 500, QString(64, QLatin1Char('a'))),
        crossDataset(QStringLiteral("600002"), 60, 480, QString(64, QLatin1Char('b'))),
        crossDataset(QStringLiteral("600003"), 60, 450, QString(64, QLatin1Char('c')))};
    const QVariantMap draft = crossDraft(
        {QStringLiteral("600001"), QStringLiteral("600002"), QStringLiteral("600003")},
        60, 400);
    const QJSValue validation = validate.call({engine.toScriptValue(catalog),
        engine.toScriptValue(draft),
        engine.toScriptValue(QStringList{QStringLiteral("RANK"), QStringLiteral("ZSCORE")}),
        engine.toScriptValue(QStringList{QStringLiteral("open"), QStringLiteral("high"),
                                         QStringLiteral("low"), QStringLiteral("close"),
                                         QStringLiteral("volume")})});
    QVERIFY2(validation.property(QStringLiteral("valid")).toBool(),
             qPrintable(validation.property(QStringLiteral("error")).toString()));

    const QVariantMap document = build.call({engine.toScriptValue(draft),
        QJSValue(QStringLiteral("2026-08-20T08:00:00.000Z"))}).toVariant().toMap();
    QCOMPARE(document.value(QStringLiteral("schemaVersion")).toInt(), 3);
    const QVariantList universe = document.value(QStringLiteral("universe")).toMap()
        .value(QStringLiteral("symbols")).toList();
    QCOMPARE(universe.size(), 3);
    QCOMPARE(document.value(QStringLiteral("position")).toMap()
                 .value(QStringLiteral("type")).toString(), QStringLiteral("equalWeight"));

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    const QString json = QString::fromUtf8(
        QJsonDocument::fromVariant(document).toJson(QJsonDocument::Compact));
    QVERIFY2(validator.parseAndValidate(json, &definition, &error), qPrintable(error));
    QCOMPARE(definition.schemaVersion, 3);
}

void CustomStrategyWizardTest::rejectsCrossSectionalMixedPeriodAndInsufficientBars()
{
    QJSEngine engine;
    QVERIFY(loadContractEngine(
        &engine, QStringLiteral("/qml/components/CrossSectionalBacktestContract.js")));
    const QJSValue validate = engine.globalObject().property(QStringLiteral("validateDraft"));
    const QStringList crossNames{QStringLiteral("RANK")};
    const QStringList fields{QStringLiteral("close")};
    const QVariantList catalog{
        crossDataset(QStringLiteral("600001"), 60, 500, QString(64, QLatin1Char('a'))),
        crossDataset(QStringLiteral("600002"), 300, 500, QString(64, QLatin1Char('b')))};

    QVariantMap draft = crossDraft(
        {QStringLiteral("600001"), QStringLiteral("600002")}, 60, 200);
    QJSValue result = validate.call({engine.toScriptValue(catalog),
        engine.toScriptValue(draft), engine.toScriptValue(crossNames),
        engine.toScriptValue(fields)});
    QVERIFY(!result.property(QStringLiteral("valid")).toBool());
    QVERIFY(result.property(QStringLiteral("error")).toString().contains(
        QStringLiteral("同周期")));

    QVariantList samePeriod{
        crossDataset(QStringLiteral("600001"), 60, 500, QString(64, QLatin1Char('a'))),
        crossDataset(QStringLiteral("600002"), 60, 100, QString(64, QLatin1Char('b')))};
    result = validate.call({engine.toScriptValue(samePeriod),
        engine.toScriptValue(draft), engine.toScriptValue(crossNames),
        engine.toScriptValue(fields)});
    QVERIFY(!result.property(QStringLiteral("valid")).toBool());
    QVERIFY(result.property(QStringLiteral("error")).toString().contains(
        QStringLiteral("K 线不足")));
}

void CustomStrategyWizardTest::conditionRowsExposeCompleteDelegateRoleShape()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY(contract.open(QIODevice::ReadOnly | QIODevice::Text));
    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue rowsFunction = engine.globalObject().property(
        QStringLiteral("conditionEditorRows"));
    QVERIFY(rowsFunction.isCallable());
    const QVariantMap comparison{
        {QStringLiteral("op"), QStringLiteral("gt")},
        {QStringLiteral("leftOperand"), QStringLiteral("close")},
        {QStringLiteral("leftKind"), QStringLiteral("field")},
        {QStringLiteral("rightOperand"), QString{}},
        {QStringLiteral("rightKind"), QStringLiteral("value")},
        {QStringLiteral("rightConstant"), 10.0}};
    const QVariantMap tree{
        {QStringLiteral("op"), QStringLiteral("and")},
        {QStringLiteral("conditions"), QVariantList{comparison}}};
    const QVariantList rows = rowsFunction.call({engine.toScriptValue(tree)})
                                  .toVariant().toList();
    QCOMPARE(rows.size(), 2);
    const QStringList roles{QStringLiteral("path"), QStringLiteral("nodeType"),
                            QStringLiteral("depth"), QStringLiteral("negated"),
                            QStringLiteral("groupOp"), QStringLiteral("childCount"),
                            QStringLiteral("leftOperand"), QStringLiteral("leftKind"),
                            QStringLiteral("op"), QStringLiteral("rightOperand"),
                            QStringLiteral("rightKind"), QStringLiteral("rightConstant")};
    for (const QVariant &rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        for (const QString &role : roles)
            QVERIFY2(row.contains(role), qPrintable(QStringLiteral("%1 缺少 %2")
                                                    .arg(row.value(QStringLiteral("nodeType")).toString(), role)));
    }
}

void CustomStrategyWizardTest::buildsValidatedSingleInstrumentDraftWithoutEmbeddingDatasetIdentity()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));

    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue builder = engine.globalObject().property(QStringLiteral("buildDefinition"));
    QVERIFY(builder.isCallable());

    const QVariantMap metadata {
        {QStringLiteral("strategyId"), QStringLiteral("custom_momentum_draft")},
        {QStringLiteral("name"), QStringLiteral("动量过滤策略")},
        {QStringLiteral("description"), QStringLiteral("仅使用本地历史数据验证")},
        {QStringLiteral("author"), QStringLiteral("本地用户")},
        {QStringLiteral("selectedSymbol"), QStringLiteral("600519")},
        {QStringLiteral("selectedFrequency"), QStringLiteral("5m")},
    };
    const QVariantList series {
        QVariantMap{{QStringLiteral("alias"), QStringLiteral("momentum")},
                    {QStringLiteral("indicator"), QStringLiteral("ROC")},
                    {QStringLiteral("period"), 14}},
    };
    const QVariantList entry {
        QVariantMap{{QStringLiteral("op"), QStringLiteral("gt")},
                    {QStringLiteral("left"), QVariantMap{
                         {QStringLiteral("series"), QStringLiteral("momentum")}}},
                    {QStringLiteral("right"), QVariantMap{
                         {QStringLiteral("value"), 0.8}}}},
    };
    const QVariantList exit {
        QVariantMap{{QStringLiteral("op"), QStringLiteral("lt")},
                    {QStringLiteral("left"), QVariantMap{
                         {QStringLiteral("series"), QStringLiteral("momentum")}}},
                    {QStringLiteral("right"), QVariantMap{
                         {QStringLiteral("value"), 0.0}}}},
    };
    const QVariantMap rules {
        {QStringLiteral("position"), QVariantMap{
             {QStringLiteral("type"), QStringLiteral("fixedPercent")},
             {QStringLiteral("percent"), 1.0}}},
        {QStringLiteral("risk"), QVariantMap{
             {QStringLiteral("tPlusOne"), true},
             {QStringLiteral("maxPositionPercent"), 30.0}}},
        {QStringLiteral("signal"), QVariantMap{
             {QStringLiteral("priceMode"), QStringLiteral("triggerClose")}}},
    };

    const QJSValue result = builder.call({
        engine.toScriptValue(metadata), engine.toScriptValue(series),
        engine.toScriptValue(entry), engine.toScriptValue(exit),
        engine.toScriptValue(rules),
        QJSValue(QStringLiteral("2026-08-18T08:00:00.000Z"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    const QVariantMap document = result.toVariant().toMap();
    QCOMPARE(document.value(QStringLiteral("schemaVersion")).toInt(), 2);
    QCOMPARE(document.value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("custom_momentum_draft"));
    QCOMPARE(document.value(QStringLiteral("name")).toString(),
             QStringLiteral("动量过滤策略"));
    QCOMPARE(document.value(QStringLiteral("description")).toString(),
             QStringLiteral("仅使用本地历史数据验证"));
    QCOMPARE(document.value(QStringLiteral("author")).toString(),
             QStringLiteral("本地用户"));
    QCOMPARE(document.value(QStringLiteral("market")).toString(), QStringLiteral("A股"));
    QCOMPARE(document.value(QStringLiteral("executionScope")).toString(),
             QStringLiteral("backtest_only"));
    QVERIFY(!document.contains(QStringLiteral("selectedSymbol")));
    QVERIFY(!document.contains(QStringLiteral("selectedFrequency")));
    QVERIFY(!document.contains(QStringLiteral("symbol")));
    QVERIFY(!document.contains(QStringLiteral("period")));

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    const QString json = QString::fromUtf8(
        QJsonDocument::fromVariant(document).toJson(QJsonDocument::Compact));
    QVERIFY2(validator.parseAndValidate(json, &definition, &error), qPrintable(error));
    QCOMPARE(definition.strategyId, QStringLiteral("custom_momentum_draft"));
}

void CustomStrategyWizardTest::preservesRequestedImmutableVersion()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));

    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue builder = engine.globalObject().property(QStringLiteral("buildDefinition"));
    QVERIFY(builder.isCallable());

    const QVariantMap metadata {
        {QStringLiteral("strategyId"), QStringLiteral("custom_versioned_draft")},
        {QStringLiteral("version"), 4},
        {QStringLiteral("name"), QStringLiteral("版本化策略")},
        {QStringLiteral("description"), QStringLiteral("不可变版本测试")},
        {QStringLiteral("author"), QStringLiteral("Qt Test")},
    };
    const QVariantMap rules {
        {QStringLiteral("position"), QVariantMap{
             {QStringLiteral("type"), QStringLiteral("fixedPercent")},
             {QStringLiteral("percent"), 1.0}}},
        {QStringLiteral("risk"), QVariantMap{{QStringLiteral("tPlusOne"), true}}},
        {QStringLiteral("signal"), QVariantMap{
             {QStringLiteral("priceMode"), QStringLiteral("triggerClose")}}},
    };
    const QJSValue result = builder.call({
        engine.toScriptValue(metadata), engine.newArray(), engine.newArray(),
        engine.newArray(), engine.toScriptValue(rules),
        QJSValue(QStringLiteral("2026-08-19T08:00:00.000Z"))});
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    QCOMPARE(result.toVariant().toMap().value(QStringLiteral("version")).toInt(), 4);
}

void CustomStrategyWizardTest::migratesLegacyDraftToCurrentPublicShape()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));

    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue migrate = engine.globalObject().property(QStringLiteral("migrateDraft"));
    QVERIFY(migrate.isCallable());

    const QVariantMap legacy {
        {QStringLiteral("name"), QStringLiteral("旧版草稿")},
        {QStringLiteral("activeStep"), 99},
        {QStringLiteral("series"), QVariantList{QVariantMap{
             {QStringLiteral("name"), QStringLiteral("SMA")},
             {QStringLiteral("alias"), QStringLiteral("fast")},
             {QStringLiteral("visible"), false}}}},
        {QStringLiteral("entry"), QVariantList{QVariantMap{
             {QStringLiteral("left"), QStringLiteral("close")},
             {QStringLiteral("right"), QStringLiteral("fast")}}}},
        {QStringLiteral("exit"), QVariantList{}},
    };
    const QVariantMap migrated = migrate.call({engine.toScriptValue(legacy)})
                                     .toVariant().toMap();
    QCOMPARE(migrated.value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("custom_wizard_draft"));
    QCOMPARE(migrated.value(QStringLiteral("author")).toString(),
             QStringLiteral("本地用户"));
    QCOMPARE(migrated.value(QStringLiteral("activeStep")).toInt(), 1);
    const QVariantMap series = migrated.value(QStringLiteral("series"))
                                   .toList().first().toMap();
    QCOMPARE(series.value(QStringLiteral("seriesVisible")).toBool(), false);
    QVERIFY(!series.contains(QStringLiteral("visible")));
    const QVariantMap entry = migrated.value(QStringLiteral("entry"))
                                  .toList().first().toMap();
    QCOMPARE(entry.value(QStringLiteral("leftOperand")).toString(),
             QStringLiteral("close"));
    QCOMPARE(entry.value(QStringLiteral("rightOperand")).toString(),
             QStringLiteral("fast"));
    QVERIFY(!entry.contains(QStringLiteral("left")));
    QVERIFY(!entry.contains(QStringLiteral("right")));
}

void CustomStrategyWizardTest::migratesLegacyConditionsToExecutableOperandKinds()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));

    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue migrate = engine.globalObject().property(QStringLiteral("migrateDraft"));
    QVERIFY(migrate.isCallable());

    const QVariantMap legacy {
        {QStringLiteral("series"), QVariantList{QVariantMap{
             {QStringLiteral("name"), QStringLiteral("SMA")},
             {QStringLiteral("alias"), QStringLiteral("fast")}}}},
        {QStringLiteral("entry"), QVariantList{QVariantMap{
             {QStringLiteral("left"), QStringLiteral("close")},
             {QStringLiteral("right"), QStringLiteral("fast")}}}},
        {QStringLiteral("exit"), QVariantList{QVariantMap{
             {QStringLiteral("left"), QStringLiteral("fast")},
             {QStringLiteral("right"), QStringLiteral("")},
             {QStringLiteral("rightConstant"), 0.0}}}},
    };

    const QVariantMap migrated = migrate.call({engine.toScriptValue(legacy)})
                                     .toVariant().toMap();
    const QVariantMap entry = migrated.value(QStringLiteral("entry"))
                                  .toList().first().toMap();
    QCOMPARE(entry.value(QStringLiteral("leftKind")).toString(),
             QStringLiteral("field"));
    QCOMPARE(entry.value(QStringLiteral("rightKind")).toString(),
             QStringLiteral("series"));
    QCOMPARE(entry.value(QStringLiteral("op")).toString(), QStringLiteral("gt"));

    const QVariantMap exit = migrated.value(QStringLiteral("exit"))
                                 .toList().first().toMap();
    QCOMPARE(exit.value(QStringLiteral("leftKind")).toString(),
             QStringLiteral("series"));
    QCOMPARE(exit.value(QStringLiteral("rightKind")).toString(),
             QStringLiteral("value"));
    QCOMPARE(exit.value(QStringLiteral("rightConstant")).toDouble(), 0.0);
}

void CustomStrategyWizardTest::validatesSampleWindowBeforeControllerRequest()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));

    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue validate = engine.globalObject().property(QStringLiteral("sampleWindowRequest"));
    QVERIFY(validate.isCallable());

    const QVariantMap valid = validate.call({QJSValue(QStringLiteral("2026-01-02")),
                                             QJSValue(QStringLiteral("2026-01-31"))})
                                  .toVariant().toMap();
    QCOMPARE(valid.value(QStringLiteral("valid")).toBool(), true);
    QCOMPARE(valid.value(QStringLiteral("from")).toString(), QStringLiteral("2026-01-02"));
    QCOMPARE(valid.value(QStringLiteral("to")).toString(), QStringLiteral("2026-01-31"));

    const QVariantMap invalidDate = validate.call({QJSValue(QStringLiteral("2026-02-30")),
                                                   QJSValue(QStringLiteral(""))})
                                        .toVariant().toMap();
    QCOMPARE(invalidDate.value(QStringLiteral("valid")).toBool(), false);

    const QVariantMap reversed = validate.call({QJSValue(QStringLiteral("2026-02-01")),
                                                QJSValue(QStringLiteral("2026-01-31"))})
                                     .toVariant().toMap();
    QCOMPARE(reversed.value(QStringLiteral("valid")).toBool(), false);
}

void CustomStrategyWizardTest::buildsIndicatorRowsFromCppSchema()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));

    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue createRow = engine.globalObject().property(QStringLiteral("newIndicatorRow"));
    const QJSValue serialize = engine.globalObject().property(QStringLiteral("seriesDocument"));
    QVERIFY(createRow.isCallable());
    QVERIFY(serialize.isCallable());

    CustomStrategyValidator validator;
    const QVariantMap schema = validator.schemaDescriptor();
    const QVariantMap macd = createRow.call({engine.toScriptValue(schema),
                                             QJSValue(QStringLiteral("MACD")),
                                             QJSValue(QStringLiteral("trend")),
                                             QJSValue(QStringLiteral("#21B6E9"))})
                                 .toVariant().toMap();
    QCOMPARE(macd.value(QStringLiteral("name")).toString(), QStringLiteral("MACD"));
    QCOMPARE(macd.value(QStringLiteral("fastPeriod")).toInt(), 12);
    QCOMPARE(macd.value(QStringLiteral("slowPeriod")).toInt(), 26);
    QCOMPARE(macd.value(QStringLiteral("signalPeriod")).toInt(), 9);
    QCOMPARE(macd.value(QStringLiteral("output")).toString(), QStringLiteral("histogram"));

    const QVariantMap document = serialize.call({engine.toScriptValue(schema),
                                                  engine.toScriptValue(macd)})
                                     .toVariant().toMap();
    QCOMPARE(document.value(QStringLiteral("alias")).toString(), QStringLiteral("trend"));
    QCOMPARE(document.value(QStringLiteral("indicator")).toString(), QStringLiteral("MACD"));
    QCOMPARE(document.value(QStringLiteral("fastPeriod")).toInt(), 12);
    QVERIFY(!document.contains(QStringLiteral("seriesVisible")));
    QVERIFY(!document.contains(QStringLiteral("seriesColor")));

    const QVariantMap vwap = createRow.call({engine.toScriptValue(schema),
                                             QJSValue(QStringLiteral("VWAP")),
                                             QJSValue(QStringLiteral("vwap")),
                                             QJSValue(QStringLiteral("#2AD879"))})
                                 .toVariant().toMap();
    const QVariantMap vwapDocument = serialize.call({engine.toScriptValue(schema),
                                                      engine.toScriptValue(vwap)})
                                         .toVariant().toMap();
    QCOMPARE(vwapDocument.size(), 2);
    QCOMPARE(vwapDocument.value(QStringLiteral("indicator")).toString(),
             QStringLiteral("VWAP"));
}

void CustomStrategyWizardTest::preservesCompleteIndicatorRolesWithoutSerializingUnusedParameters()
{
    QJSEngine engine;
    QVERIFY(loadContractEngine(&engine,
        QStringLiteral("/qml/components/CustomStrategyWizardContract.js")));
    const QJSValue schema = engine.toScriptValue(CustomStrategyValidator().schemaDescriptor());
    const QJSValue createRow = engine.globalObject().property(QStringLiteral("newIndicatorRow"));
    const QJSValue migrate = engine.globalObject().property(QStringLiteral("migrateIndicatorRows"));
    const QJSValue serialize = engine.globalObject().property(QStringLiteral("seriesDocument"));
    const QVariantMap row = createRow.call({schema, QJSValue(QStringLiteral("SMA")),
        QJSValue(QStringLiteral("average")), QJSValue(QStringLiteral("#21B6E9"))})
        .toVariant().toMap();
    QVERIFY(row.contains(QStringLiteral("kPeriod")));
    QVERIFY(row.contains(QStringLiteral("dPeriod")));
    QCOMPARE(row.value(QStringLiteral("kPeriod")).toInt(), 0);
    QCOMPARE(row.value(QStringLiteral("dPeriod")).toInt(), 0);

    const QVariantList legacy{QVariantMap{
        {QStringLiteral("name"), QStringLiteral("SMA")},
        {QStringLiteral("alias"), QStringLiteral("average")},
        {QStringLiteral("period"), 5},
        {QStringLiteral("visible"), true},
    }};
    const QVariantMap restored = migrate.call({engine.toScriptValue(legacy), schema})
        .toVariant().toList().first().toMap();
    QVERIFY(restored.contains(QStringLiteral("kPeriod")));
    QVERIFY(restored.contains(QStringLiteral("dPeriod")));
    QCOMPARE(restored.value(QStringLiteral("kPeriod")).toInt(), 0);
    QCOMPARE(restored.value(QStringLiteral("dPeriod")).toInt(), 0);
    const QVariantMap document = serialize.call({schema, engine.toScriptValue(restored)})
        .toVariant().toMap();
    const QVariantMap expected{
        {QStringLiteral("indicator"), QStringLiteral("SMA")},
        {QStringLiteral("alias"), QStringLiteral("average")},
        {QStringLiteral("period"), 5},
    };
    QCOMPARE(document, expected);
}

void CustomStrategyWizardTest::rejectsIndicatorParameterViolationsBeforePreview()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));

    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue createRow = engine.globalObject().property(QStringLiteral("newIndicatorRow"));
    const QJSValue validate = engine.globalObject().property(QStringLiteral("validateIndicatorRow"));
    QVERIFY(createRow.isCallable());
    QVERIFY(validate.isCallable());

    CustomStrategyValidator validator;
    const QVariantMap schema = validator.schemaDescriptor();
    QVariantMap macd = createRow.call({engine.toScriptValue(schema),
                                       QJSValue(QStringLiteral("MACD")),
                                       QJSValue(QStringLiteral("trend")),
                                       QJSValue(QStringLiteral("#21B6E9"))})
                           .toVariant().toMap();
    QCOMPARE(validate.call({engine.toScriptValue(schema), engine.toScriptValue(macd)})
                 .toVariant().toMap().value(QStringLiteral("valid")).toBool(), true);

    macd.insert(QStringLiteral("fastPeriod"), 30);
    const QVariantMap relationError = validate.call({engine.toScriptValue(schema),
                                                      engine.toScriptValue(macd)})
                                         .toVariant().toMap();
    QCOMPARE(relationError.value(QStringLiteral("valid")).toBool(), false);
    QCOMPARE(relationError.value(QStringLiteral("error")).toString(),
             QStringLiteral("快速周期必须小于慢速周期"));

    QVariantMap rsi = createRow.call({engine.toScriptValue(schema),
                                      QJSValue(QStringLiteral("RSI")),
                                      QJSValue(QStringLiteral("rsi")),
                                      QJSValue(QStringLiteral("#2AD879"))})
                          .toVariant().toMap();
    rsi.insert(QStringLiteral("period"), 1);
    const QVariantMap rangeError = validate.call({engine.toScriptValue(schema),
                                                   engine.toScriptValue(rsi)})
                                      .toVariant().toMap();
    QCOMPARE(rangeError.value(QStringLiteral("valid")).toBool(), false);
    QCOMPARE(rangeError.value(QStringLiteral("key")).toString(), QStringLiteral("period"));
}

void CustomStrategyWizardTest::fillsMissingIndicatorParametersDuringDraftMigration()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));

    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue migrate = engine.globalObject().property(QStringLiteral("migrateDraft"));
    QVERIFY(migrate.isCallable());

    CustomStrategyValidator validator;
    const QVariantMap legacy {
        {QStringLiteral("series"), QVariantList{QVariantMap{
             {QStringLiteral("name"), QStringLiteral("BOLLINGER")},
             {QStringLiteral("alias"), QStringLiteral("bands")},
             {QStringLiteral("visible"), true}}}},
    };
    const QVariantMap migrated = migrate.call({engine.toScriptValue(legacy),
                                                engine.toScriptValue(validator.schemaDescriptor())})
                                     .toVariant().toMap();
    const QVariantMap row = migrated.value(QStringLiteral("series")).toList().first().toMap();
    QCOMPARE(row.value(QStringLiteral("period")).toInt(), 20);
    QCOMPARE(row.value(QStringLiteral("standardDeviations")).toDouble(), 2.0);
    QCOMPARE(row.value(QStringLiteral("output")).toString(), QStringLiteral("middle"));
    QCOMPARE(row.value(QStringLiteral("seriesVisible")).toBool(), true);
}

void CustomStrategyWizardTest::blocksInvalidOrDuplicateIndicatorRowsAtStepGate()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));

    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue createRow = engine.globalObject().property(QStringLiteral("newIndicatorRow"));
    const QJSValue validateRows = engine.globalObject().property(QStringLiteral("validateIndicatorRows"));
    QVERIFY(createRow.isCallable());
    QVERIFY(validateRows.isCallable());

    CustomStrategyValidator validator;
    const QVariantMap schema = validator.schemaDescriptor();
    const QVariantMap fast = createRow.call({engine.toScriptValue(schema),
                                             QJSValue(QStringLiteral("SMA")),
                                             QJSValue(QStringLiteral("trend")),
                                             QJSValue(QStringLiteral("#21B6E9"))})
                                 .toVariant().toMap();
    const QVariantMap slow = createRow.call({engine.toScriptValue(schema),
                                             QJSValue(QStringLiteral("EMA")),
                                             QJSValue(QStringLiteral("trend")),
                                             QJSValue(QStringLiteral("#2AD879"))})
                                 .toVariant().toMap();
    const QVariantList duplicateRows{fast, slow};
    const QVariantMap duplicate = validateRows.call({engine.toScriptValue(schema),
                                                      engine.toScriptValue(duplicateRows)})
                                      .toVariant().toMap();
    QCOMPARE(duplicate.value(QStringLiteral("valid")).toBool(), false);
    QCOMPARE(duplicate.value(QStringLiteral("row")).toInt(), 1);
    QVERIFY(duplicate.value(QStringLiteral("error")).toString().contains(
        QStringLiteral("别名重复")));

    const QVariantMap valid = validateRows.call({engine.toScriptValue(schema),
                                                  engine.toScriptValue(QVariantList{fast})})
                                  .toVariant().toMap();
    QCOMPARE(valid.value(QStringLiteral("valid")).toBool(), true);
}

void CustomStrategyWizardTest::migratesFlatConditionsAndPreservesNestedBooleanExpressions()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));

    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue migrateTree = engine.globalObject().property(
        QStringLiteral("migrateConditionTree"));
    const QJSValue conditionDocument = engine.globalObject().property(
        QStringLiteral("conditionDocument"));
    QVERIFY(migrateTree.isCallable());
    QVERIFY(conditionDocument.isCallable());

    const QVariantList legacyRows {
        QVariantMap{{QStringLiteral("left"), QStringLiteral("close")},
                    {QStringLiteral("op"), QStringLiteral("gt")},
                    {QStringLiteral("right"), QStringLiteral("fast")}},
        QVariantMap{{QStringLiteral("leftOperand"), QStringLiteral("fast")},
                    {QStringLiteral("leftKind"), QStringLiteral("series")},
                    {QStringLiteral("op"), QStringLiteral("gt")},
                    {QStringLiteral("rightKind"), QStringLiteral("value")},
                    {QStringLiteral("rightConstant"), 10.0}},
    };
    const QVariantMap migrated = migrateTree.call({engine.toScriptValue(legacyRows),
                                                    engine.toScriptValue(
                                                        QStringList{QStringLiteral("fast")})})
                                     .toVariant().toMap();
    QCOMPARE(migrated.value(QStringLiteral("op")).toString(), QStringLiteral("and"));
    const QVariantList migratedChildren = migrated.value(QStringLiteral("conditions")).toList();
    QCOMPARE(migratedChildren.size(), 2);
    QCOMPARE(migratedChildren.first().toMap().value(QStringLiteral("leftKind")).toString(),
             QStringLiteral("field"));
    QCOMPARE(migratedChildren.first().toMap().value(QStringLiteral("rightKind")).toString(),
             QStringLiteral("series"));

    const QVariantMap nestedAnd {
        {QStringLiteral("op"), QStringLiteral("and")},
        {QStringLiteral("conditions"), QVariantList{
             migratedChildren.at(0), migratedChildren.at(1)}},
    };
    const QVariantMap negatedAnd {
        {QStringLiteral("op"), QStringLiteral("not")},
        {QStringLiteral("condition"), nestedAnd},
    };
    const QVariantMap nested {
        {QStringLiteral("op"), QStringLiteral("or")},
        {QStringLiteral("conditions"), QVariantList{
             migratedChildren.first(), negatedAnd}},
    };
    const QVariantMap document = conditionDocument.call({engine.toScriptValue(nested)})
                                     .toVariant().toMap();
    QCOMPARE(document.value(QStringLiteral("op")).toString(), QStringLiteral("or"));
    const QVariantList children = document.value(QStringLiteral("conditions")).toList();
    QCOMPARE(children.size(), 2);
    const QVariantMap negated = children.at(1).toMap();
    QCOMPARE(negated.value(QStringLiteral("op")).toString(), QStringLiteral("not"));
    QCOMPARE(negated.value(QStringLiteral("condition")).toMap()
                 .value(QStringLiteral("op")).toString(), QStringLiteral("and"));
    QVERIFY(!children.first().toMap().contains(QStringLiteral("leftKind")));
    QCOMPARE(children.first().toMap().value(QStringLiteral("left"))
                 .toMap().value(QStringLiteral("field")).toString(), QStringLiteral("close"));
}

void CustomStrategyWizardTest::editsNestedConditionTreeByStablePaths()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));

    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue appendGroup = engine.globalObject().property(
        QStringLiteral("appendConditionGroup"));
    const QJSValue updateNode = engine.globalObject().property(
        QStringLiteral("updateConditionNode"));
    const QJSValue removeNode = engine.globalObject().property(
        QStringLiteral("removeConditionNode"));
    const QJSValue editorRows = engine.globalObject().property(
        QStringLiteral("conditionEditorRows"));
    const QJSValue conditionDocument = engine.globalObject().property(
        QStringLiteral("conditionDocument"));
    QVERIFY(appendGroup.isCallable());
    QVERIFY(updateNode.isCallable());
    QVERIFY(removeNode.isCallable());
    QVERIFY(editorRows.isCallable());
    QVERIFY(conditionDocument.isCallable());

    const QVariantMap comparison {
        {QStringLiteral("leftOperand"), QStringLiteral("close")},
        {QStringLiteral("leftKind"), QStringLiteral("field")},
        {QStringLiteral("op"), QStringLiteral("gt")},
        {QStringLiteral("rightOperand"), QString{}},
        {QStringLiteral("rightKind"), QStringLiteral("value")},
        {QStringLiteral("rightConstant"), 10.0},
    };
    QVariantMap tree {
        {QStringLiteral("op"), QStringLiteral("and")},
        {QStringLiteral("conditions"), QVariantList{comparison, comparison}},
    };
    tree = appendGroup.call({engine.toScriptValue(tree), QJSValue(QStringLiteral("")),
                             QJSValue(QStringLiteral("or"))}).toVariant().toMap();
    QVariantList rows = editorRows.call({engine.toScriptValue(tree)}).toVariant().toList();
    QCOMPARE(rows.size(), 6);
    QCOMPARE(rows.at(3).toMap().value(QStringLiteral("path")).toString(),
             QStringLiteral("2"));
    QCOMPARE(rows.at(3).toMap().value(QStringLiteral("nodeType")).toString(),
             QStringLiteral("group"));

    tree = updateNode.call({engine.toScriptValue(tree), QJSValue(QStringLiteral("2")),
                            engine.toScriptValue(QVariantMap{
                                {QStringLiteral("negated"), true}})})
               .toVariant().toMap();
    tree = updateNode.call({engine.toScriptValue(tree), QJSValue(QStringLiteral("2/0")),
                            engine.toScriptValue(QVariantMap{
                                {QStringLiteral("leftOperand"), QStringLiteral("volume")},
                                {QStringLiteral("rightConstant"), 100.0}})})
               .toVariant().toMap();
    const QVariantMap document = conditionDocument.call({engine.toScriptValue(tree)})
                                     .toVariant().toMap();
    const QVariantMap nestedNot = document.value(QStringLiteral("conditions"))
                                      .toList().at(2).toMap();
    QCOMPARE(nestedNot.value(QStringLiteral("op")).toString(), QStringLiteral("not"));
    QCOMPARE(nestedNot.value(QStringLiteral("condition")).toMap()
                 .value(QStringLiteral("op")).toString(), QStringLiteral("or"));
    const QVariantMap updatedComparison = nestedNot.value(QStringLiteral("condition"))
                                              .toMap().value(QStringLiteral("conditions"))
                                              .toList().first().toMap();
    QCOMPARE(updatedComparison.value(QStringLiteral("left")).toMap()
                 .value(QStringLiteral("field")).toString(), QStringLiteral("volume"));
    QCOMPARE(updatedComparison.value(QStringLiteral("right")).toMap()
                 .value(QStringLiteral("value")).toDouble(), 100.0);

    tree = removeNode.call({engine.toScriptValue(tree), QJSValue(QStringLiteral("1"))})
               .toVariant().toMap();
    rows = editorRows.call({engine.toScriptValue(tree)}).toVariant().toList();
    QCOMPARE(rows.size(), 5);
}

void CustomStrategyWizardTest::removingIndicatorPrunesNestedReferences()
{
    QFile contract(QStringLiteral(PPTRADER_SOURCE_DIR
                                  "/qml/components/CustomStrategyWizardContract.js"));
    QVERIFY2(contract.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(contract.errorString()));
    QJSEngine engine;
    const QJSValue evaluation = engine.evaluate(QString::fromUtf8(contract.readAll()),
                                                 contract.fileName());
    QVERIFY2(!evaluation.isError(), qPrintable(evaluation.toString()));
    const QJSValue prune = engine.globalObject().property(
        QStringLiteral("removeConditionSeries"));
    const QJSValue document = engine.globalObject().property(
        QStringLiteral("conditionDocument"));
    QVERIFY(prune.isCallable());
    QVERIFY(document.isCallable());

    const QVariantMap fastComparison {
        {QStringLiteral("leftOperand"), QStringLiteral("fast")},
        {QStringLiteral("leftKind"), QStringLiteral("series")},
        {QStringLiteral("op"), QStringLiteral("gt")},
        {QStringLiteral("rightOperand"), QString{}},
        {QStringLiteral("rightKind"), QStringLiteral("value")},
        {QStringLiteral("rightConstant"), 0.0},
    };
    QVariantMap slowComparison = fastComparison;
    slowComparison.insert(QStringLiteral("leftOperand"), QStringLiteral("slow"));
    const QVariantMap negatedFast {
        {QStringLiteral("op"), QStringLiteral("not")},
        {QStringLiteral("condition"), fastComparison},
    };
    const QVariantMap nestedOr {
        {QStringLiteral("op"), QStringLiteral("or")},
        {QStringLiteral("conditions"), QVariantList{slowComparison, negatedFast}},
    };
    const QVariantMap tree {
        {QStringLiteral("op"), QStringLiteral("and")},
        {QStringLiteral("conditions"), QVariantList{fastComparison, nestedOr}},
    };
    const QVariantMap pruned = prune.call({engine.toScriptValue(tree),
                                           QJSValue(QStringLiteral("fast"))})
                                   .toVariant().toMap();
    const QVariantList rootChildren = pruned.value(QStringLiteral("conditions")).toList();
    QCOMPARE(rootChildren.size(), 1);
    const QVariantList nestedChildren = rootChildren.first().toMap()
                                            .value(QStringLiteral("conditions")).toList();
    QCOMPARE(nestedChildren.size(), 1);
    QCOMPARE(nestedChildren.first().toMap().value(QStringLiteral("leftOperand")).toString(),
             QStringLiteral("slow"));
    const QByteArray serialized = QJsonDocument::fromVariant(
        document.call({engine.toScriptValue(pruned)}).toVariant()).toJson(QJsonDocument::Compact);
    QVERIFY(!serialized.contains("fast"));
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::CustomStrategyWizardTest)

#include "tst_custom_strategy_wizard.moc"
