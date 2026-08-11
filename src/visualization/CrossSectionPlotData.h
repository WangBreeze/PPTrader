#pragma once

#include <QDateTime>
#include <QPointF>
#include <QString>
#include <QVector>

namespace pp {

struct CrossSectionPlotPoint {
    int displayIndex = 0;
    QString symbol;
    QString name;
    QString board;
    double price = 0.0;
    double previousClose = 0.0;
    double changePercent = 0.0;
    double volume = 0.0;
    double amount = 0.0;
    QDateTime snapshotTime;
    bool valid = false;
    bool stale = false;
    int boardBit = 0;
};

struct CrossSectionPlotSummary {
    int validCount = 0;
    int invalidCount = 0;
    double minimum = 0.0;
    double maximum = 0.0;
    double average = 0.0;
    double median = 0.0;
    QVector<int> topIndices;
    int mainBoardCount = 0;
    int shenzhenMainCount = 0;
    int growthBoardCount = 0;
    int starMarketCount = 0;
    int bseCount = 0;
    int unknownCount = 0;
};

class CrossSectionPlotData final {
public:
    enum BoardBit {
        MainBoard = 1,
        ShenzhenMain = 2,
        GrowthBoard = 4,
        StarMarket = 8,
        Bse = 16
    };

    static int boardBit(const QString &board);
    static bool isVisible(const CrossSectionPlotPoint &point, int boardMask);
    static qreal xFor(const CrossSectionPlotPoint &point, int totalCount, qreal width);
    static qreal yFor(double price, bool logarithmic, double maximum,
                      qreal height);
    static CrossSectionPlotSummary summarize(const QVector<CrossSectionPlotPoint> &points,
                                              int boardMask);
    static int nearestPoint(const QVector<CrossSectionPlotPoint> &points,
                            int boardMask, bool logarithmic, double maximum,
                            qreal width, qreal height, const QPointF &position,
                            qreal radius);
};

} // namespace pp
