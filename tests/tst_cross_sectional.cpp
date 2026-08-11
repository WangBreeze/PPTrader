#include "services/AShareRelativeStrengthIndicator.h"
#include "services/CrossSectionalIndicatorEngine.h"
#include "services/MarketSnapshotAssembler.h"

#include <QElapsedTimer>
#include <QTest>

#include <memory>

namespace pp {

class CrossSectionalTest final : public QObject {
    Q_OBJECT

private slots:
    void assemblesUniverseAndCalculatesRelativeStrength();
    void processesFiveThousandStocksWithinCapacityBaseline();
};

void CrossSectionalTest::assemblesUniverseAndCalculatesRelativeStrength()
{
    QVector<Instrument> universe {
        {QStringLiteral("600001"), QStringLiteral("股票一"), Market::AShare, QStringLiteral("SSE")},
        {QStringLiteral("600002"), QStringLiteral("股票二"), Market::AShare, QStringLiteral("SSE")},
        {QStringLiteral("600003"), QStringLiteral("股票三"), Market::AShare, QStringLiteral("SSE")},
        {QStringLiteral("AAPL"), QStringLiteral("Apple"), Market::USStock, QStringLiteral("NASDAQ")}
    };
    const QDateTime timestamp(QDate(2026, 8, 3), QTime(1, 30), Qt::UTC);
    MarketSnapshotAssembler assembler(Market::AShare);
    assembler.setUniverse(universe);
    assembler.beginBatch(QStringLiteral("BATCH-1"), timestamp);
    QCOMPARE(assembler.expectedCount(), 3);

    CrossSectionalQuote first;
    first.instrument = universe[0];
    first.timestamp = timestamp;
    first.price = 110.0;
    first.previousClose = 100.0;
    QVERIFY(assembler.addQuote(first));

    CrossSectionalQuote second = first;
    second.instrument = universe[1];
    second.price = 100.0;
    QVERIFY(assembler.addQuote(second));
    second.price = 105.0;
    QVERIFY(assembler.addQuote(second));

    CrossSectionalQuote suspended = first;
    suspended.instrument = universe[2];
    suspended.suspended = true;
    QVERIFY(assembler.addQuote(suspended));

    CrossSectionalQuote foreign = first;
    foreign.instrument = universe[3];
    QVERIFY(!assembler.addQuote(foreign));
    QCOMPARE(assembler.receivedCount(), 3);

    const MarketSnapshot snapshot = assembler.completeBatch();
    QCOMPARE(snapshot.expectedCount, 3);
    QCOMPARE(snapshot.quotes.size(), 3);
    QCOMPARE(snapshot.completenessRatio(), 1.0);

    CrossSectionalIndicatorEngine engine;
    QVERIFY(engine.addIndicator(std::make_unique<AShareRelativeStrengthIndicator>()));
    QVERIFY(!engine.addIndicator(std::make_unique<AShareRelativeStrengthIndicator>()));
    const QVector<FactorFrame> frames = engine.calculate(snapshot);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().values.size(), 3);

    const FactorValue &strongest = frames.first().values[0];
    const FactorValue &secondStrongest = frames.first().values[1];
    const FactorValue &invalid = frames.first().values[2];
    QVERIFY(strongest.valid);
    QCOMPARE(strongest.rank, 1);
    QCOMPARE(strongest.percentile, 1.0);
    QVERIFY(qAbs(strongest.rawValue - 0.10) < 0.000001);
    QVERIFY(secondStrongest.valid);
    QCOMPARE(secondStrongest.rank, 2);
    QCOMPARE(secondStrongest.percentile, 0.0);
    QVERIFY(!invalid.valid);
    QCOMPARE(invalid.invalidReason, QStringLiteral("停牌标的"));

    MarketSnapshot unsupported = snapshot;
    unsupported.market = Market::USStock;
    QVERIFY(engine.calculate(unsupported).isEmpty());
}

void CrossSectionalTest::processesFiveThousandStocksWithinCapacityBaseline()
{
    constexpr int stockCount = 5000;
    QVector<Instrument> universe;
    universe.reserve(stockCount);
    for (int index = 0; index < stockCount; ++index) {
        universe.append({QString::number(600000 + index), QStringLiteral("测试股票"),
                         Market::AShare, QStringLiteral("SSE")});
    }

    const QDateTime timestamp = QDateTime::currentDateTimeUtc();
    QElapsedTimer timer;
    timer.start();
    MarketSnapshotAssembler assembler(Market::AShare);
    assembler.setUniverse(universe);
    assembler.beginBatch(QStringLiteral("FULL-MARKET-5000"), timestamp);
    for (int index = 0; index < stockCount; ++index) {
        Tick tick;
        tick.instrument = universe[index];
        tick.timestamp = timestamp;
        tick.previousClose = 100.0;
        tick.price = 97.5 + static_cast<double>(index) / 1000.0;
        tick.volume = 100000.0 + index;
        tick.amount = tick.price * tick.volume;
        QVERIFY(assembler.addTick(tick));
    }
    const MarketSnapshot snapshot = assembler.completeBatch();

    CrossSectionalIndicatorEngine engine;
    QVERIFY(engine.addIndicator(std::make_unique<AShareRelativeStrengthIndicator>()));
    const QVector<FactorFrame> frames = engine.calculate(snapshot);
    const qint64 elapsedMilliseconds = timer.elapsed();
    qInfo("5000-stock snapshot assembly and factor calculation: %lld ms",
          static_cast<long long>(elapsedMilliseconds));

    QCOMPARE(snapshot.quotes.size(), stockCount);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().values.size(), stockCount);
    QCOMPARE(frames.first().values.last().rank, 1);
    QVERIFY(elapsedMilliseconds < 2000);
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::CrossSectionalTest)

#include "tst_cross_sectional.moc"
