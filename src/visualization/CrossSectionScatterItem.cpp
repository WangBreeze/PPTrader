#include "visualization/CrossSectionScatterItem.h"

#include "models/MarketSnapshotListModel.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QtGlobal>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>

#include <array>

namespace pp {

namespace {

int boardIndex(int boardBit)
{
    switch (boardBit) {
    case CrossSectionPlotData::MainBoard: return 0;
    case CrossSectionPlotData::ShenzhenMain: return 1;
    case CrossSectionPlotData::GrowthBoard: return 2;
    case CrossSectionPlotData::StarMarket: return 3;
    case CrossSectionPlotData::Bse: return 4;
    default: return -1;
    }
}

} // namespace

CrossSectionScatterItem::CrossSectionScatterItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    bool configured = false;
    const int requested = qEnvironmentVariableIntValue(
        "PPTRADER_PERF_METRICS", &configured);
#if defined(QT_NO_DEBUG)
    m_performanceMetricsEnabled = configured && requested > 0;
#else
    m_performanceMetricsEnabled = !configured || requested > 0;
#endif
}

QAbstractItemModel *CrossSectionScatterItem::model() const { return m_model.data(); }

void CrossSectionScatterItem::setModel(QAbstractItemModel *model)
{
    if (m_model == model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model) {
        connect(m_model, &QAbstractItemModel::modelReset,
                this, &CrossSectionScatterItem::rebuildModelData);
        connect(m_model, &QAbstractItemModel::dataChanged,
                this, &CrossSectionScatterItem::rebuildModelData);
        connect(m_model, &QAbstractItemModel::rowsInserted,
                this, &CrossSectionScatterItem::rebuildModelData);
        connect(m_model, &QAbstractItemModel::rowsRemoved,
                this, &CrossSectionScatterItem::rebuildModelData);
        connect(m_model, &QAbstractItemModel::layoutChanged,
                this, &CrossSectionScatterItem::rebuildModelData);
        connect(m_model, &QObject::destroyed, this, [this]() {
            m_model = nullptr;
            rebuildModelData();
            emit modelChanged();
        });
    }
    rebuildModelData();
    emit modelChanged();
}

int CrossSectionScatterItem::boardMask() const { return m_boardMask; }

void CrossSectionScatterItem::setBoardMask(int boardMask)
{
    if (m_boardMask == boardMask) return;
    m_boardMask = boardMask;
    rebuildSummary();
    markGeometryDirty();
    emit boardMaskChanged();
}

bool CrossSectionScatterItem::logarithmic() const { return m_logarithmic; }

void CrossSectionScatterItem::setLogarithmic(bool logarithmic)
{
    if (m_logarithmic == logarithmic) return;
    m_logarithmic = logarithmic;
    markGeometryDirty();
    emit logarithmicChanged();
}

QVariantMap CrossSectionScatterItem::summary() const { return m_summary; }
int CrossSectionScatterItem::pointCount() const { return m_points.size(); }
bool CrossSectionScatterItem::performanceMetricsEnabled() const
{
    return m_performanceMetricsEnabled;
}

void CrossSectionScatterItem::setPerformanceMetricsEnabled(bool enabled)
{
    if (m_performanceMetricsEnabled == enabled) return;
    m_performanceMetricsEnabled = enabled;
    ++m_performanceMetricsGeneration;
    resetPerformanceMetrics();
    emit performanceMetricsEnabledChanged();
}

qreal CrossSectionScatterItem::modelPreparationMilliseconds() const
{
    return m_modelPreparationMilliseconds;
}

qreal CrossSectionScatterItem::sceneGraphUpdateMilliseconds() const
{
    return m_sceneGraphUpdateMilliseconds;
}

qulonglong CrossSectionScatterItem::modelPreparationSamples() const
{
    return m_modelPreparationSamples;
}

qulonglong CrossSectionScatterItem::sceneGraphUpdateSamples() const
{
    return m_sceneGraphUpdateSamples;
}

int CrossSectionScatterItem::visiblePointCount() const { return m_visiblePointCount; }
int CrossSectionScatterItem::geometryVertexCount() const { return m_geometryVertexCount; }
qreal CrossSectionScatterItem::renderMilliseconds() const { return m_renderMilliseconds; }
QColor CrossSectionScatterItem::mainBoardColor() const { return m_mainBoardColor; }
QColor CrossSectionScatterItem::shenzhenMainColor() const { return m_shenzhenMainColor; }
QColor CrossSectionScatterItem::growthBoardColor() const { return m_growthBoardColor; }
QColor CrossSectionScatterItem::starMarketColor() const { return m_starMarketColor; }
QColor CrossSectionScatterItem::bseColor() const { return m_bseColor; }

#define PP_COLOR_SETTER(Method, Member) \
    void CrossSectionScatterItem::Method(const QColor &color) \
    { \
        if (Member == color) return; \
        Member = color; \
        markGeometryDirty(); \
        emit colorsChanged(); \
    }

PP_COLOR_SETTER(setMainBoardColor, m_mainBoardColor)
PP_COLOR_SETTER(setShenzhenMainColor, m_shenzhenMainColor)
PP_COLOR_SETTER(setGrowthBoardColor, m_growthBoardColor)
PP_COLOR_SETTER(setStarMarketColor, m_starMarketColor)
PP_COLOR_SETTER(setBseColor, m_bseColor)

#undef PP_COLOR_SETTER

QVariantMap CrossSectionScatterItem::findNearest(qreal x, qreal y, qreal radius) const
{
    const int index = CrossSectionPlotData::nearestPoint(
        m_points, m_boardMask, m_logarithmic, m_plotSummary.maximum,
        width(), height(), QPointF(x, y), radius);
    return index >= 0 ? pointMap(m_points.at(index)) : QVariantMap{};
}

QVariantMap CrossSectionScatterItem::pointBySymbol(const QString &symbol) const
{
    const auto iterator = std::lower_bound(
        m_points.cbegin(), m_points.cend(), symbol,
        [](const CrossSectionPlotPoint &point, const QString &value) {
            return point.symbol < value;
        });
    return iterator != m_points.cend() && iterator->symbol == symbol
        ? pointMap(*iterator) : QVariantMap{};
}

QPointF CrossSectionScatterItem::positionForPoint(const QVariantMap &point) const
{
    CrossSectionPlotPoint value;
    value.displayIndex = point.value(QStringLiteral("displayIndex")).toInt();
    value.price = point.value(QStringLiteral("price")).toDouble();
    return QPointF(CrossSectionPlotData::xFor(value, m_points.size(), width()),
                   CrossSectionPlotData::yFor(value.price, m_logarithmic,
                                              m_plotSummary.maximum, height()));
}

QSGNode *CrossSectionScatterItem::updatePaintNode(
    QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!m_geometryDirty) return oldNode;
    QElapsedTimer timer;
    if (m_performanceMetricsEnabled) timer.start();

    delete oldNode;
    auto *rootNode = new QSGNode;
    std::array<QVector<const CrossSectionPlotPoint *>, 5> groups;
    for (const CrossSectionPlotPoint &point : std::as_const(m_points)) {
        if (!CrossSectionPlotData::isVisible(point, m_boardMask)) continue;
        const int index = boardIndex(point.boardBit);
        if (index >= 0) groups.at(index).append(&point);
    }

    const std::array<QColor, 5> colors = {
        m_mainBoardColor, m_shenzhenMainColor, m_growthBoardColor,
        m_starMarketColor, m_bseColor
    };
    constexpr int segmentCount = 8;
    constexpr std::array<QPointF, segmentCount + 1> unitCircle = {
        QPointF(1.0, 0.0), QPointF(0.70710678, 0.70710678),
        QPointF(0.0, 1.0), QPointF(-0.70710678, 0.70710678),
        QPointF(-1.0, 0.0), QPointF(-0.70710678, -0.70710678),
        QPointF(0.0, -1.0), QPointF(0.70710678, -0.70710678),
        QPointF(1.0, 0.0)
    };
    for (int groupIndex = 0; groupIndex < static_cast<int>(groups.size()); ++groupIndex) {
        const QVector<const CrossSectionPlotPoint *> &group = groups.at(groupIndex);
        if (group.isEmpty()) continue;

        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(),
                                         group.size() * segmentCount * 3);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        geometry->setVertexDataPattern(QSGGeometry::StaticPattern);
        QSGGeometry::Point2D *vertices = geometry->vertexDataAsPoint2D();

        for (int pointIndex = 0; pointIndex < group.size(); ++pointIndex) {
            const CrossSectionPlotPoint &point = *group.at(pointIndex);
            const qreal x = CrossSectionPlotData::xFor(point, m_points.size(), width());
            const qreal y = CrossSectionPlotData::yFor(
                point.price, m_logarithmic, m_plotSummary.maximum, height());
            const int pointOffset = pointIndex * segmentCount * 3;
            for (int segment = 0; segment < segmentCount; ++segment) {
                const int vertexOffset = pointOffset + segment * 3;
                vertices[vertexOffset].set(float(x), float(y));
                vertices[vertexOffset + 1].set(
                    float(x + unitCircle.at(segment).x() * logicalPointRadius()),
                    float(y + unitCircle.at(segment).y() * logicalPointRadius()));
                vertices[vertexOffset + 2].set(
                    float(x + unitCircle.at(segment + 1).x() * logicalPointRadius()),
                    float(y + unitCircle.at(segment + 1).y() * logicalPointRadius()));
            }
        }

        auto *node = new QSGGeometryNode;
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        auto *material = new QSGFlatColorMaterial;
        QColor color = colors.at(groupIndex);
        color.setAlphaF(color.alphaF() * 0.68);
        material->setColor(color);
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
        rootNode->appendChildNode(node);
    }

    m_geometryDirty = false;
    if (!m_performanceMetricsEnabled) return rootNode;

    int visiblePointCount = 0;
    for (const auto &group : groups) visiblePointCount += group.size();
    const int geometryVertexCount = visiblePointCount * segmentCount * 3;
    const qreal elapsed = timer.nsecsElapsed() / 1000000.0;
    const qulonglong generation = m_performanceMetricsGeneration;
    QPointer<CrossSectionScatterItem> guard(this);
    QMetaObject::invokeMethod(this, [guard, elapsed, visiblePointCount,
                                     geometryVertexCount, generation]() {
        if (guard) {
            guard->recordSceneGraphUpdate(elapsed, visiblePointCount,
                                          geometryVertexCount, generation);
        }
    }, Qt::QueuedConnection);
    return rootNode;
}

void CrossSectionScatterItem::geometryChange(const QRectF &newGeometry,
                                             const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) markGeometryDirty();
}

void CrossSectionScatterItem::rebuildModelData()
{
    QElapsedTimer timer;
    if (m_performanceMetricsEnabled) timer.start();
    const int previousCount = m_points.size();
    m_points.clear();
    if (m_model) {
        if (const auto *snapshotModel =
                qobject_cast<const MarketSnapshotListModel *>(m_model.data())) {
            const MarketSnapshot &snapshot = snapshotModel->snapshot();
            m_points.reserve(snapshot.quotes.size());
            for (int row = 0; row < snapshot.quotes.size(); ++row) {
                const CrossSectionalQuote &quote = snapshot.quotes.at(row);
                CrossSectionPlotPoint point;
                point.displayIndex = row + 1;
                point.symbol = quote.instrument.symbol;
                point.name = quote.instrument.name.isEmpty()
                    ? quote.instrument.symbol : quote.instrument.name;
                point.board = MarketSnapshotListModel::boardForInstrument(quote.instrument);
                point.price = quote.price;
                point.previousClose = quote.previousClose;
                point.changePercent = quote.previousClose > 0.0
                    ? (quote.price - quote.previousClose) / quote.previousClose * 100.0
                    : 0.0;
                point.volume = quote.volume;
                point.amount = quote.amount;
                point.snapshotTime = quote.timestamp.isValid()
                    ? quote.timestamp : snapshot.timestamp;
                point.valid = quote.price > 0.0 && !quote.suspended;
                point.stale = snapshotModel->snapshotIsStale() || quote.stale;
                point.boardBit = CrossSectionPlotData::boardBit(point.board);
                m_points.append(std::move(point));
            }
        } else {
            const QHash<int, QByteArray> names = m_model->roleNames();
            QHash<QByteArray, int> roles;
            for (auto iterator = names.cbegin(); iterator != names.cend(); ++iterator) {
                roles.insert(iterator.value(), iterator.key());
            }
            const auto value = [this, &roles](const QModelIndex &index,
                                               const QByteArray &name) {
                const auto iterator = roles.constFind(name);
                return iterator == roles.cend()
                    ? QVariant{} : m_model->data(index, iterator.value());
            };
            const int rows = m_model->rowCount();
            m_points.reserve(rows);
            for (int row = 0; row < rows; ++row) {
                const QModelIndex index = m_model->index(row, 0);
                CrossSectionPlotPoint point;
                point.displayIndex = value(index, "displayIndex").toInt();
                if (point.displayIndex <= 0) point.displayIndex = row + 1;
                point.symbol = value(index, "symbol").toString();
                point.name = value(index, "name").toString();
                point.board = value(index, "board").toString();
                point.price = value(index, "price").toDouble();
                point.previousClose = value(index, "previousClose").toDouble();
                point.changePercent = value(index, "changePercent").toDouble();
                point.volume = value(index, "volume").toDouble();
                point.amount = value(index, "amount").toDouble();
                point.snapshotTime = value(index, "snapshotTime").toDateTime();
                point.valid = value(index, "valid").toBool();
                point.stale = value(index, "stale").toBool();
                point.boardBit = CrossSectionPlotData::boardBit(point.board);
                m_points.append(std::move(point));
            }
        }
    }

    rebuildSummary();
    if (m_performanceMetricsEnabled) {
        recordModelPreparation(timer.nsecsElapsed() / 1000000.0);
    }
    markGeometryDirty();
    if (previousCount != m_points.size()) emit pointCountChanged();
    emit snapshotDataChanged();
}

void CrossSectionScatterItem::rebuildSummary()
{
    m_plotSummary = CrossSectionPlotData::summarize(m_points, m_boardMask);
    QVariantList top;
    for (int index : std::as_const(m_plotSummary.topIndices)) {
        top.append(pointMap(m_points.at(index)));
    }
    const QVariantMap boardCounts {
        {QStringLiteral("主板"), m_plotSummary.mainBoardCount},
        {QStringLiteral("深市主板"), m_plotSummary.shenzhenMainCount},
        {QStringLiteral("创业板"), m_plotSummary.growthBoardCount},
        {QStringLiteral("科创板"), m_plotSummary.starMarketCount},
        {QStringLiteral("北交所"), m_plotSummary.bseCount},
        {QStringLiteral("未知"), m_plotSummary.unknownCount}
    };
    m_summary = {
        {QStringLiteral("validCount"), m_plotSummary.validCount},
        {QStringLiteral("invalidCount"), m_plotSummary.invalidCount},
        {QStringLiteral("minimum"), m_plotSummary.minimum},
        {QStringLiteral("maximum"), m_plotSummary.maximum},
        {QStringLiteral("average"), m_plotSummary.average},
        {QStringLiteral("median"), m_plotSummary.median},
        {QStringLiteral("top"), top},
        {QStringLiteral("boardCounts"), boardCounts}
    };
    emit summaryChanged();
}

void CrossSectionScatterItem::markGeometryDirty()
{
    m_geometryDirty = true;
    update();
}

QVariantMap CrossSectionScatterItem::pointMap(const CrossSectionPlotPoint &point) const
{
    return {
        {QStringLiteral("displayIndex"), point.displayIndex},
        {QStringLiteral("symbol"), point.symbol},
        {QStringLiteral("name"), point.name},
        {QStringLiteral("board"), point.board},
        {QStringLiteral("price"), point.price},
        {QStringLiteral("previousClose"), point.previousClose},
        {QStringLiteral("changePercent"), point.changePercent},
        {QStringLiteral("volume"), point.volume},
        {QStringLiteral("amount"), point.amount},
        {QStringLiteral("snapshotTime"), point.snapshotTime},
        {QStringLiteral("valid"), point.valid},
        {QStringLiteral("stale"), point.stale}
    };
}

void CrossSectionScatterItem::recordModelPreparation(qreal milliseconds)
{
    m_modelPreparationMilliseconds = milliseconds;
    ++m_modelPreparationSamples;
    emit performanceMetricsChanged();
}

void CrossSectionScatterItem::recordSceneGraphUpdate(qreal milliseconds,
                                                     int visiblePointCount,
                                                     int geometryVertexCount,
                                                     qulonglong generation)
{
    if (!m_performanceMetricsEnabled
        || generation != m_performanceMetricsGeneration) return;
    m_sceneGraphUpdateMilliseconds = milliseconds;
    ++m_sceneGraphUpdateSamples;
    m_visiblePointCount = visiblePointCount;
    m_geometryVertexCount = geometryVertexCount;
    m_renderMilliseconds = milliseconds;
    emit performanceMetricsChanged();
    emit renderMillisecondsChanged();
}

void CrossSectionScatterItem::resetPerformanceMetrics()
{
    m_modelPreparationMilliseconds = -1.0;
    m_sceneGraphUpdateMilliseconds = -1.0;
    m_modelPreparationSamples = 0;
    m_sceneGraphUpdateSamples = 0;
    m_visiblePointCount = 0;
    m_geometryVertexCount = 0;
    m_renderMilliseconds = -1.0;
    emit performanceMetricsChanged();
    emit renderMillisecondsChanged();
}

} // namespace pp
