#include "services/MarketSession.h"

#include <QTest>
#include <QTimeZone>

namespace pp {

class MarketSessionTest final : public QObject {
    Q_OBJECT

private slots:
    void aShareSessionBoundaries();
    void weekendsAreClosed();
    void officialAShareHolidaysAreClosed();
    void cryptoIsAlwaysOpen();
};

namespace {

QDateTime toUtc(const QDate &date, const QTime &time, const QByteArray &zoneId)
{
    return QDateTime(date, time, QTimeZone(zoneId)).toUTC();
}

Instrument instrumentFor(Market market)
{
    Instrument instrument;
    instrument.symbol = QStringLiteral("TEST");
    instrument.market = market;
    return instrument;
}

} // namespace

void MarketSessionTest::aShareSessionBoundaries()
{
    const Instrument instrument = instrumentFor(Market::AShare);
    const QDate monday(2026, 8, 3);

    QVERIFY(!MarketSession::isOpen(instrument, toUtc(monday, QTime(9, 29, 59), "Asia/Shanghai")));
    QVERIFY(MarketSession::isOpen(instrument, toUtc(monday, QTime(9, 30), "Asia/Shanghai")));
    QVERIFY(!MarketSession::isOpen(instrument, toUtc(monday, QTime(11, 30), "Asia/Shanghai")));
    QVERIFY(MarketSession::isOpen(instrument, toUtc(monday, QTime(13, 0), "Asia/Shanghai")));
    QVERIFY(MarketSession::isOpen(instrument, toUtc(monday, QTime(15, 0), "Asia/Shanghai")));
    QVERIFY(MarketSession::isOpen(
        instrument, toUtc(monday, QTime(15, 29, 59), "Asia/Shanghai")));
    QVERIFY(!MarketSession::isOpen(instrument, toUtc(monday, QTime(15, 30), "Asia/Shanghai")));
    QVERIFY(MarketSession::statusText(
        instrument, toUtc(monday, QTime(15, 29, 59), "Asia/Shanghai"))
        .contains(QStringLiteral("13:00-15:30")));
}

void MarketSessionTest::weekendsAreClosed()
{
    const Instrument instrument = instrumentFor(Market::AShare);
    const QDate saturday(2026, 8, 1);

    QVERIFY(!MarketSession::isOpen(instrument, toUtc(saturday, QTime(10, 0), "Asia/Shanghai")));
}

void MarketSessionTest::officialAShareHolidaysAreClosed()
{
    const Instrument instrument = instrumentFor(Market::AShare);
    QVERIFY(!MarketSession::isOpen(
        instrument, toUtc(QDate(2026, 1, 2), QTime(10, 0), "Asia/Shanghai")));
    QVERIFY(MarketSession::isOpen(
        instrument, toUtc(QDate(2026, 1, 5), QTime(10, 0), "Asia/Shanghai")));
    QVERIFY(MarketSession::statusText(
        instrument, toUtc(QDate(2026, 1, 2), QTime(10, 0), "Asia/Shanghai"))
        .contains(QStringLiteral("元旦休市")));
}

void MarketSessionTest::cryptoIsAlwaysOpen()
{
    const Instrument instrument = instrumentFor(Market::Crypto);

    QVERIFY(MarketSession::isOpen(instrument, QDateTime(QDate(2026, 8, 1), QTime(3, 0), Qt::UTC)));
}

} // namespace pp

QTEST_APPLESS_MAIN(pp::MarketSessionTest)

#include "tst_market_session.moc"
