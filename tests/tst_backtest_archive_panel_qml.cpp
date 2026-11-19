#include "models/BacktestArchiveListModel.h"
#include "models/LocalTickSeriesListModel.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <memory>

namespace pp {

class BacktestArchivePanelControllerStub final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *backtestArchiveModel READ backtestArchiveModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *backtestArchiveRawTickModel
               READ backtestArchiveRawTickModel CONSTANT)
    Q_PROPERTY(int backtestArchiveCount READ backtestArchiveCount CONSTANT)
    Q_PROPERTY(QVariantMap backtestArchiveStorage READ backtestArchiveStorage CONSTANT)
    Q_PROPERTY(bool backtestArchiveCompactionRunning READ falseValue CONSTANT)
    Q_PROPERTY(QString backtestArchiveCompactionError READ emptyString CONSTANT)
    Q_PROPERTY(QString selectedBacktestArchiveId READ selectedBacktestArchiveId
               NOTIFY backtestArchiveStateChanged)
    Q_PROPERTY(bool backtestArchiveLoading READ falseValue CONSTANT)
    Q_PROPERTY(QString backtestArchiveError READ emptyString CONSTANT)
    Q_PROPERTY(QString backtestArchiveSaveError READ emptyString CONSTANT)
    Q_PROPERTY(bool backtestResultHistoricalSnapshot READ trueValue CONSTANT)
    Q_PROPERTY(bool backtestRunning READ falseValue CONSTANT)
    Q_PROPERTY(QVariantMap backtestResultConfiguration
               READ backtestResultConfiguration NOTIFY backtestArchiveStateChanged)
    Q_PROPERTY(bool backtestArchiveRawTickPageLoading READ falseValue CONSTANT)
    Q_PROPERTY(QString backtestArchiveRawTickPageResultId
               READ backtestArchiveRawTickPageResultId CONSTANT)
    Q_PROPERTY(int backtestArchiveRawTickPageOffset READ zero CONSTANT)
    Q_PROPERTY(int backtestArchiveRawTickPageSize READ pageSize CONSTANT)
    Q_PROPERTY(int backtestArchiveRawTickTotalCount READ totalCount CONSTANT)
    Q_PROPERTY(bool backtestArchiveRawTickHasPrevious READ falseValue CONSTANT)
    Q_PROPERTY(bool backtestArchiveRawTickHasNext READ trueValue CONSTANT)
    Q_PROPERTY(QString backtestArchiveRawTickPageError READ emptyString CONSTANT)

public:
    explicit BacktestArchivePanelControllerStub(QObject *parent = nullptr)
        : QObject(parent)
    {
        BacktestArchiveSummary summary;
        summary.resultId = QStringLiteral("raw-result-1");
        summary.taskId = QStringLiteral("77");
        summary.completedAt = QDateTime(QDate(2026, 8, 30), QTime(1, 2), Qt::UTC);
        summary.strategyId = QStringLiteral("tick_momentum");
        summary.instrument = {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                              Market::AShare, QStringLiteral("上交所")};
        summary.frequency = QStringLiteral("tick");
        summary.requestedBars = 12;
        summary.actualBars = 12;
        summary.dataVersion = QStringLiteral("sha256:test");
        m_archives.loadSummaries({summary});

        LocalTickSeriesPoint point;
        point.eventIndex = 0;
        point.tick.instrument = summary.instrument;
        point.tick.timestamp = summary.completedAt;
        point.tick.tradingDate = summary.completedAt.date();
        point.tick.sourceLine = 1;
        point.tick.price = 100.0;
        point.tick.volumeShares = 100.0;
        point.tick.amount = 10000.0;
        point.tick.direction = RawTradeDirection::Buy;
        point.tick.source = QStringLiteral("archive_fixture");
        m_ticks.setPoints({point});
    }

    QAbstractItemModel *backtestArchiveModel() { return &m_archives; }
    QAbstractItemModel *backtestArchiveRawTickModel() { return &m_ticks; }
    int backtestArchiveCount() const { return m_archives.count(); }
    QVariantMap backtestArchiveStorage() const
    {
        return {{QStringLiteral("archiveBytes"), 4096},
                {QStringLiteral("databaseBytes"), 8192},
                {QStringLiteral("walBytes"), 0},
                {QStringLiteral("reusableBytes"), 4096}};
    }
    QString selectedBacktestArchiveId() const { return m_selectedResultId; }
    bool falseValue() const { return false; }
    bool trueValue() const { return true; }
    QString emptyString() const { return {}; }
    QVariantMap backtestResultConfiguration() const
    {
        return {{QStringLiteral("rawTickSnapshotAvailable"), true}};
    }
    QString backtestArchiveRawTickPageResultId() const
    {
        return QStringLiteral("raw-result-1");
    }
    int zero() const { return 0; }
    int pageSize() const { return 256; }
    int totalCount() const { return 12; }

    Q_INVOKABLE bool loadBacktestArchive(const QString &) { return true; }
    Q_INVOKABLE bool deleteBacktestArchive(const QString &resultId)
    {
        m_lastDeletedResultId = resultId;
        return true;
    }
    Q_INVOKABLE bool compactBacktestArchiveStorage()
    {
        m_compactionRequested = true;
        return true;
    }
    Q_INVOKABLE void refreshBacktestArchiveList() {}
    Q_INVOKABLE qulonglong requestBacktestArchiveRawTickPage(
        const QString &resultId, int offset, int requestedPageSize)
    {
        m_lastRequestedResultId = resultId;
        m_lastRequestedOffset = offset;
        m_lastRequestedPageSize = requestedPageSize;
        return 41;
    }

    void selectDifferentArchive()
    {
        m_selectedResultId = QStringLiteral("other-result");
        emit backtestArchiveStateChanged();
    }
    void selectOriginalArchive()
    {
        m_selectedResultId = QStringLiteral("raw-result-1");
        emit backtestArchiveStateChanged();
    }
    QString lastRequestedResultId() const { return m_lastRequestedResultId; }
    int lastRequestedOffset() const { return m_lastRequestedOffset; }
    int lastRequestedPageSize() const { return m_lastRequestedPageSize; }
    QString lastDeletedResultId() const { return m_lastDeletedResultId; }
    bool compactionRequested() const { return m_compactionRequested; }

signals:
    void backtestArchiveStateChanged();
    void backtestArchiveRawTickPageStateChanged();

private:
    BacktestArchiveListModel m_archives;
    LocalTickSeriesListModel m_ticks;
    QString m_selectedResultId = QStringLiteral("raw-result-1");
    QString m_lastRequestedResultId;
    int m_lastRequestedOffset = -1;
    int m_lastRequestedPageSize = 0;
    QString m_lastDeletedResultId;
    bool m_compactionRequested = false;
};

class BacktestArchivePanelQmlTest final : public QObject {
    Q_OBJECT

private slots:
    void rawTickBrowserRequestsFirstPageAndClosesForDifferentArchive();
};

void BacktestArchivePanelQmlTest::rawTickBrowserRequestsFirstPageAndClosesForDifferentArchive()
{
    QQmlEngine engine;
    BacktestArchivePanelControllerStub controller;
    QQuickWindow window;
    window.resize(1120, 760);
    const QString componentsDir = QStringLiteral(
        PPTRADER_SOURCE_DIR "/qml/components");
    engine.addImportPath(componentsDir);

    QQmlComponent themeComponent(
        &engine, QUrl::fromLocalFile(componentsDir + QStringLiteral("/PPTheme.qml")));
    std::unique_ptr<QObject> theme(themeComponent.create());
    QVERIFY2(theme, qPrintable(themeComponent.errorString()));

    QQmlComponent panelComponent(
        &engine,
        QUrl::fromLocalFile(componentsDir
                            + QStringLiteral("/BacktestArchivePanel.qml")));
    QVariantMap initialProperties {
        {QStringLiteral("parent"), QVariant::fromValue(window.contentItem())},
        {QStringLiteral("theme"), QVariant::fromValue(theme.get())},
        {QStringLiteral("controller"), QVariant::fromValue(&controller)},
        {QStringLiteral("width"), 1080},
        {QStringLiteral("height"), 520}};
    std::unique_ptr<QObject> panel(
        panelComponent.createWithInitialProperties(initialProperties));
    QVERIFY2(panel, qPrintable(panelComponent.errorString()));
    theme.release()->setParent(panel.get());
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(panel.get(), "openRawTickBrowser"));
    QTRY_VERIFY_WITH_TIMEOUT(
        panel->property("rawTickBrowserOpened").toBool(), 1000);
    QCOMPARE(controller.lastRequestedResultId(), QStringLiteral("raw-result-1"));
    QCOMPARE(controller.lastRequestedOffset(), 0);
    QCOMPARE(controller.lastRequestedPageSize(), 256);

    controller.selectDifferentArchive();
    QTRY_VERIFY_WITH_TIMEOUT(
        !panel->property("rawTickBrowserOpened").toBool(), 1000);

    controller.selectOriginalArchive();
    QVERIFY(QMetaObject::invokeMethod(panel.get(), "restoreSelection"));
    QObject *deleteButton = panel->findChild<QObject *>(
        QStringLiteral("deleteBacktestArchiveButton"));
    QObject *confirmation = panel->findChild<QObject *>(
        QStringLiteral("deleteBacktestArchiveConfirm"));
    QObject *confirmButton = panel->findChild<QObject *>(
        QStringLiteral("confirmDeleteBacktestArchiveButton"));
    QVERIFY(deleteButton);
    QVERIFY(confirmation);
    QVERIFY(confirmButton);
    QVERIFY(QMetaObject::invokeMethod(deleteButton, "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(confirmation->property("opened").toBool(), 1000);
    QVERIFY(QMetaObject::invokeMethod(confirmButton, "clicked"));
    QCOMPARE(controller.lastDeletedResultId(), QStringLiteral("raw-result-1"));

    QObject *compactButton = panel->findChild<QObject *>(
        QStringLiteral("compactBacktestArchiveButton"));
    QObject *compactConfirm = panel->findChild<QObject *>(
        QStringLiteral("compactBacktestArchiveConfirm"));
    QObject *startCompaction = panel->findChild<QObject *>(
        QStringLiteral("confirmCompactBacktestArchiveButton"));
    QVERIFY(compactButton);
    QVERIFY(compactConfirm);
    QVERIFY(startCompaction);
    QVERIFY(QMetaObject::invokeMethod(compactButton, "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(compactConfirm->property("opened").toBool(), 1000);
    QVERIFY(QMetaObject::invokeMethod(startCompaction, "clicked"));
    QVERIFY(controller.compactionRequested());
}

} // namespace pp

QTEST_MAIN(pp::BacktestArchivePanelQmlTest)

#include "tst_backtest_archive_panel_qml.moc"
