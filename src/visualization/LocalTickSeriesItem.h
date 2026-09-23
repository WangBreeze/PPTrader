#pragma once

#include "visualization/LocalTickPlotData.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QPointer>
#include <QQuickItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace pp {

class LocalTickSeriesItem : public QQuickItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(LocalTickSeriesItem)
    Q_PROPERTY(QAbstractItemModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY pointCountChanged)
    Q_PROPERTY(int visibleStart READ visibleStart NOTIFY viewportChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY viewportChanged)
    Q_PROPERTY(double minimumPrice READ minimumPrice NOTIFY rangeChanged)
    Q_PROPERTY(double maximumPrice READ maximumPrice NOTIFY rangeChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor buyColor READ buyColor WRITE setBuyColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor sellColor READ sellColor WRITE setSellColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor neutralColor READ neutralColor WRITE setNeutralColor NOTIFY colorsChanged)
public:
    explicit LocalTickSeriesItem(QQuickItem *parent = nullptr);
    QAbstractItemModel *model() const;
    void setModel(QAbstractItemModel *model);
    int pointCount() const;
    int visibleStart() const;
    int visibleCount() const;
    double minimumPrice() const;
    double maximumPrice() const;
    QColor lineColor() const; QColor buyColor() const; QColor sellColor() const; QColor neutralColor() const;
    void setLineColor(const QColor &value); void setBuyColor(const QColor &value);
    void setSellColor(const QColor &value); void setNeutralColor(const QColor &value);
    Q_INVOKABLE QVariantMap findNearest(qreal x) const;
    Q_INVOKABLE double priceAt(qreal y) const;
    Q_INVOKABLE void zoomAt(qreal x, qreal factor);
    Q_INVOKABLE void panByPixels(qreal deltaX);
    Q_INVOKABLE void resetViewport();
signals:
    void modelChanged(); void pointCountChanged(); void viewportChanged();
    void rangeChanged(); void colorsChanged();
protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
private:
    void rebuildModelData(); void markDirty(); void applyViewport(const LocalTickViewport &viewport);
    QVariantMap pointMap(const LocalTickSeriesPoint &point) const;
    QPointer<QAbstractItemModel> m_model;
    QVector<LocalTickSeriesPoint> m_points;
    LocalTickViewport m_viewport;
    LocalTickPlotRange m_range;
    bool m_dirty = true;
    QColor m_lineColor = QColor(QStringLiteral("#7dd3fc"));
    QColor m_buyColor = QColor(QStringLiteral("#ef5350"));
    QColor m_sellColor = QColor(QStringLiteral("#26a69a"));
    QColor m_neutralColor = QColor(QStringLiteral("#94a3b8"));
};

} // namespace pp
