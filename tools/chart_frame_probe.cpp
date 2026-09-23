#include "models/LocalCandleSeriesListModel.h"
#include "visualization/FrameTimingStatistics.h"
#include "visualization/LocalCandleSeriesItem.h"

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QSaveFile>
#include <QScreen>
#include <QSGRendererInterface>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace {

QString graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Unknown: return QStringLiteral("unknown");
    case QSGRendererInterface::Software: return QStringLiteral("software");
    case QSGRendererInterface::OpenVG: return QStringLiteral("openvg");
    case QSGRendererInterface::OpenGL: return QStringLiteral("opengl");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("direct3d11");
    case QSGRendererInterface::Vulkan: return QStringLiteral("vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("metal");
    case QSGRendererInterface::Null: return QStringLiteral("null");
    }
    return QStringLiteral("unrecognized");
}

QJsonObject rectJson(const QRect &rect)
{
    return {{QStringLiteral("x"), rect.x()},
            {QStringLiteral("y"), rect.y()},
            {QStringLiteral("width"), rect.width()},
            {QStringLiteral("height"), rect.height()}};
}

QVector<pp::LocalCandleSeriesPoint> makeSeries(int pointCount)
{
    QVector<pp::LocalCandleSeriesPoint> points;
    points.reserve(pointCount + 64);
    const QDateTime firstTime(
        QDate(2025, 1, 2), QTime(9, 30), QTimeZone::utc());
    double previousClose = 10.0;
    for (int index = 0; index < pointCount; ++index) {
        const double wave = std::sin(double(index) * 0.031) * 0.08;
        const double drift = double(index % 997) * 0.00002;
        pp::LocalCandleSeriesPoint point;
        point.candle.instrument = {
            QStringLiteral("600519"), QStringLiteral("帧探针样本"),
            pp::Market::AShare, QStringLiteral("SSE")};
        point.candle.startTime = firstTime.addSecs(qint64(index) * 60);
        point.candle.periodSeconds = 60;
        point.candle.open = previousClose;
        point.candle.close = std::max(1.0, previousClose + wave * 0.12 + drift);
        point.candle.high = std::max(point.candle.open, point.candle.close)
            + 0.04 + std::abs(wave) * 0.2;
        point.candle.low = std::min(point.candle.open, point.candle.close)
            - 0.04 - std::abs(wave) * 0.15;
        point.candle.volume = 8000.0 + double((index * 7919) % 50000);
        point.candle.buyVolume = point.candle.volume * 0.48;
        point.candle.sellVolume = point.candle.volume * 0.44;
        point.candle.neutralVolume = point.candle.volume * 0.08;
        point.candle.directionalVolumeKnown = true;
        point.candle.source = QStringLiteral("controlled_frame_probe");
        point.endTime = point.candle.startTime.addSecs(60);
        point.sourceCount = 1;
        points.append(point);
        previousClose = point.candle.close;
    }
    return points;
}

bool writeJson(const QString &path, const QJsonObject &report, QString *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) *error = QStringLiteral("无法创建报告目录：%1").arg(info.absolutePath());
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法写入报告：%1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = QStringLiteral("无法提交报告：%1").arg(file.errorString());
        return false;
    }
    return true;
}

int positiveOption(const QCommandLineParser &parser, const QCommandLineOption &option,
                   int fallback, int minimum, int maximum, QString *error)
{
    bool ok = false;
    const int value = parser.value(option).toInt(&ok);
    if (!ok || value < minimum || value > maximum) {
        if (error) {
            *error = QStringLiteral("参数 --%1 必须在 %2..%3 之间")
                .arg(option.names().first()).arg(minimum).arg(maximum);
        }
        return fallback;
    }
    return value;
}

pp::ProcessMemorySample currentProcessMemory(qint64 elapsedMilliseconds)
{
    pp::ProcessMemorySample sample;
    sample.elapsedMilliseconds = elapsedMilliseconds;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters {};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
            sizeof(counters))) {
        sample.workingSetBytes = qint64(counters.WorkingSetSize);
        sample.privateBytes = qint64(counters.PrivateUsage);
    }
#endif
    return sample;
}

QJsonObject memorySampleJson(const pp::ProcessMemorySample &sample)
{
    return {
        {QStringLiteral("elapsedMilliseconds"), sample.elapsedMilliseconds},
        {QStringLiteral("workingSetBytes"), sample.workingSetBytes},
        {QStringLiteral("privateBytes"), sample.privateBytes}
    };
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("pptrader_chart_frame_probe"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "PPTrader QQuickWindow / Scene Graph 端到端交换帧探针"));
    parser.addHelpOption();
    QCommandLineOption pointsOption(
        QStringLiteral("points"), QStringLiteral("序列总点数"),
        QStringLiteral("count"), QStringLiteral("50000"));
    QCommandLineOption visibleOption(
        QStringLiteral("visible-points"), QStringLiteral("初始可见点数"),
        QStringLiteral("count"), QStringLiteral("12000"));
    QCommandLineOption framesOption(
        QStringLiteral("frames"), QStringLiteral("计入统计的交换帧数"),
        QStringLiteral("count"), QStringLiteral("600"));
    QCommandLineOption warmupOption(
        QStringLiteral("warmup-frames"), QStringLiteral("预热交换帧数"),
        QStringLiteral("count"), QStringLiteral("60"));
    QCommandLineOption durationOption(
        QStringLiteral("duration-seconds"),
        QStringLiteral("按时长运行；0 表示按交换帧数结束"),
        QStringLiteral("seconds"), QStringLiteral("0"));
    QCommandLineOption memoryIntervalOption(
        QStringLiteral("memory-sample-interval-seconds"),
        QStringLiteral("进程内存采样间隔"),
        QStringLiteral("seconds"), QStringLiteral("5"));
    QCommandLineOption progressIntervalOption(
        QStringLiteral("progress-interval-seconds"),
        QStringLiteral("控制台进度输出间隔"),
        QStringLiteral("seconds"), QStringLiteral("60"));
    QCommandLineOption refreshOption(
        QStringLiteral("tail-refresh-interval"),
        QStringLiteral("每多少次交互更新一次尾部数据"),
        QStringLiteral("actions"), QStringLiteral("30"));
    QCommandLineOption widthOption(
        QStringLiteral("width"), QStringLiteral("窗口宽度"),
        QStringLiteral("pixels"), QStringLiteral("1600"));
    QCommandLineOption heightOption(
        QStringLiteral("height"), QStringLiteral("窗口高度"),
        QStringLiteral("pixels"), QStringLiteral("900"));
    QCommandLineOption outputOption(
        QStringLiteral("output"), QStringLiteral("JSON 报告路径"),
        QStringLiteral("path"), QStringLiteral("chart-frame-probe.json"));
    parser.addOptions({pointsOption, visibleOption, framesOption, warmupOption,
                       durationOption, memoryIntervalOption,
                       progressIntervalOption, refreshOption, widthOption,
                       heightOption, outputOption});
    parser.process(application);

    QString optionError;
    const int pointCount = positiveOption(
        parser, pointsOption, 50000, 100, 1'000'000, &optionError);
    int initialVisible = positiveOption(
        parser, visibleOption, 12000, 10, 1'000'000, &optionError);
    const int requestedFrames = positiveOption(
        parser, framesOption, 600, 30, 100'000, &optionError);
    const int warmupFrames = positiveOption(
        parser, warmupOption, 60, 0, 10'000, &optionError);
    const int durationSeconds = positiveOption(
        parser, durationOption, 0, 0, 86'400, &optionError);
    const int memorySampleIntervalSeconds = positiveOption(
        parser, memoryIntervalOption, 5, 1, 300, &optionError);
    const int progressIntervalSeconds = positiveOption(
        parser, progressIntervalOption, 60, 1, 3'600, &optionError);
    const int refreshInterval = positiveOption(
        parser, refreshOption, 30, 1, 10'000, &optionError);
    const int requestedWidth = positiveOption(
        parser, widthOption, 1600, 320, 10000, &optionError);
    const int requestedHeight = positiveOption(
        parser, heightOption, 900, 240, 10000, &optionError);
    if (!optionError.isEmpty()) {
        QTextStream(stderr) << optionError << Qt::endl;
        return 2;
    }
    initialVisible = std::min(initialVisible, pointCount);

    const QList<QScreen *> screens = QGuiApplication::screens();
    QScreen *targetScreen = QGuiApplication::primaryScreen();
    bool usedSecondaryScreen = false;
    for (QScreen *screen : screens) {
        if (screen != QGuiApplication::primaryScreen()) {
            targetScreen = screen;
            usedSecondaryScreen = true;
            break;
        }
    }
    if (!targetScreen) {
        QTextStream(stderr) << QStringLiteral("没有可用屏幕") << Qt::endl;
        return 3;
    }

    const QRect available = targetScreen->availableGeometry();
    const QSize windowSize(std::min(requestedWidth, available.width()),
                           std::min(requestedHeight, available.height()));
    const QPoint position(
        available.x() + (available.width() - windowSize.width()) / 2,
        available.y() + (available.height() - windowSize.height()) / 2);

    pp::LocalCandleSeriesListModel model;
    QVector<pp::LocalCandleSeriesPoint> points = makeSeries(pointCount);
    QElapsedTimer preparationTimer;
    preparationTimer.start();
    model.setPoints(points);

    QQuickWindow window;
    window.setTitle(QStringLiteral("PPTrader · Scene Graph 帧探针"));
    window.setColor(QColor(QStringLiteral("#08111f")));
    window.setPersistentGraphics(true);
    window.setPersistentSceneGraph(true);
    window.setScreen(targetScreen);
    window.setGeometry(QRect(position, windowSize));

    auto *chart = new pp::LocalCandleSeriesItem(window.contentItem());
    chart->setSize(QSizeF(windowSize));
    chart->setShowVolume(true);
    chart->setShowMovingAverages(true);
    chart->setShowBollingerBands(true);
    chart->setShowRsi(true);
    chart->setShowMacd(true);
    chart->setRisingColor(QColor(QStringLiteral("#f05454")));
    chart->setFallingColor(QColor(QStringLiteral("#21c38b")));
    chart->setModel(&model);
    if (initialVisible < pointCount)
        chart->zoomAt(chart->width(), double(pointCount) / initialVisible);
    chart->scrollToLatest();
    const qint64 preparationNanoseconds = preparationTimer.nsecsElapsed();

    QVector<qint64> frameIntervals;
    QVector<qint64> interactionDurations;
    QVector<qint64> tailRefreshDurations;
    const int estimatedFrameCount = durationSeconds > 0
        ? std::min(2'000'000, durationSeconds * 150)
        : requestedFrames;
    const int estimatedActionCount = durationSeconds > 0
        ? std::min(2'000'000, durationSeconds * 130)
        : requestedFrames * 2;
    frameIntervals.reserve(estimatedFrameCount);
    interactionDurations.reserve(estimatedActionCount);
    tailRefreshDurations.reserve(estimatedActionCount / refreshInterval + 2);
    QVector<pp::ProcessMemorySample> memorySamples;
    memorySamples.reserve(durationSeconds > 0
        ? durationSeconds / memorySampleIntervalSeconds + 4 : 16);
    QElapsedTimer probeClock;
    probeClock.start();
    qint64 previousSwapNanoseconds = -1;
    int swappedFrames = 0;
    int actionCount = 0;
    int appendedBars = 0;
    bool finishing = false;
    QString sceneGraphError;

    QTimer actionTimer;
    actionTimer.setTimerType(Qt::PreciseTimer);
    actionTimer.setInterval(8);
    QTimer memoryTimer;
    memoryTimer.setTimerType(Qt::CoarseTimer);
    memoryTimer.setInterval(memorySampleIntervalSeconds * 1000);
    QTimer progressTimer;
    progressTimer.setTimerType(Qt::CoarseTimer);
    progressTimer.setInterval(progressIntervalSeconds * 1000);
    QTimer durationTimer;
    durationTimer.setTimerType(Qt::PreciseTimer);
    durationTimer.setSingleShot(true);
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    auto sampleMemory = [&] {
        const pp::ProcessMemorySample sample = currentProcessMemory(probeClock.elapsed());
        if (!memorySamples.isEmpty()
            && memorySamples.last().elapsedMilliseconds == sample.elapsedMilliseconds) {
            memorySamples.last() = sample;
        } else {
            memorySamples.append(sample);
        }
    };

    QObject::connect(&memoryTimer, &QTimer::timeout, &window, sampleMemory);
    QObject::connect(&progressTimer, &QTimer::timeout, &window, [&] {
        sampleMemory();
        const pp::ProcessMemorySample memory = memorySamples.last();
        QTextStream(stdout)
            << QStringLiteral("PROGRESS elapsed=%1s frames=%2 actions=%3 tailRefreshes=%4 workingSetMiB=%5 privateMiB=%6")
                   .arg(probeClock.elapsed() / 1000)
                   .arg(frameIntervals.size())
                   .arg(actionCount)
                   .arg(tailRefreshDurations.size())
                   .arg(double(memory.workingSetBytes) / (1024.0 * 1024.0), 0, 'f', 1)
                   .arg(double(memory.privateBytes) / (1024.0 * 1024.0), 0, 'f', 1)
            << Qt::endl;
    });
    QObject::connect(&actionTimer, &QTimer::timeout, &window, [&] {
        QElapsedTimer interactionTimer;
        interactionTimer.start();
        const qreal anchor = qreal((actionCount % 17) + 1) / 18.0;
        constexpr qreal zoomFactor = 1.006;
        chart->zoomAt(chart->width() * anchor,
                      actionCount % 2 == 0 ? zoomFactor : 1.0 / zoomFactor);
        chart->panByPixels(actionCount % 4 < 2 ? 9.0 : -7.0);
        chart->scalePriceRange(actionCount % 2 == 0 ? 1.0006 : 1.0 / 1.0006);
        interactionDurations.append(interactionTimer.nsecsElapsed());

        ++actionCount;
        if (actionCount % refreshInterval != 0) return;
        pp::LocalCandleSeriesPoint &last = points.last();
        last.candle.close += (actionCount / refreshInterval) % 2 == 0 ? 0.01 : -0.006;
        last.candle.high = std::max(last.candle.high, last.candle.close);
        last.candle.low = std::min(last.candle.low, last.candle.close);
        last.candle.volume += 100.0;
        if ((actionCount / refreshInterval) % 2 == 0) {
            pp::LocalCandleSeriesPoint next = last;
            next.candle.startTime = last.endTime;
            next.endTime = next.candle.startTime.addSecs(60);
            next.candle.open = last.candle.close;
            next.candle.close = next.candle.open + 0.012;
            next.candle.high = next.candle.close + 0.04;
            next.candle.low = next.candle.open - 0.03;
            next.candle.volume = 9000.0 + appendedBars * 50.0;
            points.append(next);
            ++appendedBars;
        }
        QElapsedTimer refreshTimer;
        refreshTimer.start();
        model.setPoints(points);
        tailRefreshDurations.append(refreshTimer.nsecsElapsed());
    });

    QObject::connect(&window, &QQuickWindow::widthChanged, chart, [&, chart] {
        chart->setWidth(window.width());
    });
    QObject::connect(&window, &QQuickWindow::heightChanged, chart, [&, chart] {
        chart->setHeight(window.height());
    });
    QObject::connect(&window, &QQuickWindow::sceneGraphError, &window,
                     [&](QQuickWindow::SceneGraphError, const QString &message) {
        sceneGraphError = message;
    });

    auto finish = [&](bool timedOut) {
        if (finishing) return;
        finishing = true;
        actionTimer.stop();
        memoryTimer.stop();
        progressTimer.stop();
        durationTimer.stop();
        timeoutTimer.stop();
        sampleMemory();
        const pp::FrameTimingSummary frameSummary =
            pp::summarizeFrameTimings(frameIntervals);
        const pp::FrameTimingSummary interactionSummary =
            pp::summarizeFrameTimings(interactionDurations);
        const pp::FrameTimingSummary refreshSummary =
            pp::summarizeFrameTimings(tailRefreshDurations);
        const double refreshRate = targetScreen->refreshRate();
        const double refreshBudget = refreshRate > 1.0 ? 1000.0 / refreshRate : 16.7;
        QVector<pp::ProcessMemorySample> steadyMemorySamples;
        steadyMemorySamples.reserve(memorySamples.size());
        for (const pp::ProcessMemorySample &sample : memorySamples) {
            if (sample.elapsedMilliseconds >= 60'000)
                steadyMemorySamples.append(sample);
        }
        QJsonArray memorySampleArray;
        for (const pp::ProcessMemorySample &sample : memorySamples)
            memorySampleArray.append(memorySampleJson(sample));
        const bool completionCondition = durationSeconds > 0
            ? !frameIntervals.isEmpty()
            : frameIntervals.size() >= requestedFrames;
        const bool valid = !timedOut && sceneGraphError.isEmpty()
            && completionCondition;

        QJsonObject report {
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("createdAtUtc"),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("status"), valid ? QStringLiteral("completed")
                                              : QStringLiteral("failed")},
            {QStringLiteral("measurementScope"), QStringLiteral(
                 "端到端交换帧间隔：包含事件调度、Scene Graph 同步/渲染、RHI 提交与呈现同步；不等同于纯 GPU 指令耗时")},
            {QStringLiteral("timedOut"), timedOut},
            {QStringLiteral("stopMode"), timedOut
                 ? QStringLiteral("hard_timeout")
                 : durationSeconds > 0 ? QStringLiteral("duration")
                                       : QStringLiteral("frame_count")},
            {QStringLiteral("actualDurationSeconds"),
             double(probeClock.elapsed()) / 1000.0},
            {QStringLiteral("sceneGraphError"), sceneGraphError},
            {QStringLiteral("renderer"), QJsonObject {
                 {QStringLiteral("graphicsApi"), graphicsApiName(
                      window.rendererInterface()->graphicsApi())},
                 {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
                 {QStringLiteral("buildType"),
#ifdef NDEBUG
                  QStringLiteral("Release")
#else
                  QStringLiteral("Debug")
#endif
                 }
             }},
            {QStringLiteral("host"), QJsonObject {
                 {QStringLiteral("os"), QSysInfo::prettyProductName()},
                 {QStringLiteral("cpuArchitecture"), QSysInfo::currentCpuArchitecture()}
             }},
            {QStringLiteral("screen"), QJsonObject {
                 {QStringLiteral("name"), targetScreen->name()},
                 {QStringLiteral("secondaryRequestedAndUsed"), usedSecondaryScreen},
                 {QStringLiteral("availableGeometry"), rectJson(available)},
                 {QStringLiteral("windowGeometry"), rectJson(window.geometry())},
                 {QStringLiteral("devicePixelRatio"), targetScreen->devicePixelRatio()},
                 {QStringLiteral("refreshRateHz"), refreshRate},
                 {QStringLiteral("refreshBudgetMs"), refreshBudget}
             }},
            {QStringLiteral("configuration"), QJsonObject {
                 {QStringLiteral("initialPointCount"), pointCount},
                 {QStringLiteral("finalPointCount"), points.size()},
                 {QStringLiteral("initialVisiblePointCount"), initialVisible},
                 {QStringLiteral("finalVisiblePointCount"), chart->visibleCount()},
                 {QStringLiteral("requestedFrames"), requestedFrames},
                 {QStringLiteral("warmupFrames"), warmupFrames},
                 {QStringLiteral("durationSeconds"), durationSeconds},
                 {QStringLiteral("memorySampleIntervalSeconds"),
                  memorySampleIntervalSeconds},
                 {QStringLiteral("progressIntervalSeconds"),
                  progressIntervalSeconds},
                 {QStringLiteral("tailRefreshIntervalActions"), refreshInterval},
                 {QStringLiteral("actions"), actionCount},
                 {QStringLiteral("appendedBars"), appendedBars},
                 {QStringLiteral("allIndicatorLayersEnabled"), true}
             }},
            {QStringLiteral("preparationMs"),
             double(preparationNanoseconds) / 1'000'000.0},
            {QStringLiteral("frameIntervals"), frameSummary.toJson()},
            {QStringLiteral("framesOverRefreshBudget"),
             pp::countTimingSamplesOver(frameIntervals, refreshBudget)},
            {QStringLiteral("framesOverOneAndHalfRefreshBudget"),
             pp::countTimingSamplesOver(frameIntervals, refreshBudget * 1.5)},
            {QStringLiteral("framesOverTwoRefreshBudgets"),
             pp::countTimingSamplesOver(frameIntervals, refreshBudget * 2.0)},
            {QStringLiteral("interactionCpu"), interactionSummary.toJson()},
            {QStringLiteral("tailRefreshCpu"), refreshSummary.toJson()},
            {QStringLiteral("processMemory"),
             pp::summarizeProcessMemory(memorySamples).toJson()},
            {QStringLiteral("processMemoryAfter60Seconds"),
             pp::summarizeProcessMemory(steadyMemorySamples).toJson()},
            {QStringLiteral("processMemorySamples"), memorySampleArray},
            {QStringLiteral("sceneGraph"), QJsonObject {
                 {QStringLiteral("renderBucketCount"), chart->renderBucketCount()},
                 {QStringLiteral("levelOfDetailActive"), chart->levelOfDetailActive()},
                 {QStringLiteral("averageBarsPerRenderBucket"),
                  chart->averageBarsPerRenderBucket()},
                 {QStringLiteral("geometryVertexCount"), chart->geometryVertexCount()},
                 {QStringLiteral("volumeVertexCount"), chart->volumeVertexCount()},
                 {QStringLiteral("indicatorVertexCount"), chart->indicatorVertexCount()},
                 {QStringLiteral("oscillatorVertexCount"), chart->oscillatorVertexCount()},
                 {QStringLiteral("fullIndicatorRebuildCount"),
                  qint64(chart->fullIndicatorRebuildCount())},
                 {QStringLiteral("incrementalIndicatorUpdateCount"),
                  qint64(chart->incrementalIndicatorUpdateCount())}
             }}
        };

        QString writeError;
        const bool written = writeJson(parser.value(outputOption), report, &writeError);
        QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact)
                            << Qt::endl;
        if (!written) QTextStream(stderr) << writeError << Qt::endl;
        window.hide();
        QTimer::singleShot(0, &application, [&, valid, written] {
            application.exit(valid && written ? 0 : 4);
        });
    };

    QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&] {
        const qint64 now = probeClock.nsecsElapsed();
        ++swappedFrames;
        if (previousSwapNanoseconds >= 0 && swappedFrames > warmupFrames)
            frameIntervals.append(now - previousSwapNanoseconds);
        previousSwapNanoseconds = now;
        if (durationSeconds == 0 && frameIntervals.size() >= requestedFrames)
            QTimer::singleShot(0, &window, [&] { finish(false); });
    });

    QObject::connect(&durationTimer, &QTimer::timeout, &window,
                     [&] { finish(false); });
    QObject::connect(&timeoutTimer, &QTimer::timeout, &window, [&] { finish(true); });
    const int timeoutMilliseconds = durationSeconds > 0
        ? (durationSeconds + 60) * 1000
        : std::max(30'000, int(std::ceil(
              (warmupFrames + requestedFrames) * 1000.0 / 20.0)));
    timeoutTimer.start(timeoutMilliseconds);

    window.show();
    sampleMemory();
    memoryTimer.start();
    progressTimer.start();
    if (durationSeconds > 0)
        durationTimer.start(durationSeconds * 1000);
    actionTimer.start();
    return application.exec();
}
