#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <memory>

namespace pp {

class StrategyMatrixParameterDialogQmlTest final : public QObject {
    Q_OBJECT

private slots:
    void validatesAndSavesNumericParameterSnapshot();
};

void StrategyMatrixParameterDialogQmlTest::validatesAndSavesNumericParameterSnapshot()
{
    QQmlEngine engine;
    QQuickWindow window;
    window.resize(900, 650);
    const QString componentsDir = QStringLiteral(
        PPTRADER_SOURCE_DIR "/qml/components");
    engine.addImportPath(componentsDir);

    QQmlComponent themeComponent(
        &engine, QUrl::fromLocalFile(componentsDir + QStringLiteral("/PPTheme.qml")));
    std::unique_ptr<QObject> theme(themeComponent.create());
    QVERIFY2(theme, qPrintable(themeComponent.errorString()));

    QQmlComponent dialogComponent(
        &engine, QUrl::fromLocalFile(componentsDir
                                    + QStringLiteral("/StrategyMatrixParameterDialog.qml")));
    QVariantMap initialProperties {
        {QStringLiteral("parent"), QVariant::fromValue(window.contentItem())},
        {QStringLiteral("theme"), QVariant::fromValue(theme.get())}};
    std::unique_ptr<QObject> dialog(
        dialogComponent.createWithInitialProperties(initialProperties));
    QVERIFY2(dialog, qPrintable(dialogComponent.errorString()));
    theme.release()->setParent(dialog.get());

    const QVariantList specs {
        QVariantMap {{QStringLiteral("key"), QStringLiteral("fastPeriod")},
                     {QStringLiteral("displayName"), QStringLiteral("快速周期")},
                     {QStringLiteral("type"), QStringLiteral("integer")},
                     {QStringLiteral("defaultValue"), 5},
                     {QStringLiteral("minimumValue"), 2},
                     {QStringLiteral("maximumValue"), 120}}};
    engine.rootContext()->setContextProperty(QStringLiteral("parameterDialog"), dialog.get());
    engine.rootContext()->setContextProperty(QStringLiteral("parameterSpecs"), specs);
    auto evaluate = [&engine](const QString &script) {
        QQmlExpression expression(engine.rootContext(), nullptr, script);
        expression.evaluate();
        return !expression.hasError();
    };
    QSignalSpy saved(dialog.get(), SIGNAL(saved(QString,QVariant)));
    QVERIFY(saved.isValid());

    QVERIFY(evaluate(QStringLiteral(
        "parameterDialog.configure('sma_crossover#方案2', parameterSpecs, "
        "{fastPeriod: 200})")));
    QVERIFY(evaluate(QStringLiteral("parameterDialog.saveValues()")));
    QCOMPARE(saved.count(), 0);
    QVERIFY(!dialog->property("validationError").toString().isEmpty());

    QVERIFY(evaluate(QStringLiteral(
        "parameterDialog.changeValue('fastPeriod', '8')")));
    QVERIFY(evaluate(QStringLiteral("parameterDialog.saveValues()")));
    QCOMPARE(saved.count(), 1);
    QCOMPARE(saved.at(0).at(0).toString(), QStringLiteral("sma_crossover#方案2"));
    QCOMPARE(saved.at(0).at(1).toMap().value(QStringLiteral("fastPeriod")).toInt(), 8);
}

} // namespace pp

QTEST_MAIN(pp::StrategyMatrixParameterDialogQmlTest)

#include "tst_strategy_matrix_parameter_dialog_qml.moc"
