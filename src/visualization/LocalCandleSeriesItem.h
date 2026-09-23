#pragma once

#include "visualization/LocalCandlePlotData.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QPointer>
#include <QQuickItem>
#include <QVariantList>
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
    Q_PROPERTY(int renderBucketCount READ renderBucketCount NOTIFY geometryChanged)
    Q_PROPERTY(bool levelOfDetailActive READ levelOfDetailActive NOTIFY geometryChanged)
    Q_PROPERTY(qreal averageBarsPerRenderBucket READ averageBarsPerRenderBucket
                   NOTIFY geometryChanged)
    Q_PROPERTY(bool viewportAtLeftEdge READ viewportAtLeftEdge NOTIFY viewportChanged)
    Q_PROPERTY(bool viewportAtRightEdge READ viewportAtRightEdge NOTIFY viewportChanged)
    Q_PROPERTY(qreal priceScaleFactor READ priceScaleFactor NOTIFY priceScaleChanged)
    Q_PROPERTY(bool automaticPriceRange READ automaticPriceRange NOTIFY priceScaleChanged)
    Q_PROPERTY(int geometryVertexCount READ geometryVertexCount NOTIFY geometryChanged)
    Q_PROPERTY(int signalCount READ signalCount NOTIFY signalCountChanged)
    Q_PROPERTY(bool showSignals READ showSignals WRITE setShowSignals NOTIFY showSignalsChanged)
    Q_PROPERTY(bool showVolume READ showVolume WRITE setShowVolume NOTIFY overlayOptionsChanged)
    Q_PROPERTY(bool showMovingAverages READ showMovingAverages WRITE setShowMovingAverages NOTIFY overlayOptionsChanged)
    Q_PROPERTY(bool showBollingerBands READ showBollingerBands WRITE setShowBollingerBands NOTIFY overlayOptionsChanged)
    Q_PROPERTY(bool showRsi READ showRsi WRITE setShowRsi NOTIFY overlayOptionsChanged)
    Q_PROPERTY(bool showMacd READ showMacd WRITE setShowMacd NOTIFY overlayOptionsChanged)
    Q_PROPERTY(int movingAverageFastPeriod READ movingAverageFastPeriod WRITE setMovingAverageFastPeriod NOTIFY indicatorParametersChanged)
    Q_PROPERTY(int movingAverageSlowPeriod READ movingAverageSlowPeriod WRITE setMovingAverageSlowPeriod NOTIFY indicatorParametersChanged)
    Q_PROPERTY(int bollingerPeriod READ bollingerPeriod WRITE setBollingerPeriod NOTIFY indicatorParametersChanged)
    Q_PROPERTY(qreal bollingerStandardDeviations READ bollingerStandardDeviations WRITE setBollingerStandardDeviations NOTIFY indicatorParametersChanged)
    Q_PROPERTY(int rsiPeriod READ rsiPeriod WRITE setRsiPeriod NOTIFY indicatorParametersChanged)
    Q_PROPERTY(int macdFastPeriod READ macdFastPeriod WRITE setMacdFastPeriod NOTIFY indicatorParametersChanged)
    Q_PROPERTY(int macdSlowPeriod READ macdSlowPeriod WRITE setMacdSlowPeriod NOTIFY indicatorParametersChanged)
    Q_PROPERTY(int macdSignalPeriod READ macdSignalPeriod WRITE setMacdSignalPeriod NOTIFY indicatorParametersChanged)
    Q_PROPERTY(qreal pricePlotRatio READ pricePlotRatio NOTIFY overlayOptionsChanged)
    Q_PROPERTY(qreal volumePlotTop READ volumePlotTop NOTIFY overlayOptionsChanged)
    Q_PROPERTY(qreal volumePlotRatio READ volumePlotRatio NOTIFY overlayOptionsChanged)
    Q_PROPERTY(qreal rsiPlotTop READ rsiPlotTop NOTIFY overlayOptionsChanged)
    Q_PROPERTY(qreal rsiPlotRatio READ rsiPlotRatio NOTIFY overlayOptionsChanged)
    Q_PROPERTY(qreal macdPlotTop READ macdPlotTop NOTIFY overlayOptionsChanged)
    Q_PROPERTY(qreal macdPlotRatio READ macdPlotRatio NOTIFY overlayOptionsChanged)
    Q_PROPERTY(double maximumVolume READ maximumVolume NOTIFY axisTicksChanged)
    Q_PROPERTY(int volumeVertexCount READ volumeVertexCount NOTIFY geometryChanged)
    Q_PROPERTY(int indicatorVertexCount READ indicatorVertexCount NOTIFY geometryChanged)
    Q_PROPERTY(int oscillatorVertexCount READ oscillatorVertexCount NOTIFY geometryChanged)
    Q_PROPERTY(qulonglong fullIndicatorRebuildCount READ fullIndicatorRebuildCount
                   NOTIFY indicatorCacheStatisticsChanged)
    Q_PROPERTY(qulonglong incrementalIndicatorUpdateCount
                   READ incrementalIndicatorUpdateCount
                   NOTIFY indicatorCacheStatisticsChanged)
    Q_PROPERTY(QVariantList priceAxisTicks READ priceAxisTicks NOTIFY axisTicksChanged)
    Q_PROPERTY(QVariantList timeAxisTicks READ timeAxisTicks NOTIFY axisTicksChanged)
    Q_PROPERTY(QVariantList macdAxisTicks READ macdAxisTicks NOTIFY axisTicksChanged)
    Q_PROPERTY(QColor risingColor READ risingColor WRITE setRisingColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor fallingColor READ fallingColor WRITE setFallingColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor wickColor READ wickColor WRITE setWickColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor buySignalColor READ buySignalColor WRITE setBuySignalColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor sellSignalColor READ sellSignalColor WRITE setSellSignalColor NOTIFY colorsChanged)

public:
    explicit LocalCandleSeriesItem(QQuickItem *parent = nullptr);
    QAbstractItemModel *model() const;
    void setModel(QAbstractItemModel *model);
    int pointCount() const;
    double minimumPrice() const;
    double maximumPrice() const;
    int visibleStart() const;
    int visibleCount() const;
    int renderBucketCount() const;
    bool levelOfDetailActive() const;
    qreal averageBarsPerRenderBucket() const;
    bool viewportAtLeftEdge() const;
    bool viewportAtRightEdge() const;
    qreal priceScaleFactor() const;
    bool automaticPriceRange() const;
    int geometryVertexCount() const;
    int signalCount() const;
    bool showSignals() const;
    bool showVolume() const;
    bool showMovingAverages() const;
    bool showBollingerBands() const;
    bool showRsi() const;
    bool showMacd() const;
    int movingAverageFastPeriod() const;
    int movingAverageSlowPeriod() const;
    int bollingerPeriod() const;
    qreal bollingerStandardDeviations() const;
    int rsiPeriod() const;
    int macdFastPeriod() const;
    int macdSlowPeriod() const;
    int macdSignalPeriod() const;
    qreal pricePlotRatio() const;
    qreal volumePlotTop() const;
    qreal volumePlotRatio() const;
    qreal rsiPlotTop() const;
    qreal rsiPlotRatio() const;
    qreal macdPlotTop() const;
    qreal macdPlotRatio() const;
    double maximumVolume() const;
    int volumeVertexCount() const;
    int indicatorVertexCount() const;
    int oscillatorVertexCount() const;
    qulonglong fullIndicatorRebuildCount() const;
    qulonglong incrementalIndicatorUpdateCount() const;
    QVariantList priceAxisTicks() const;
    QVariantList timeAxisTicks() const;
    QVariantList macdAxisTicks() const;
    QColor risingColor() const;
    QColor fallingColor() const;
    QColor wickColor() const;
    QColor buySignalColor() const;
    QColor sellSignalColor() const;
    void setShowSignals(bool showSignals);
    void setShowVolume(bool showVolume);
    void setShowMovingAverages(bool showMovingAverages);
    void setShowBollingerBands(bool showBollingerBands);
    void setShowRsi(bool showRsi);
    void setShowMacd(bool showMacd);
    void setMovingAverageFastPeriod(int period);
    void setMovingAverageSlowPeriod(int period);
    void setBollingerPeriod(int period);
    void setBollingerStandardDeviations(qreal standardDeviations);
    void setRsiPeriod(int period);
    void setMacdFastPeriod(int period);
    void setMacdSlowPeriod(int period);
    void setMacdSignalPeriod(int period);
    void setRisingColor(const QColor &color);
    void setFallingColor(const QColor &color);
    void setWickColor(const QColor &color);
    void setBuySignalColor(const QColor &color);
    void setSellSignalColor(const QColor &color);
    Q_INVOKABLE QVariantMap findNearest(qreal x) const;
    Q_INVOKABLE double priceAt(qreal y) const;
    Q_INVOKABLE void zoomAt(qreal x, qreal factor);
    Q_INVOKABLE void panByPixels(qreal deltaX);
    Q_INVOKABLE void resetViewport();
    Q_INVOKABLE void scrollToLatest();
    Q_INVOKABLE void scalePriceRange(qreal factor);
    Q_INVOKABLE void resetPriceRange();
    Q_INVOKABLE QString pointTextAt(qreal x) const;
    Q_INVOKABLE bool copyNearestToClipboard(qreal x) const;

signals:
    void modelChanged();
    void pointCountChanged();
    void rangeChanged();
    void geometryChanged();
    void colorsChanged();
    void viewportChanged();
    void priceScaleChanged();
    void signalCountChanged();
    void showSignalsChanged();
    void overlayOptionsChanged();
    void indicatorParametersChanged();
    void indicatorCacheStatisticsChanged();
    void axisTicksChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void rebuildModelData();
    bool updateTypedModelRows(int first, int last);
    bool appendTypedModelRows(int first, int last);
    void finishModelDataChange(int previousCount, int previousSignalCount,
                               const LocalCandleViewport &previousViewport,
                               bool wasAtRightEdge,
                               const LocalCandlePlotRange &previousRange,
                               int derivedStart);
    void markGeometryDirty();
    void applyViewport(const LocalCandleViewport &viewport);
    void rebuildDerivedData();
    void rebuildDerivedDataFrom(int first);
    void rebuildRsiCache(int first);
    void rebuildMacdCache(int first);
    void indicatorParametersDidChange();
    void recalculatePriceRange();
    void rebuildAxisTicks();
    QVariantMap pointMap(int index) const;

    struct SignalVisual {
        bool present = false;
        bool buy = false;
        double price = 0.0;
        QString side;
        QString reason;
    };

    QPointer<QAbstractItemModel> m_model;
    QVector<LocalCandleSeriesPoint> m_points;
    QVector<SignalVisual> m_signals;
    QVector<double> m_smaFast;
    QVector<double> m_smaSlow;
    LocalCandleBollingerBands m_bollinger;
    QVector<double> m_rsi;
    QVector<double> m_rsiAverageGain;
    QVector<double> m_rsiAverageLoss;
    LocalCandleMacdValues m_macd;
    QVector<double> m_macdFastEma;
    QVector<double> m_macdSlowEma;
    LocalCandlePlotRange m_range;
    LocalCandlePlotRange m_macdRange;
    LocalCandleViewport m_viewport;
    qreal m_priceScaleFactor = 1.0;
    int m_pendingPrependedRows = 0;
    bool m_geometryDirty = true;
    int m_geometryVertexCount = 0;
    int m_renderBucketCount = 0;
    int m_volumeVertexCount = 0;
    int m_indicatorVertexCount = 0;
    int m_oscillatorVertexCount = 0;
    int m_signalCount = 0;
    qulonglong m_fullIndicatorRebuildCount = 0;
    qulonglong m_incrementalIndicatorUpdateCount = 0;
    bool m_showSignals = true;
    bool m_showVolume = false;
    bool m_showMovingAverages = false;
    bool m_showBollingerBands = false;
    bool m_showRsi = false;
    bool m_showMacd = false;
    int m_movingAverageFastPeriod = 5;
    int m_movingAverageSlowPeriod = 20;
    int m_bollingerPeriod = 20;
    qreal m_bollingerStandardDeviations = 2.0;
    int m_rsiPeriod = 14;
    int m_macdFastPeriod = 12;
    int m_macdSlowPeriod = 26;
    int m_macdSignalPeriod = 9;
    double m_maximumVolume = 0.0;
    QVariantList m_priceAxisTicks;
    QVariantList m_timeAxisTicks;
    QVariantList m_macdAxisTicks;
    QColor m_risingColor = QColor(QStringLiteral("#ef5350"));
    QColor m_fallingColor = QColor(QStringLiteral("#26a69a"));
    QColor m_wickColor = QColor(QStringLiteral("#7f8ca5"));
    QColor m_buySignalColor = QColor(QStringLiteral("#26a69a"));
    QColor m_sellSignalColor = QColor(QStringLiteral("#ef5350"));
};

} // namespace pp
