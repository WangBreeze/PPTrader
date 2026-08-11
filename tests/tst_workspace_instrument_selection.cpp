#include "app/WorkspaceInstrumentSelectionState.h"

#include <QTest>

namespace pp {

class WorkspaceInstrumentSelectionTest final : public QObject {
    Q_OBJECT

private slots:
    void keepsLiveBacktestAndLegacyResearchSelectionsIndependent();
    void rejectsMissingLocalDataAndSymbolsOutsideLiveUniverse();
};

static Instrument instrument(const QString &symbol, const QString &name)
{
    return {symbol, name, Market::AShare,
            symbol.startsWith(QStringLiteral("6"))
                ? QStringLiteral("上交所") : QStringLiteral("深交所")};
}

static LocalHistoryDataset dataset(const Instrument &value, int periodSeconds, int count)
{
    LocalHistoryDataset result;
    result.instrument = value;
    result.periodSeconds = periodSeconds;
    result.candleCount = count;
    result.firstTime = QDateTime(QDate(2025, 1, 1), QTime(7, 0), Qt::UTC);
    result.lastTime = result.firstTime.addDays(count - 1);
    result.source = QStringLiteral("raw_tick_cache_v1");
    return result;
}

void WorkspaceInstrumentSelectionTest::keepsLiveBacktestAndLegacyResearchSelectionsIndependent()
{
    const Instrument research = instrument(QStringLiteral("600519"), QStringLiteral("贵州茅台"));
    const Instrument live = instrument(QStringLiteral("000001"), QStringLiteral("平安银行"));
    const Instrument backtest = instrument(QStringLiteral("300750"), QStringLiteral("宁德时代"));
    const Instrument nextResearch = instrument(QStringLiteral("002594"), QStringLiteral("比亚迪"));

    WorkspaceInstrumentSelectionState state;
    state.setLiveUniverse({research, live, backtest, nextResearch});
    state.setLocalHistoryCatalog({dataset(backtest, 86400, 500)});
    state.setResearchInstrument(research);

    QString error;
    QVERIFY(state.selectLiveInstrument(live.symbol, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(state.selectBacktestInstrument(backtest.symbol, 86400, 100, &error));
    QCOMPARE(state.researchInstrument().symbol, research.symbol);
    QCOMPARE(state.liveInstrument().symbol, live.symbol);
    QCOMPARE(state.backtestInstrument().symbol, backtest.symbol);
    QCOMPARE(state.selectedBacktestDataset()->candleCount, 500);

    state.setResearchInstrument(nextResearch);
    QCOMPARE(state.researchInstrument().symbol, nextResearch.symbol);
    QCOMPARE(state.liveInstrument().symbol, live.symbol);
    QCOMPARE(state.backtestInstrument().symbol, backtest.symbol);

    QVERIFY(state.selectLiveInstrument(research.symbol, &error));
    QCOMPARE(state.researchInstrument().symbol, nextResearch.symbol);
    QCOMPARE(state.backtestInstrument().symbol, backtest.symbol);
}

void WorkspaceInstrumentSelectionTest::rejectsMissingLocalDataAndSymbolsOutsideLiveUniverse()
{
    const Instrument live = instrument(QStringLiteral("600519"), QStringLiteral("贵州茅台"));
    const Instrument cached = instrument(QStringLiteral("300750"), QStringLiteral("宁德时代"));
    WorkspaceInstrumentSelectionState state;
    state.setLiveUniverse({live});
    state.setLocalHistoryCatalog({dataset(cached, 86400, 20)});

    QString error;
    QVERIFY(!state.selectLiveInstrument(QStringLiteral("999999"), &error));
    QVERIFY(error.contains(QStringLiteral("股票池")));
    QVERIFY(state.liveInstrument().symbol.isEmpty());

    QVERIFY(!state.selectBacktestInstrument(QStringLiteral("000001"), 86400, 2, &error));
    QVERIFY(error.contains(QStringLiteral("本地没有")));
    QVERIFY(state.backtestInstrument().symbol.isEmpty());

    QVERIFY(!state.selectBacktestInstrument(cached.symbol, 86400, 100, &error));
    QVERIFY(error.contains(QStringLiteral("至少需要")));
    QVERIFY(state.selectBacktestInstrument(cached.symbol, 86400, 20, &error));
    QVERIFY(state.selectedBacktestDataset());

    LocalHistoryDataset remoteOnly = dataset(cached, 300, 500);
    remoteOnly.source = QStringLiteral("mootdx");
    state.setLocalHistoryCatalog({remoteOnly});
    QVERIFY(!state.selectBacktestInstrument(cached.symbol, 300, 20, &error));
    QVERIFY(error.contains(QStringLiteral("历史 Tick")));

    state.setLocalHistoryCatalog({});
    QCOMPARE(state.backtestInstrument().symbol, cached.symbol);
    QVERIFY(!state.selectedBacktestDataset());
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::WorkspaceInstrumentSelectionTest)

#include "tst_workspace_instrument_selection.moc"
