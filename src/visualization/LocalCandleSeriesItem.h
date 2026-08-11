#pragma once

#include "visualization/LocalCandlePlotData.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QPointer>
#include <QQuickItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace pp {

class LocalCandleSeriesItem : public QQuickItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(LocalCandleSeriesItem)
    Q_PROPERTY(QAbstractItemModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY pointCountChanged)
    Q_PROPERTY(double minimumPrice READ minimumPrice NOTIFY rangeChanged)
    Q_PROPERTY(double maximumPrice READ maximumPrice NOTIFY rangeChanged)
    Q_PROPERTY(int visibleStart READ visibleStart NOTIFY viewportChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY viewportChanged)
    Q_PROPERTY(bool viewportAtLeftEdge READ viewportAtLeftEdge NOTIFY viewportChanged)
    Q_PROPERTY(int geometryVertexCount READ geometryVertexCount NOTIFY geometryChanged)
    Q_PROPERTY(QColor risingColor READ risingColor WRITE setRisingColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor fallingColor READ fallingColor WRITE setFallingColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor wickColor READ wickColor WRITE setWickColor NOTIFY colorsChanged)

public:
    explicit LocalCandleSeriesItem(QQuickItem *parent = nullptr);
    QAbstractItemModel *model() const;
    void setModel(QAbstractItemModel *model);
    int pointCount() const;
    double minimumPrice() const;
    double maximumPrice() const;
    int visibleStart() const;
    int visibleCount() const;
    bool viewportAtLeftEdge() const;
    int geometryVertexCount() const;
    QColor risingColor() const;
    QColor fallingColor() const;
    QColor wickColor() const;
    void setRisingColor(const QColor &color);
    void setFallingColor(const QColor &color);
    void setWickColor(const QColor &color);
    Q_INVOKABLE QVariantMap findNearest(qreal x) const;
    Q_INVOKABLE double priceAt(qreal y) const;
    Q_INVOKABLE void zoomAt(qreal x, qreal factor);
    Q_INVOKABLE void panByPixels(qreal deltaX);
    Q_INVOKABLE void resetViewport();

signals:
    void modelChanged();
    void pointCountChanged();
    void rangeChanged();
    void geometryChanged();
    void colorsChanged();
    void viewportChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void rebuildModelData();
    void markGeometryDirty();
    void applyViewport(const LocalCandleViewport &viewport);
    QVariantMap pointMap(const LocalCandleSeriesPoint &point) const;

    QPointer<QAbstractItemModel> m_model;
    QVector<LocalCandleSeriesPoint> m_points;
    LocalCandlePlotRange m_range;
    LocalCandleViewport m_viewport;
    bool m_geometryDirty = true;
    int m_geometryVertexCount = 0;
    QColor m_risingColor = QColor(QStringLiteral("#ef5350"));
    QColor m_fallingColor = QColor(QStringLiteral("#26a69a"));
    QColor m_wickColor = QColor(QStringLiteral("#7f8ca5"));
};

} // namespace pp
