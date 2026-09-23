#include "app/RealtimeChartSession.h"
#include "models/RealtimeChartWindowListModel.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

namespace pp {

class RealtimeChartSessionTest : public QObject {
    Q_OBJECT

private slots:
    void buildsDynamicChartPanes();
    void enforcesChartLimitAndKeepsOnePane();
    void rejectsOldResponseAfterNewRequest();
    void preservesModelWhenProviderLastBarRegresses();
    void retriesWhenProviderLagsClosedMinute();
    void retriesFlatCountDespiteLeadingProviderLabel();
    void retryBudgetResetsAndStopsSafely();
    void catchupRespectsSessions_data();
    void catchupRespectsSessions();
    void parserPreservesUnverifiedLabelsAndAllReturnedBars();
    void switchingOneSessionDoesNotChangeAnother();
    void windowModelOwnsStableSessions();
};

namespace {
Instrument instrument(const QString &symbol, const QString &name)
{
    return {symbol, name, Market::AShare,
            symbol.startsWith(QLatin1Char('6')) ? QStringLiteral("SSE")
                                                : QStringLiteral("SZSE")};
}

RealtimeMinuteSeriesResult successfulResult(
    const Instrument &target, int count = 6)
{
    static const QTimeZone shanghai("Asia/Shanghai");
    RealtimeMinuteSeriesResult result;
    result.instrument = target;
    result.providerId = QStringLiteral("test_m1");
    result.requestedAt = QDateTime::currentDateTimeUtc();
    result.fetchedAt = result.requestedAt;
    const QDateTime first(QDate(2026, 9, 2), QTime(9, 30), shanghai);
    for (int index = 0; index < count; ++index) {
        Candle candle;
        candle.instrument = target;
        candle.startTime = first.addSecs(index * 60).toUTC();
        candle.periodSeconds = 60;
        candle.open = 10.0 + index * 0.01;
        candle.close = candle.open + 0.02;
        candle.high = candle.close + 0.01;
        candle.low = candle.open - 0.01;
        candle.volume = 1000.0 + index;
        candle.source = result.providerId;
        result.candles.append(candle);
    }
    result.lastBarTime = result.candles.last().startTime;
    result.succeeded = true;
    return result;
}
} // namespace

void RealtimeChartSessionTest::buildsDynamicChartPanes()
{
    quint64 submittedId = 0;
    RealtimeChartSession session(
        QStringLiteral("window-1"),
        instrument(QStringLiteral("600519"), QStringLiteral("贵州茅台")),
        [&submittedId](quint64 requestId, const Instrument &, int) {
            submittedId = requestId;
            return true;
        }, []() {});

    QCOMPARE(session.chartPaneCount(), 1);
    QCOMPARE(session.minuteCount(), 0);
    QVERIFY(!session.addChart().isEmpty());
    QVERIFY(!session.addChart().isEmpty());
    QCOMPARE(session.chartPaneCount(), 3);
    QCOMPARE(session.chartPaneModel()->index(0, 0)
                 .data(Qt::UserRole + 2).toInt(), 60);
    QCOMPARE(session.chartPaneModel()->index(1, 0)
                 .data(Qt::UserRole + 2).toInt(), 300);
    QCOMPARE(session.chartPaneModel()->index(2, 0)
                 .data(Qt::UserRole + 2).toInt(), 600);

    QCOMPARE(session.requestRefresh(), 1ULL);
    QCOMPARE(submittedId, 1ULL);
    QVERIFY(session.acceptResult(
        submittedId, successfulResult(
            instrument(QStringLiteral("600519"), QStringLiteral("贵州茅台")), 12)));
    QCOMPARE(session.minuteCount(), 12);
    QCOMPARE(session.fiveMinuteCount(), 3);
    QCOMPARE(session.countForPeriod(600), 2);
    QCOMPARE(session.chartPaneModel()->index(2, 0)
                 .data(Qt::UserRole + 5).toInt(), 2);
    QVERIFY(session.modelForPeriod(60) != session.modelForPeriod(300));
    QVERIFY(session.setChartPeriod(
        session.chartPaneModel()->index(2, 0).data(Qt::UserRole + 1).toString(),
        900));
    QCOMPARE(session.countForPeriod(600), 0);
    QCOMPARE(session.fifteenMinuteCount(), 1);
    QCOMPARE(session.successfulRefreshCount(), 1);
    QCOMPARE(session.failedRefreshCount(), 0);
}

void RealtimeChartSessionTest::enforcesChartLimitAndKeepsOnePane()
{
    RealtimeChartSession session(
        QStringLiteral("window-limit"),
        instrument(QStringLiteral("600519"), QStringLiteral("贵州茅台")),
        [](quint64, const Instrument &, int) { return true; }, []() {});
    while (session.chartPaneCount() < 10)
        QVERIFY(!session.addChart().isEmpty());
    QCOMPARE(session.chartPaneCount(), 10);
    QVERIFY(session.addChart().isEmpty());
    QVERIFY(!session.chartPaneError().isEmpty());

    while (session.chartPaneCount() > 1) {
        const QString chartId = session.chartPaneModel()->index(0, 0)
                                    .data(Qt::UserRole + 1).toString();
        QVERIFY(session.removeChart(chartId));
    }
    const QString finalId = session.chartPaneModel()->index(0, 0)
                                .data(Qt::UserRole + 1).toString();
    QVERIFY(!session.removeChart(finalId));
    QCOMPARE(session.chartPaneCount(), 1);
}

void RealtimeChartSessionTest::rejectsOldResponseAfterNewRequest()
{
    QVector<quint64> submitted;
    const Instrument target = instrument(
        QStringLiteral("000001"), QStringLiteral("平安银行"));
    RealtimeChartSession session(
        QStringLiteral("window-2"), target,
        [&submitted](quint64 requestId, const Instrument &, int) {
            submitted.append(requestId);
            return true;
        }, []() {});
    const quint64 first = session.requestRefresh();
    const quint64 second = session.requestRefresh();
    QCOMPARE(submitted, QVector<quint64>({first, second}));
    QVERIFY(!session.acceptResult(first, successfulResult(target)));
    QCOMPARE(session.minuteCount(), 0);
    QVERIFY(session.acceptResult(second, successfulResult(target)));
    QCOMPARE(session.resolvedRequestId(), second);
    QCOMPARE(session.minuteCount(), 6);
}

void RealtimeChartSessionTest::preservesModelWhenProviderLastBarRegresses()
{
    const Instrument target = instrument(
        QStringLiteral("600519"), QStringLiteral("贵州茅台"));
    RealtimeChartSession session(
        QStringLiteral("regression"), target,
        [](quint64, const Instrument &, int) { return true; }, []() {});

    const auto current = successfulResult(target, 10);
    QVERIFY(session.acceptResult(session.requestRefresh(), current));
    QCOMPARE(session.minuteCount(), 10);
    QCOMPARE(session.lastBarTime(), current.lastBarTime);

    auto regressed = successfulResult(target, 8);
    regressed.requestedAt = current.requestedAt.addSecs(60);
    regressed.fetchedAt = current.fetchedAt.addSecs(60);
    QVERIFY(regressed.lastBarTime < current.lastBarTime);
    QVERIFY(session.acceptResult(session.requestRefresh(), regressed));

    QCOMPARE(session.minuteCount(), 10);
    QCOMPARE(session.lastBarTime(), current.lastBarTime);
    QCOMPARE(session.providerLastBarTime(), regressed.lastBarTime);
    QCOMPARE(session.successfulRefreshCount(), 1);
    QCOMPARE(session.failedRefreshCount(), 1);
    const QVariantMap diagnostics = session.minuteRefreshDiagnostics();
    QVERIFY(diagnostics.value(QStringLiteral("providerRegressed")).toBool());
    QVERIFY(!diagnostics.value(QStringLiteral("accepted")).toBool());
    QVERIFY(diagnostics.value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("回退")));
}

void RealtimeChartSessionTest::retriesWhenProviderLagsClosedMinute()
{
    quint64 submittedId = 0;
    int submits = 0;
    const Instrument target = instrument(QStringLiteral("600519"), QStringLiteral("贵州茅台"));
    RealtimeChartSession session(
        QStringLiteral("catchup"), target,
        [&submittedId, &submits](quint64 requestId, const Instrument &, int) {
            submittedId = requestId;
            ++submits;
            return true;
        }, []() {});
    session.setRefreshEnabled(true, false);
    QVERIFY(session.requestRefresh() != 0);
    const QDateTime fetched(QDate(2026, 9, 2), QTime(1, 31, 2), Qt::UTC);
    auto result = successfulResult(target, 1);
    result.fetchedAt = fetched;
    result.candles.first().startTime = fetched.addSecs(-122);
    result.lastBarTime = result.candles.first().startTime;
    QVERIFY(session.acceptResult(submittedId, result));
    QCOMPARE(session.expectedMinute(), QDateTime(QDate(2026, 9, 2), QTime(1, 30), Qt::UTC));
    QCOMPARE(session.providerLastBarTime(), result.lastBarTime);
    QTRY_VERIFY_WITH_TIMEOUT(submits >= 2, 1500);
}

void RealtimeChartSessionTest::retriesFlatCountDespiteLeadingProviderLabel()
{
    // 9/4 窗口二：09:36 已返回 8 根/末标签09:37，09:37仍8根，
    // 下一轮09:38才10根。只重放根数/标签事实，不假定末柱是否闭合。
    const Instrument target = instrument(QStringLiteral("000001"), QStringLiteral("平安银行"));
    int submits = 0;
    RealtimeChartSession *active = nullptr;
    RealtimeChartSession session(QStringLiteral("leading"), target,
        [&](quint64 id, const Instrument &, int) {
            ++submits;
            if (submits == 3) {
                auto catchup = successfulResult(target, 9);
                catchup.fetchedAt = QDateTime(QDate(2026, 9, 2), QTime(1, 37, 3), Qt::UTC);
                QTimer::singleShot(0, active, [active, id, catchup]() {
                    active->acceptResult(id, catchup);
                });
            }
            return true;
        }, []() {});
    active = &session;
    session.setRefreshEnabled(true);
    auto previous = successfulResult(target, 8);
    previous.fetchedAt = QDateTime(QDate(2026, 9, 2), QTime(1, 36, 2), Qt::UTC);
    QVERIFY(session.acceptResult(session.requestRefresh(), previous));
    auto flat = previous;
    flat.fetchedAt = previous.fetchedAt.addSecs(60);
    QVERIFY(session.acceptResult(session.requestRefresh(), flat));
    QCOMPARE(session.minuteCount(), 8);
    QVERIFY(session.providerLastBarTime() > session.expectedMinute());
    QTRY_COMPARE_WITH_TIMEOUT(session.minuteCount(), 9, 1500);
    QCOMPARE(submits, 3);
    auto next = successfulResult(target, 10);
    next.fetchedAt = flat.fetchedAt.addSecs(60);
    QVERIFY(session.acceptResult(session.requestRefresh(), next));
    QCOMPARE(session.minuteCount(), 10);
    QVERIFY(!session.minuteRefreshDiagnostics().value(QStringLiteral("retryScheduled")).toBool());
    QVERIFY(!session.acceptResult(session.resolvedRequestId(), next));
}

void RealtimeChartSessionTest::retryBudgetResetsAndStopsSafely()
{
    const Instrument target = instrument(QStringLiteral("000001"), QStringLiteral("平安银行"));
    int submits = 0;
    auto flat = successfulResult(target, 8);
    flat.fetchedAt = QDateTime(QDate(2026, 9, 2), QTime(1, 36, 2), Qt::UTC);
    RealtimeChartSession *active = nullptr;
    RealtimeChartSession session(QStringLiteral("budget"), target,
        [&](quint64 id, const Instrument &, int) {
            ++submits;
            const auto result = flat;
            QTimer::singleShot(0, active, [active, id, result]() {
                active->acceptResult(id, result);
            });
            return true;
        }, []() {});
    active = &session;
    session.setRefreshEnabled(true, true);
    QTRY_COMPARE(session.minuteCount(), 8);
    flat.fetchedAt = flat.fetchedAt.addSecs(60);
    session.requestRefresh();
    QTRY_VERIFY_WITH_TIMEOUT(session.minuteRefreshDiagnostics()
        .value(QStringLiteral("retryExhausted")).toBool(), 2500);
    QCOMPARE(submits, 4); // 首次 + 常规 + 两次补请求
    QCOMPARE(session.minuteCount(), 8); // 失败追赶不能制造数据
    QVERIFY(!session.minuteRefreshDiagnostics().value(QStringLiteral("retryScheduled")).toBool());
    flat.fetchedAt = flat.fetchedAt.addSecs(60);
    session.requestRefresh();
    QTRY_VERIFY(session.minuteRefreshDiagnostics().value(QStringLiteral("retryScheduled")).toBool());
    QCOMPARE(session.minuteRefreshDiagnostics().value(QStringLiteral("retryAttempt")).toInt(), 0);
    session.setRefreshEnabled(false);
    const int stopped = submits;
    QTest::qWait(850);
    QCOMPARE(submits, stopped);
    session.setRefreshEnabled(true, true);
    QTRY_VERIFY(!session.loading());
    const quint64 cancelled = session.resolvedRequestId();
    session.setInstrument(instrument(QStringLiteral("600519"), QStringLiteral("贵州茅台")), false);
    QVERIFY(!session.acceptResult(cancelled, flat));
    QTest::qWait(850);
    QCOMPARE(submits, stopped + 1);
    QCOMPARE(session.minuteCount(), 0);
    QVERIFY(session.minuteRefreshDiagnostics().isEmpty());
    session.shutdown();
    QCOMPARE(session.requestRefresh(), 0ULL);
}

void RealtimeChartSessionTest::catchupRespectsSessions_data()
{
    QTest::addColumn<QTime>("time");
    QTest::addColumn<bool>("retry");
    QTest::newRow("before-open") << QTime(9, 29, 2) << false;
    QTest::newRow("morning") << QTime(10, 1, 2) << true;
    QTest::newRow("morning-final-grace") << QTime(11, 31, 2) << true;
    QTest::newRow("lunch") << QTime(12, 0, 2) << false;
    QTest::newRow("before-afternoon") << QTime(12, 59, 2) << false;
    QTest::newRow("afternoon") << QTime(13, 1, 2) << true;
    QTest::newRow("close") << QTime(15, 0, 2) << true;
    QTest::newRow("close-final-grace") << QTime(15, 1, 2) << true;
    QTest::newRow("after-close") << QTime(15, 2, 2) << false;
    QTest::newRow("extended-observation") << QTime(15, 30, 2) << false;
}

void RealtimeChartSessionTest::catchupRespectsSessions()
{
    QFETCH(QTime, time);
    QFETCH(bool, retry);
    const auto target = instrument(QStringLiteral("000001"), QStringLiteral("平安银行"));
    RealtimeChartSession session(QStringLiteral("boundaries"), target,
        [](quint64, const Instrument &, int) { return true; }, []() {});
    session.setRefreshEnabled(true);
    auto result = successfulResult(target, 1);
    result.fetchedAt = QDateTime(QDate(2026, 9, 2), time, QTimeZone("Asia/Shanghai")).toUTC();
    QVERIFY(session.acceptResult(session.requestRefresh(), result));
    QCOMPARE(session.minuteRefreshDiagnostics().value(QStringLiteral("retryScheduled")).toBool(), retry);
    QCOMPARE(session.minuteCount(), 1);
    // 同一状态在周末/陈旧日期不得反复补请求。
    result.fetchedAt = result.fetchedAt.addDays(3);
    QVERIFY(session.acceptResult(session.requestRefresh(), result));
    QVERIFY(!session.minuteRefreshDiagnostics().value(QStringLiteral("retryScheduled")).toBool());
}

void RealtimeChartSessionTest::parserPreservesUnverifiedLabelsAndAllReturnedBars()
{
    const QByteArray payload = R"({"code":0,"data":{"sz000001":{"m1":[
        ["202609021130","10","11","12","9","2"],
        ["202609021301","11","12","13","10","3"],
        ["202609021500","12","13","14","11","4"]]}}})";
    const auto target = instrument(QStringLiteral("000001"), QStringLiteral("平安银行"));
    const QDateTime fetched(QDate(2026, 9, 2), QTime(4, 59, 21), Qt::UTC);
    const auto result = TencentMinuteBarService::parseResponse(payload, target, fetched, fetched.addMSecs(100));
    QVERIFY(result.succeeded);
    QCOMPARE(result.candles.size(), 3);
    QCOMPARE(result.responseRowCount, 3);
    QCOMPARE(result.providerTailRows.at(1).toList().first().toString(), QStringLiteral("202609021301"));
    QCOMPARE(result.candles.at(1).startTime, QDateTime(QDate(2026, 9, 2), QTime(5, 1), Qt::UTC));
    QCOMPARE(result.candles.at(1).volume, 300.0);
    QCOMPARE(result.lastBarTime, QDateTime(QDate(2026, 9, 2), QTime(7, 0), Qt::UTC));
    QCOMPARE(result.payloadSha256.size(), 64);
    RealtimeChartSession left(QStringLiteral("left"), target,
        [](quint64, const Instrument &, int) { return true; }, []() {});
    RealtimeChartSession right(QStringLiteral("right"), target,
        [](quint64, const Instrument &, int) { return true; }, []() {});
    QVERIFY(left.acceptResult(left.requestRefresh(), result));
    QCOMPARE(left.minuteCount(), 3); // 保留领先标签，不无证据删柱或平移时间。
    QCOMPARE(right.minuteCount(), 0);
    auto twoMore = successfulResult(target, 5);
    twoMore.fetchedAt = fetched.addSecs(60);
    for (int index = 0; index < twoMore.candles.size(); ++index) {
        twoMore.candles[index].startTime = result.lastBarTime.addSecs(
            (index + 1) * 60);
    }
    twoMore.lastBarTime = twoMore.candles.last().startTime;
    QVERIFY(left.acceptResult(left.requestRefresh(), twoMore));
    QCOMPARE(left.minuteCount(), 5); // +2 是收到的事实，不能拆成两次虚假实时更新。
    QCOMPARE(left.minuteRefreshDiagnostics().value(QStringLiteral("countDelta")).toInt(), 2);
    QCOMPARE(right.minuteCount(), 0);
}

void RealtimeChartSessionTest::switchingOneSessionDoesNotChangeAnother()
{
    const Instrument first = instrument(
        QStringLiteral("600519"), QStringLiteral("贵州茅台"));
    const Instrument second = instrument(
        QStringLiteral("000001"), QStringLiteral("平安银行"));
    const Instrument replacement = instrument(
        QStringLiteral("000333"), QStringLiteral("美的集团"));
    const auto submitter = [](quint64, const Instrument &, int) { return true; };
    RealtimeChartSession left(QStringLiteral("left"), first, submitter, []() {});
    RealtimeChartSession right(QStringLiteral("right"), second, submitter, []() {});

    left.setInstrument(replacement, false);
    QCOMPARE(left.symbol(), replacement.symbol);
    QCOMPARE(left.instrumentSwitchCount(), 1);
    QCOMPARE(right.symbol(), second.symbol);
    QCOMPARE(right.instrumentSwitchCount(), 0);
}

void RealtimeChartSessionTest::windowModelOwnsStableSessions()
{
    RealtimeChartWindowListModel model;
    auto *first = model.appendSession(
        QStringLiteral("one"),
        instrument(QStringLiteral("600519"), QStringLiteral("贵州茅台")));
    auto *second = model.appendSession(
        QStringLiteral("two"),
        instrument(QStringLiteral("000001"), QStringLiteral("平安银行")));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.sessionById(QStringLiteral("one")), first);
    QCOMPARE(model.sessionById(QStringLiteral("two")), second);
    QCOMPARE(model.get(1).value(QStringLiteral("symbol")).toString(),
             QStringLiteral("000001"));
    QVERIFY(model.removeSession(QStringLiteral("one")));
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.sessionById(QStringLiteral("two")), second);
    QVERIFY(!model.removeSession(QStringLiteral("missing")));
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::RealtimeChartSessionTest)

#include "tst_realtime_chart_session.moc"
