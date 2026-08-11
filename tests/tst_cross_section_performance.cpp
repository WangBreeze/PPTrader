#include "models/MarketSnapshotListModel.h"
#include "visualization/CrossSectionPlotData.h"
#include "visualization/CrossSectionScatterItem.h"

#include <QElapsedTimer>
#include <QSGGeometryNode>
#include <QTest>

#include <algorithm>
#include <array>
#include <functional>

namespace pp {

namespace {

class TestScatterItem final : public CrossSectionScatterItem {
public:
    using CrossSectionScatterItem::CrossSectionScatterItem;

    QSGNode *build(QSGNode *oldNode)
    {
        return updatePaintNode(oldNode, nullptr);
    }
};

MarketSnapshot makeSnapshot(int count)
{
    MarketSnapshot snapshot;
    snapshot.batchId = QStringLiteral("PERF-5207");
    snapshot.market = Market::AShare;
    snapshot.timestamp = QDateTime::fromString(
        QStringLiteral("2026-08-01T02:30:00.000Z"), Qt::ISODateWithMs);
    snapshot.completedAt = snapshot.timestamp.addMSecs(1800);
    snapshot.expectedCount = count;
    snapshot.quotes.reserve(count);
    for (int index = 0; index < count; ++index) {
        CrossSectionalQuote quote;
        const int board = index % 4;
        if (board == 0) {
            quote.instrument.symbol = QStringLiteral("6%1").arg(index, 5, 10, QLatin1Char('0'));
            quote.instrument.venue = QStringLiteral("上海");
        } else if (board == 1) {
            quote.instrument.symbol = QStringLiteral("0%1").arg(index, 5, 10, QLatin1Char('0'));
            quote.instrument.venue = QStringLiteral("深圳");
        } else if (board == 2) {
            quote.instrument.symbol = QStringLiteral("300%1")
                .arg(index % 1000, 3, 10, QLatin1Char('0'));
            quote.instrument.venue = QStringLiteral("深圳");
        } else {
            quote.instrument.symbol = QStringLiteral("688%1")
                .arg(index % 1000, 3, 10, QLatin1Char('0'));
            quote.instrument.venue = QStringLiteral("上海");
        }
        quote.instrument.name = quote.instrument.symbol;
        quote.instrument.market = Market::AShare;
        quote.timestamp = snapshot.timestamp;
        quote.price = 1.0 + (index % 20000) / 10.0;
        quote.previousClose = quote.price * 0.99;
        quote.volume = 1000.0 + index;
        quote.amount = quote.price * quote.volume;
        snapshot.quotes.append(std::move(quote));
    }
    return snapshot;
}

QVector<CrossSectionPlotPoint> makePoints(const MarketSnapshot &snapshot)
{
    QVector<CrossSectionPlotPoint> points;
    points.reserve(snapshot.quotes.size());
    int displayIndex = 0;
    for (const CrossSectionalQuote &quote : snapshot.quotes) {
        CrossSectionPlotPoint point;
        point.displayIndex = ++displayIndex;
        point.symbol = quote.instrument.symbol;
        point.board = MarketSnapshotListModel::boardForInstrument(quote.instrument);
        point.boardBit = CrossSectionPlotData::boardBit(point.board);
        point.price = quote.price;
        point.valid = true;
        points.append(std::move(point));
    }
    return points;
}

double percentileMilliseconds(QVector<qint64> samples, double percentile)
{
    std::sort(samples.begin(), samples.end());
    const int lastIndex = static_cast<int>(samples.size()) - 1;
    const int index = std::clamp(
        qRound(lastIndex * percentile), 0, lastIndex);
    return samples.at(index) / 1000000.0;
}

QVector<qint64> measure(int sampleCount, const std::function<void(int)> &operation)
{
    QVector<qint64> samples;
    samples.reserve(sampleCount);
    operation(-1);
    for (int sample = 0; sample < sampleCount; ++sample) {
        QElapsedTimer timer;
        timer.start();
        operation(sample);
        samples.append(timer.nsecsElapsed());
    }
    return samples;
}

void report(const char *name, const QVector<qint64> &samples)
{
    qInfo("%s: p50 %.3f ms, p95 %.3f ms, samples %d", name,
          percentileMilliseconds(samples, 0.50),
          percentileMilliseconds(samples, 0.95), samples.size());
}

} // namespace

class CrossSectionPerformanceTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesSwitchableCpuMetrics();
    void benchmarksFiveThousandPointInteractions();
    void keepsGeometryBoundedAcrossSizesAndDpr();
};

void CrossSectionPerformanceTest::exposesSwitchableCpuMetrics()
{
    MarketSnapshotListModel model;
    TestScatterItem item;
    item.setPerformanceMetricsEnabled(false);
    item.setWidth(1128.0);
    item.setHeight(430.0);
    item.setModel(&model);
    model.loadSnapshot(makeSnapshot(5207), false);
    QSGNode *node = item.build(nullptr);
    QCoreApplication::processEvents();
    QCOMPARE(item.modelPreparationSamples(), qulonglong(0));
    QCOMPARE(item.sceneGraphUpdateSamples(), qulonglong(0));
    QCOMPARE(item.renderMilliseconds(), -1.0);

    item.setPerformanceMetricsEnabled(true);
    model.loadSnapshot(makeSnapshot(5207), false);
    node = item.build(node);
    QTRY_COMPARE(item.sceneGraphUpdateSamples(), qulonglong(1));
    QCOMPARE(item.modelPreparationSamples(), qulonglong(1));
    QCOMPARE(item.visiblePointCount(), 5207);
    QCOMPARE(item.geometryVertexCount(), 5207 * 8 * 3);
    QVERIFY(item.modelPreparationMilliseconds() >= 0.0);
    QVERIFY(item.sceneGraphUpdateMilliseconds() >= 0.0);
    QCOMPARE(item.renderMilliseconds(), item.sceneGraphUpdateMilliseconds());
    QVERIFY2(item.modelPreparationMilliseconds() < 250.0,
             "5207 点模型准备超过 Debug 防回归上界 250 ms");
    QVERIFY2(item.sceneGraphUpdateMilliseconds() < 250.0,
             "5207 点 Scene Graph CPU 更新超过 Debug 防回归上界 250 ms");

    item.setBoardMask(30);
    node = item.build(node);
    item.setPerformanceMetricsEnabled(false);
    QCoreApplication::processEvents();
    QCOMPARE(item.modelPreparationMilliseconds(), -1.0);
    QCOMPARE(item.sceneGraphUpdateMilliseconds(), -1.0);
    QCOMPARE(item.sceneGraphUpdateSamples(), qulonglong(0));
    delete node;
}

void CrossSectionPerformanceTest::benchmarksFiveThousandPointInteractions()
{
    const MarketSnapshot snapshot = makeSnapshot(5207);
    const QVector<CrossSectionPlotPoint> points = makePoints(snapshot);

    CrossSectionPlotSummary summary;
    const QVector<qint64> summarySamples = measure(80, [&](int sample) {
        const std::array<int, 8> masks {31, 30, 29, 27, 23, 15, 7, 31};
        summary = CrossSectionPlotData::summarize(
            points, masks.at(static_cast<size_t>(std::max(0, sample) % masks.size())));
    });
    report("5207-point summary/filter", summarySamples);
    QVERIFY(percentileMilliseconds(summarySamples, 0.95) < 50.0);

    volatile qreal coordinateSink = 0.0;
    const QVector<qint64> coordinateSamples = measure(60, [&](int sample) {
        const bool logarithmic = sample % 2 == 0;
        for (const CrossSectionPlotPoint &point : points) {
            coordinateSink += CrossSectionPlotData::xFor(point, points.size(), 1128.0);
            coordinateSink += CrossSectionPlotData::yFor(
                point.price, logarithmic, 2000.0, 430.0);
        }
    });
    Q_UNUSED(coordinateSink);
    report("5207-point linear/log coordinate pass", coordinateSamples);
    QVERIFY(percentileMilliseconds(coordinateSamples, 0.95) < 50.0);

    const CrossSectionPlotSummary fullSummary =
        CrossSectionPlotData::summarize(points, 31);
    volatile int hitSink = 0;
    const QVector<qint64> hitSamples = measure(60, [&](int sample) {
        for (int iteration = 0; iteration < 1000; ++iteration) {
            const int index = (iteration * 37 + std::max(0, sample)) % points.size();
            const CrossSectionPlotPoint &point = points.at(index);
            const QPointF target(
                CrossSectionPlotData::xFor(point, points.size(), 1128.0),
                CrossSectionPlotData::yFor(
                    point.price, true, fullSummary.maximum, 430.0));
            hitSink += CrossSectionPlotData::nearestPoint(
                points, 31, true, fullSummary.maximum,
                1128.0, 430.0, target, 10.0);
        }
    });
    Q_UNUSED(hitSink);
    report("1000 hover hit tests", hitSamples);
    QVERIFY(percentileMilliseconds(hitSamples, 0.95) < 100.0);

    MarketSnapshotListModel model;
    TestScatterItem item;
    item.setPerformanceMetricsEnabled(true);
    item.setWidth(1128.0);
    item.setHeight(430.0);
    item.setModel(&model);
    const QVector<qint64> preparationSamples = measure(24, [&](int) {
        model.loadSnapshot(snapshot, false);
    });
    report("5207-point model preparation", preparationSamples);
    QVERIFY(percentileMilliseconds(preparationSamples, 0.95) < 250.0);

    QSGNode *node = item.build(nullptr);
    const QVector<qint64> geometrySamples = measure(32, [&](int sample) {
        const std::array<int, 8> masks {30, 31, 29, 31, 27, 31, 15, 31};
        item.setBoardMask(masks.at(static_cast<size_t>(std::max(0, sample) % masks.size())));
        item.setLogarithmic(sample % 2 == 0);
        item.setWidth(sample % 3 == 0 ? 986.0 : 1128.0);
        node = item.build(node);
    });
    report("5207-point Scene Graph geometry rebuild", geometrySamples);
    QVERIFY(percentileMilliseconds(geometrySamples, 0.95) < 250.0);
    QCoreApplication::processEvents();
    QVERIFY(item.sceneGraphUpdateSamples() >= qulonglong(1));
    delete node;
}

void CrossSectionPerformanceTest::keepsGeometryBoundedAcrossSizesAndDpr()
{
    MarketSnapshotListModel model;
    model.loadSnapshot(makeSnapshot(5207), false);
    TestScatterItem item;
    item.setPerformanceMetricsEnabled(true);
    item.setModel(&model);

    const std::array<QSizeF, 4> sizes {
        QSizeF(1128.0, 430.0), QSizeF(986.0, 430.0),
        QSizeF(1017.0, 430.0), QSizeF(1020.0, 374.0)
    };
    QSGNode *node = nullptr;
    for (const QSizeF &size : sizes) {
        item.setSize(size);
        node = item.build(node);
        int vertexCount = 0;
        for (QSGNode *child = node->firstChild(); child; child = child->nextSibling()) {
            auto *geometryNode = static_cast<QSGGeometryNode *>(child);
            const QSGGeometry *geometry = geometryNode->geometry();
            vertexCount += geometry->vertexCount();
            const QSGGeometry::Point2D *vertices = geometry->vertexDataAsPoint2D();
            for (int index = 0; index < geometry->vertexCount(); ++index) {
                QVERIFY(vertices[index].x >= -CrossSectionScatterItem::logicalPointRadius() - 0.01);
                QVERIFY(vertices[index].x <= size.width()
                        + CrossSectionScatterItem::logicalPointRadius() + 0.01);
                QVERIFY(vertices[index].y >= -CrossSectionScatterItem::logicalPointRadius() - 0.01);
                QVERIFY(vertices[index].y <= size.height()
                        + CrossSectionScatterItem::logicalPointRadius() + 0.01);
            }
        }
        QCOMPARE(vertexCount, 5207 * 8 * 3);
    }

    QCOMPARE(CrossSectionScatterItem::logicalPointRadius(), 2.0);
    QCOMPARE(CrossSectionScatterItem::logicalPointRadius() * 1.00, 2.0);
    QCOMPARE(CrossSectionScatterItem::logicalPointRadius() * 1.25, 2.5);
    QCOMPARE(CrossSectionScatterItem::logicalPointRadius() * 1.50, 3.0);
    delete node;
}

} // namespace pp

QTEST_MAIN(pp::CrossSectionPerformanceTest)

#include "tst_cross_section_performance.moc"
