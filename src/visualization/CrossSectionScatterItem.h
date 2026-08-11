#pragma once

#include "visualization/CrossSectionPlotData.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QPointer>
#include <QQuickItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace pp {

class CrossSectionScatterItem : public QQuickItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(CrossSectionScatterItem)
    Q_PROPERTY(QAbstractItemModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(int boardMask READ boardMask WRITE setBoardMask NOTIFY boardMaskChanged)
    Q_PROPERTY(bool logarithmic READ logarithmic WRITE setLogarithmic NOTIFY logarithmicChanged)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY summaryChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY pointCountChanged)
    Q_PROPERTY(bool performanceMetricsEnabled READ performanceMetricsEnabled
               WRITE setPerformanceMetricsEnabled NOTIFY performanceMetricsEnabledChanged)
    Q_PROPERTY(qreal modelPreparationMilliseconds READ modelPreparationMilliseconds
               NOTIFY performanceMetricsChanged)
    Q_PROPERTY(qreal sceneGraphUpdateMilliseconds READ sceneGraphUpdateMilliseconds
               NOTIFY performanceMetricsChanged)
    Q_PROPERTY(qulonglong modelPreparationSamples READ modelPreparationSamples
               NOTIFY performanceMetricsChanged)
    Q_PROPERTY(qulonglong sceneGraphUpdateSamples READ sceneGraphUpdateSamples
               NOTIFY performanceMetricsChanged)
    Q_PROPERTY(int visiblePointCount READ visiblePointCount NOTIFY performanceMetricsChanged)
    Q_PROPERTY(int geometryVertexCount READ geometryVertexCount NOTIFY performanceMetricsChanged)
    Q_PROPERTY(qreal renderMilliseconds READ renderMilliseconds NOTIFY renderMillisecondsChanged)
    Q_PROPERTY(QColor mainBoardColor READ mainBoardColor WRITE setMainBoardColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor shenzhenMainColor READ shenzhenMainColor WRITE setShenzhenMainColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor growthBoardColor READ growthBoardColor WRITE setGrowthBoardColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor starMarketColor READ starMarketColor WRITE setStarMarketColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor bseColor READ bseColor WRITE setBseColor NOTIFY colorsChanged)

public:
    explicit CrossSectionScatterItem(QQuickItem *parent = nullptr);

    QAbstractItemModel *model() const;
    void setModel(QAbstractItemModel *model);
    int boardMask() const;
    void setBoardMask(int boardMask);
    bool logarithmic() const;
    void setLogarithmic(bool logarithmic);
    QVariantMap summary() const;
    int pointCount() const;
    bool performanceMetricsEnabled() const;
    void setPerformanceMetricsEnabled(bool enabled);
    qreal modelPreparationMilliseconds() const;
    qreal sceneGraphUpdateMilliseconds() const;
    qulonglong modelPreparationSamples() const;
    qulonglong sceneGraphUpdateSamples() const;
    int visiblePointCount() const;
    int geometryVertexCount() const;
    qreal renderMilliseconds() const;
    static constexpr qreal logicalPointRadius() { return 2.0; }

    QColor mainBoardColor() const;
    void setMainBoardColor(const QColor &color);
    QColor shenzhenMainColor() const;
    void setShenzhenMainColor(const QColor &color);
    QColor growthBoardColor() const;
    void setGrowthBoardColor(const QColor &color);
    QColor starMarketColor() const;
    void setStarMarketColor(const QColor &color);
    QColor bseColor() const;
    void setBseColor(const QColor &color);

    Q_INVOKABLE QVariantMap findNearest(qreal x, qreal y, qreal radius = 10.0) const;
    Q_INVOKABLE QVariantMap pointBySymbol(const QString &symbol) const;
    Q_INVOKABLE QPointF positionForPoint(const QVariantMap &point) const;

signals:
    void modelChanged();
    void boardMaskChanged();
    void logarithmicChanged();
    void summaryChanged();
    void pointCountChanged();
    void performanceMetricsEnabledChanged();
    void performanceMetricsChanged();
    void renderMillisecondsChanged();
    void colorsChanged();
    void snapshotDataChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode,
                             UpdatePaintNodeData *updatePaintNodeData) override;
    void geometryChange(const QRectF &newGeometry,
                        const QRectF &oldGeometry) override;

private:
    void rebuildModelData();
    void rebuildSummary();
    void markGeometryDirty();
    QVariantMap pointMap(const CrossSectionPlotPoint &point) const;
    void recordModelPreparation(qreal milliseconds);
    void recordSceneGraphUpdate(qreal milliseconds, int visiblePointCount,
                                int geometryVertexCount, qulonglong generation);
    void resetPerformanceMetrics();

    QPointer<QAbstractItemModel> m_model;
    QVector<CrossSectionPlotPoint> m_points;
    CrossSectionPlotSummary m_plotSummary;
    QVariantMap m_summary;
    int m_boardMask = 31;
    bool m_logarithmic = true;
    bool m_geometryDirty = true;
    bool m_performanceMetricsEnabled = false;
    qreal m_modelPreparationMilliseconds = -1.0;
    qreal m_sceneGraphUpdateMilliseconds = -1.0;
    qulonglong m_modelPreparationSamples = 0;
    qulonglong m_sceneGraphUpdateSamples = 0;
    int m_visiblePointCount = 0;
    int m_geometryVertexCount = 0;
    qulonglong m_performanceMetricsGeneration = 0;
    qreal m_renderMilliseconds = -1.0;
    QColor m_mainBoardColor = QColor(QStringLiteral("#22d3ee"));
    QColor m_shenzhenMainColor = QColor(QStringLiteral("#4f8cff"));
    QColor m_growthBoardColor = QColor(QStringLiteral("#a78bfa"));
    QColor m_starMarketColor = QColor(QStringLiteral("#f4c95d"));
    QColor m_bseColor = QColor(QStringLiteral("#44d19d"));
};

} // namespace pp
