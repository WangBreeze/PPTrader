#include <QAbstractListModel>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

#include <memory>

namespace pp {

class EmptyIntentModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count CONSTANT)

public:
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : 0;
    }
    QVariant data(const QModelIndex &, int) const override { return {}; }
    int count() const { return 0; }
};

class PluginPanelControllerStub final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList customStrategyExecutionCatalog READ customStrategyExecutionCatalog
               NOTIFY strategyPluginStateChanged)
    Q_PROPERTY(QVariantList loadedStrategyPlugins READ loadedStrategyPlugins
               NOTIFY strategyPluginStateChanged)
    Q_PROPERTY(QString strategyPluginDirectory READ strategyPluginDirectory
               NOTIFY strategyPluginStateChanged)
    Q_PROPERTY(int pendingStrategyIntentCount READ zero CONSTANT)
    Q_PROPERTY(QAbstractItemModel *strategyIntentModel READ strategyIntentModel CONSTANT)
    Q_PROPERTY(bool customStrategyReplayRunning READ falseValue CONSTANT)
    Q_PROPERTY(int customStrategyReplayedBars READ zero CONSTANT)
    Q_PROPERTY(int customStrategyReplayTotalBars READ zero CONSTANT)

public:
    explicit PluginPanelControllerStub(QObject *parent = nullptr) : QObject(parent) {}

    QVariantList customStrategyExecutionCatalog() const
    {
        return {
            QVariantMap{{QStringLiteral("strategyId"), QStringLiteral("mock_alpha")},
                        {QStringLiteral("name"), QStringLiteral("模拟执行策略")},
                        {QStringLiteral("version"), 3},
                        {QStringLiteral("executionScope"), QStringLiteral("mock_execution")},
                        {QStringLiteral("state"), 0},
                        {QStringLiteral("stateName"), QStringLiteral("未加载")},
                        {QStringLiteral("runtimeEligible"), true},
                        {QStringLiteral("unavailableReason"), QString{}},
                        {QStringLiteral("error"), QString{}},
                        {QStringLiteral("symbol"), QString{}}},
            QVariantMap{{QStringLiteral("strategyId"), QStringLiteral("research_only")},
                        {QStringLiteral("name"), QStringLiteral("仅历史回测策略")},
                        {QStringLiteral("version"), 1},
                        {QStringLiteral("executionScope"), QStringLiteral("backtest_only")},
                        {QStringLiteral("state"), 0},
                        {QStringLiteral("stateName"), QStringLiteral("未加载")},
                        {QStringLiteral("runtimeEligible"), false},
                        {QStringLiteral("unavailableReason"),
                         QStringLiteral("策略执行范围为仅历史回测")},
                        {QStringLiteral("error"), QString{}},
                        {QStringLiteral("symbol"), QString{}}}};
    }
    QVariantList loadedStrategyPlugins() const
    {
        return {QVariantMap{{QStringLiteral("pluginId"), QStringLiteral("sample.plugin")},
                            {QStringLiteral("pluginName"), QStringLiteral("测试 C++ 插件")},
                            {QStringLiteral("fileName"), QStringLiteral("sample.dll")}}};
    }
    QString strategyPluginDirectory() const { return QStringLiteral("C:/test/strategies"); }
    int zero() const { return 0; }
    bool falseValue() const { return false; }
    QAbstractItemModel *strategyIntentModel() { return &m_intents; }

    Q_INVOKABLE QVariantList strategyPluginScan()
    {
        ++m_scanCount;
        return {QVariantMap{{QStringLiteral("fileName"), QStringLiteral("sample.dll")},
                            {QStringLiteral("manifestOk"), true},
                            {QStringLiteral("error"), QString{}}}};
    }
    Q_INVOKABLE QVariantList strategyPluginParameterSchema(const QString &) const
    {
        return {QVariantMap{{QStringLiteral("key"), QStringLiteral("threshold")},
                            {QStringLiteral("displayName"), QStringLiteral("阈值")},
                            {QStringLiteral("type"), QStringLiteral("real")},
                            {QStringLiteral("defaultValue"), 1.5},
                            {QStringLiteral("minimumValue"), 0.0},
                            {QStringLiteral("maximumValue"), 10.0}}};
    }
    Q_INVOKABLE bool activateStrategyPluginStrategy(const QString &pluginId,
                                                     const QVariantMap &configuration)
    {
        m_activatedPluginId = pluginId;
        m_activationConfiguration = configuration;
        return true;
    }
    Q_INVOKABLE bool loadStrategyPlugin(const QString &) { return true; }
    Q_INVOKABLE bool unloadStrategyPlugin(const QString &) { return true; }
    Q_INVOKABLE bool enableCustomStrategyExecution(const QString &, int, const QString &)
    {
        return true;
    }
    Q_INVOKABLE bool disableCustomStrategyExecution(const QString &) { return true; }
    Q_INVOKABLE QVariantMap notificationSettings() const
    {
        return {{QStringLiteral("desktopEnabled"), false},
                {QStringLiteral("webhookEnabled"), false},
                {QStringLiteral("webhookUrl"), QString{}},
                {QStringLiteral("notifySignal"), true},
                {QStringLiteral("notifyApproval"), true},
                {QStringLiteral("notifyOrderSubmitted"), true},
                {QStringLiteral("notifyExecutionRejected"), true},
                {QStringLiteral("notifyIntentExpired"), true}};
    }
    Q_INVOKABLE bool configureNotifications(bool, bool, const QString &, bool, bool,
                                             bool, bool, bool)
    {
        return true;
    }
    Q_INVOKABLE bool startCustomStrategyReplay(const QString &, const QString &, int, int, int)
    {
        return true;
    }
    Q_INVOKABLE void stopCustomStrategyReplay() {}
    Q_INVOKABLE bool approveStrategyIntent(const QString &) { return true; }
    Q_INVOKABLE bool rejectStrategyIntent(const QString &, const QString &) { return true; }

    int scanCount() const { return m_scanCount; }
    QString activatedPluginId() const { return m_activatedPluginId; }
    QVariantMap activationConfiguration() const { return m_activationConfiguration; }

signals:
    void strategyPluginStateChanged();
    void notificationSettingsChanged();

private:
    EmptyIntentModel m_intents;
    int m_scanCount = 0;
    QString m_activatedPluginId;
    QVariantMap m_activationConfiguration;
};

class StrategyPluginPanelQmlTest final : public QObject {
    Q_OBJECT

private slots:
    void createsResponsivePanelAndUsesRealControllerContracts();
};

void StrategyPluginPanelQmlTest::createsResponsivePanelAndUsesRealControllerContracts()
{
    QQmlEngine engine;
    PluginPanelControllerStub controller;
    const QString componentsDir = QStringLiteral(PPTRADER_SOURCE_DIR "/qml/components");
    engine.addImportPath(componentsDir);

    QQmlComponent themeComponent(&engine,
        QUrl::fromLocalFile(componentsDir + QStringLiteral("/PPTheme.qml")));
    std::unique_ptr<QObject> theme(themeComponent.create());
    QVERIFY2(theme, qPrintable(themeComponent.errorString()));

    QQmlComponent panelComponent(&engine,
        QUrl::fromLocalFile(componentsDir + QStringLiteral("/StrategyPluginPanel.qml")));
    QVariantMap initialProperties{
        {QStringLiteral("theme"), QVariant::fromValue(theme.get())},
        {QStringLiteral("controller"), QVariant::fromValue(&controller)},
        {QStringLiteral("width"), 1200},
        {QStringLiteral("height"), 760}};
    std::unique_ptr<QObject> panel(panelComponent.createWithInitialProperties(initialProperties));
    QVERIFY2(panel, qPrintable(panelComponent.errorString()));
    theme.release()->setParent(panel.get());
    QCoreApplication::processEvents();

    QCOMPARE(panel->property("compactLayout").toBool(), false);
    QCOMPARE(panel->property("runtimeCatalog").toList().size(), 2);
    const QVariantMap unavailable = panel->property("runtimeCatalog").toList().at(1).toMap();
    QCOMPARE(unavailable.value(QStringLiteral("runtimeEligible")).toBool(), false);
    QCOMPARE(unavailable.value(QStringLiteral("unavailableReason")).toString(),
             QStringLiteral("策略执行范围为仅历史回测"));

    QObject *catalogCard = panel->findChild<QObject *>(QStringLiteral("strategyPluginCatalogCard"));
    QVERIFY(catalogCard);
    QCOMPARE(qRound(catalogCard->property("width").toDouble()), 430);

    QVERIFY(QMetaObject::invokeMethod(panel.get(), "refreshScan"));
    QCOMPARE(controller.scanCount(), 1);
    QCOMPARE(panel->property("scanResults").toList().size(), 1);

    QObject *strategyId = panel->findChild<QObject *>(QStringLiteral("pluginStrategyId"));
    QObject *symbol = panel->findChild<QObject *>(QStringLiteral("pluginSymbol"));
    QVERIFY(strategyId);
    QVERIFY(symbol);
    strategyId->setProperty("text", QStringLiteral("third_party_alpha"));
    symbol->setProperty("text", QStringLiteral("600519"));
    QVERIFY(QMetaObject::invokeMethod(panel.get(), "reloadPluginParameters"));
    QVariant activated;
    QVERIFY(QMetaObject::invokeMethod(panel.get(), "activateLoadedPlugin",
                                      Q_RETURN_ARG(QVariant, activated)));
    QCOMPARE(activated.toBool(), true);
    QCOMPARE(controller.activatedPluginId(), QStringLiteral("sample.plugin"));
    QCOMPARE(controller.activationConfiguration().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("third_party_alpha"));
    QCOMPARE(controller.activationConfiguration().value(QStringLiteral("symbol")).toString(),
             QStringLiteral("600519"));
    QCOMPARE(controller.activationConfiguration().value(QStringLiteral("threshold")).toDouble(),
             1.5);

    panel->setProperty("width", 760);
    panel->setProperty("height", 1500);
    QCoreApplication::processEvents();
    QCOMPARE(panel->property("compactLayout").toBool(), true);
    QObject *grid = panel->findChild<QObject *>(QStringLiteral("strategyPluginGrid"));
    QVERIFY(grid);
    const QString compactGeometry = QStringLiteral("panel=%1 grid=%2 card=%3")
        .arg(panel->property("width").toDouble())
        .arg(grid->property("width").toDouble())
        .arg(catalogCard->property("width").toDouble());
    QVERIFY2(qRound(catalogCard->property("width").toDouble())
                 == qRound(grid->property("width").toDouble()),
             qPrintable(compactGeometry));
}

} // namespace pp

QTEST_MAIN(pp::StrategyPluginPanelQmlTest)

#include "tst_strategy_plugin_panel_qml.moc"
