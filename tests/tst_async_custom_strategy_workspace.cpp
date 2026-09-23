#include "services/AsyncCustomStrategyWorkspaceService.h"
#include "services/SqliteLocalDataStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace pp {

class AsyncCustomStrategyWorkspaceTest final : public QObject {
    Q_OBJECT

private slots:
    void requestGateRejectsStaleCompletion();
    void cancelledRequestGateRejectsLateCompletion();
    void saveRunsAsynchronouslyAndPersists();
    void previewReportsValidationErrorAsynchronously();
};

static QString validStrategyJson()
{
    const auto indicator = [](const QString &name, int period) {
        return QJsonObject{{QStringLiteral("indicator"), name},
                           {QStringLiteral("period"), period}};
    };
    const QJsonObject document{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("strategyId"), QStringLiteral("custom_async_sma")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("name"), QStringLiteral("异步均线策略")},
        {QStringLiteral("description"), QStringLiteral("异步保存测试")},
        {QStringLiteral("author"), QStringLiteral("Qt Test")},
        {QStringLiteral("market"), QStringLiteral("A股")},
        {QStringLiteral("executionScope"), QStringLiteral("backtest_only")},
        {QStringLiteral("createdAt"), QStringLiteral("2026-08-19T00:00:00.000Z")},
        {QStringLiteral("updatedAt"), QStringLiteral("2026-08-19T00:00:00.000Z")},
        {QStringLiteral("parameters"), QJsonArray{}},
        {QStringLiteral("entry"), QJsonObject{
            {QStringLiteral("op"), QStringLiteral("crossesAbove")},
            {QStringLiteral("left"), indicator(QStringLiteral("SMA"), 5)},
            {QStringLiteral("right"), indicator(QStringLiteral("SMA"), 20)}}},
        {QStringLiteral("exit"), QJsonObject{
            {QStringLiteral("op"), QStringLiteral("crossesBelow")},
            {QStringLiteral("left"), indicator(QStringLiteral("SMA"), 5)},
            {QStringLiteral("right"), indicator(QStringLiteral("SMA"), 20)}}}};
    return QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
}

void AsyncCustomStrategyWorkspaceTest::requestGateRejectsStaleCompletion()
{
    AsyncOperationRequestGate gate;
    const quint64 first = gate.begin();
    const quint64 second = gate.begin();
    QVERIFY(second > first);
    QVERIFY(gate.loading());
    QVERIFY(!gate.finish(first));
    QVERIFY(gate.loading());
    QCOMPARE(gate.resolvedRequestId(), quint64(0));
    QVERIFY(gate.finish(second));
    QVERIFY(!gate.loading());
    QCOMPARE(gate.resolvedRequestId(), second);
}

void AsyncCustomStrategyWorkspaceTest::cancelledRequestGateRejectsLateCompletion()
{
    AsyncOperationRequestGate gate;
    const quint64 requestId = gate.begin();
    QVERIFY(gate.loading());
    gate.cancel();
    QVERIFY(!gate.loading());
    QCOMPARE(gate.resolvedRequestId(), quint64(0));
    QVERIFY(!gate.finish(requestId));
    const quint64 nextRequestId = gate.begin();
    QVERIFY(nextRequestId > requestId);
}

void AsyncCustomStrategyWorkspaceTest::saveRunsAsynchronouslyAndPersists()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("async.db"));
    AsyncCustomStrategyWorkspaceService service;
    QVERIFY(service.start(databasePath));
    QSignalSpy spy(&service, &AsyncCustomStrategyWorkspaceService::saveFinished);
    QVERIFY(service.requestSave(41, validStrategyJson()));
    QCOMPARE(spy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toULongLong(), quint64(41));
    const QVariantMap result = arguments.at(1).toMap();
    QVERIFY2(result.value(QStringLiteral("success")).toBool(),
             qPrintable(result.value(QStringLiteral("error")).toString()));
    service.stop();

    SqliteLocalDataStore verificationStore;
    QVERIFY(verificationStore.initialize(databasePath));
    const auto definitions = verificationStore.loadCustomStrategies();
    QCOMPARE(definitions.size(), 1);
    QCOMPARE(definitions.first().strategyId, QStringLiteral("custom_async_sma"));
}

void AsyncCustomStrategyWorkspaceTest::previewReportsValidationErrorAsynchronously()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AsyncCustomStrategyWorkspaceService service;
    QVERIFY(service.start(directory.filePath(QStringLiteral("preview.db"))));
    QSignalSpy spy(&service, &AsyncCustomStrategyWorkspaceService::previewReady);
    Instrument instrument{QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                          Market::AShare, QStringLiteral("上交所")};
    QVERIFY(service.requestPreview(9, QStringLiteral("{"), instrument, 300, 300));
    QCOMPARE(spy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toULongLong(), quint64(9));
    QVERIFY(!arguments.at(1).toMap().value(QStringLiteral("error")).toString().isEmpty());
}

} // namespace pp

QTEST_MAIN(pp::AsyncCustomStrategyWorkspaceTest)
#include "tst_async_custom_strategy_workspace.moc"
