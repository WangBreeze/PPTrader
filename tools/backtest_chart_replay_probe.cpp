#include "models/CandleListModel.h"
#include "visualization/FrameTimingStatistics.h"
#include "visualization/LocalCandleSeriesItem.h"

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QTimer>

#include <algorithm>

#ifdef Q_OS_LINUX
#include <sys/resource.h>
#endif

namespace {

QVector<pp::CandlePoint> loadHistory(const QString &databasePath,
                                     const QString &symbol, int count,
                                     QString *error)
{
    QVector<pp::CandlePoint> points;
    if (!QFileInfo::exists(databasePath)) {
        *error = QStringLiteral("历史库不存在：%1").arg(databasePath);
        return points;
    }
    QSqlDatabase database = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), QStringLiteral("chart_replay_probe"));
    database.setDatabaseName(databasePath);
    database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!database.open()) {
        *error = database.lastError().text();
        return points;
    }
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "select symbol, name, market, venue, start_time_utc, period_seconds, "
        "open, high, low, close, volume, source "
        "from candles where symbol=? and period_seconds=60 "
        "order by start_time_utc limit ?"));
    query.addBindValue(symbol);
    query.addBindValue(count);
    if (!query.exec()) {
        *error = query.lastError().text();
        return points;
    }
    points.reserve(count);
    while (query.next()) {
        pp::CandlePoint point;
        auto &candle = point.candle;
        candle.instrument.symbol = query.value(0).toString();
        candle.instrument.name = query.value(1).toString();
        candle.instrument.market = pp::Market::AShare;
        candle.instrument.venue = query.value(3).toString();
        candle.startTime = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
        candle.periodSeconds = query.value(5).toInt();
        candle.open = query.value(6).toDouble();
        candle.high = query.value(7).toDouble();
        candle.low = query.value(8).toDouble();
        candle.close = query.value(9).toDouble();
        candle.volume = query.value(10).toDouble();
        candle.source = query.value(11).toString();
        if (!candle.startTime.isValid() || candle.periodSeconds != 60
            || candle.low <= 0.0 || candle.high < candle.low) {
            *error = QStringLiteral("历史 K 线包含无效记录");
            points.clear();
            return points;
        }
        points.append(point);
    }
    return points;
}

qint64 peakResidentBytes()
{
#ifdef Q_OS_LINUX
    rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        return qint64(usage.ru_maxrss) * 1024;
#endif
    return 0;
}

bool saveReport(const QString &path, const QJsonObject &report)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    return file.commit();
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("真实历史分钟 K 线 10 根/秒图表回放探针"));
    parser.addHelpOption();
    QCommandLineOption databaseOption(QStringLiteral("database"),
        QStringLiteral("只读历史 SQLite 数据库"), QStringLiteral("path"));
    QCommandLineOption symbolOption(QStringLiteral("symbol"),
        QStringLiteral("股票代码"), QStringLiteral("code"), QStringLiteral("600519"));
    QCommandLineOption initialOption(QStringLiteral("initial-bars"),
        QStringLiteral("回放前已显示的 K 线数量"), QStringLiteral("count"),
        QStringLiteral("5000"));
    QCommandLineOption replayOption(QStringLiteral("replay-bars"),
        QStringLiteral("逐根回放的 K 线数量"), QStringLiteral("count"),
        QStringLiteral("300"));
    QCommandLineOption speedOption(QStringLiteral("bars-per-second"),
        QStringLiteral("每秒回放根数"), QStringLiteral("rate"),
        QStringLiteral("10"));
    QCommandLineOption outputOption(QStringLiteral("output"),
        QStringLiteral("JSON 报告路径"), QStringLiteral("path"),
        QStringLiteral("backtest-chart-replay-probe.json"));
    parser.addOptions({databaseOption, symbolOption, initialOption, replayOption,
                       speedOption, outputOption});
    parser.process(application);
    bool initialOk = false;
    bool replayOk = false;
    bool speedOk = false;
    const int initialBars = parser.value(initialOption).toInt(&initialOk);
    const int replayBars = parser.value(replayOption).toInt(&replayOk);
    const int speed = parser.value(speedOption).toInt(&speedOk);
    if (!parser.isSet(databaseOption) || !initialOk || !replayOk || !speedOk
        || initialBars < 1 || replayBars < 30 || speed < 1 || speed > 20) {
        QTextStream(stderr) << QStringLiteral("需要 --database，且回放参数必须有效") << Qt::endl;
        return 2;
    }
    QString error;
    const QVector<pp::CandlePoint> history = loadHistory(
        parser.value(databaseOption), parser.value(symbolOption),
        initialBars + replayBars, &error);
    if (!error.isEmpty() || history.size() != initialBars + replayBars) {
        QTextStream(stderr) << (error.isEmpty()
            ? QStringLiteral("可用历史 K 线不足") : error) << Qt::endl;
        return 3;
    }

    pp::CandleListModel model;
    model.setMaximumRows(history.size());
    model.loadPoints(history.mid(0, initialBars));
    QQuickWindow window;
    window.setTitle(QStringLiteral("PPTrader · 历史 K 线回放探针"));
    window.setColor(QColor(QStringLiteral("#08111f")));
    window.resize(1280, 720);
    auto *chart = new pp::LocalCandleSeriesItem(window.contentItem());
    chart->setSize(window.size());
    chart->setShowVolume(true);
    chart->setShowMovingAverages(true);
    chart->setShowBollingerBands(true);
    chart->setShowRsi(true);
    chart->setShowMacd(true);
    chart->setShowSignals(true);
    chart->setModel(&model);
    chart->showLatest(120);

    QVector<qint64> frameIntervals;
    QVector<qint64> appendDurations;
    frameIntervals.reserve(replayBars * 8);
    appendDurations.reserve(replayBars);
    QElapsedTimer clock;
    clock.start();
    qint64 previousSwap = -1;
    bool replayStarted = false;
    bool finished = false;
    int revealed = 0;
    QString sceneGraphError;
    QTimer frameTimer;
    frameTimer.setTimerType(Qt::PreciseTimer);
    frameTimer.setInterval(16);
    QObject::connect(&frameTimer, &QTimer::timeout, &window,
                     [&window] { window.update(); });
    QTimer replayTimer;
    replayTimer.setTimerType(Qt::PreciseTimer);
    replayTimer.setInterval(1000 / speed);
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&] {
        const qint64 now = clock.nsecsElapsed();
        if (replayStarted && previousSwap >= 0)
            frameIntervals.append(now - previousSwap);
        previousSwap = now;
    });
    QObject::connect(&window, &QQuickWindow::sceneGraphError, &window,
        [&](QQuickWindow::SceneGraphError, const QString &message) {
            sceneGraphError = message;
        });
    auto finish = [&](bool timedOut) {
        if (finished) return;
        finished = true;
        frameTimer.stop();
        replayTimer.stop();
        const auto frames = pp::summarizeFrameTimings(frameIntervals);
        const auto appends = pp::summarizeFrameTimings(appendDurations);
        const bool valid = !timedOut && sceneGraphError.isEmpty()
            && revealed == replayBars && model.count() == history.size()
            && chart->pointCount() == history.size()
            && frames.sampleCount >= replayBars;
        const QJsonObject report {
            {QStringLiteral("status"), valid ? QStringLiteral("completed")
                                             : QStringLiteral("failed")},
            {QStringLiteral("dataSource"), QStringLiteral("SQLite candles, read-only")},
            {QStringLiteral("database"), parser.value(databaseOption)},
            {QStringLiteral("symbol"), parser.value(symbolOption)},
            {QStringLiteral("firstBarUtc"), history.first().candle.startTime.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("lastBarUtc"), history.last().candle.startTime.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("initialBars"), initialBars},
            {QStringLiteral("replayedBars"), revealed},
            {QStringLiteral("barsPerSecond"), speed},
            {QStringLiteral("visibleBars"), chart->visibleCount()},
            {QStringLiteral("frameIntervals"), frames.toJson()},
            {QStringLiteral("appendAndViewportCpu"), appends.toJson()},
            {QStringLiteral("peakResidentBytes"), peakResidentBytes()},
            {QStringLiteral("fullIndicatorRebuilds"), qint64(chart->fullIndicatorRebuildCount())},
            {QStringLiteral("incrementalIndicatorUpdates"), qint64(chart->incrementalIndicatorUpdateCount())},
            {QStringLiteral("timedOut"), timedOut},
            {QStringLiteral("sceneGraphError"), sceneGraphError},
            {QStringLiteral("measurementScope"), QStringLiteral(
                "帧交换间隔包含调度与渲染；16 ms 刷新定时器加 10 根/秒真实分钟 K 线，不含回测计算和归档读取")}
        };
        const bool saved = saveReport(parser.value(outputOption), report);
        QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact)
                            << Qt::endl;
        application.exit(valid && saved ? 0 : 4);
    };
    QObject::connect(&replayTimer, &QTimer::timeout, &window, [&] {
        QElapsedTimer appendClock;
        appendClock.start();
        model.addPoint(history.at(initialBars + revealed));
        chart->showLatest(120);
        appendDurations.append(appendClock.nsecsElapsed());
        ++revealed;
        if (revealed == replayBars) {
            replayTimer.stop();
            QTimer::singleShot(250, &window, [&] { finish(false); });
        }
    });
    QTimer::singleShot(1000, &window, [&] {
        previousSwap = -1;
        replayStarted = true;
        frameTimer.start();
        replayTimer.start();
    });
    QTimer::singleShot((replayBars * 1000 / speed) + 10000, &window,
                       [&] { finish(true); });
    window.show();
    return application.exec();
}
