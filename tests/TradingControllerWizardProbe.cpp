#include "app/WizardContractProbe.h"
#include "app/TradingController.h"
#include "services/BacktestDataVersion.h"
#include "services/SqliteLocalDataStore.h"

#include <QApplication>
#include <QAccessible>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTimeZone>
#include <functional>
#include <algorithm>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
bool waitUntil(const std::function<bool()> &ready, int timeout = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    return ready();
}

pp::BacktestArchiveRecord fixture(int window)
{
    pp::BacktestArchiveRecord record;
    record.resultId = QStringLiteral("wizard-probe-%1").arg(window);
    record.completedAt = QDateTime(QDate(2026, 9, 7), QTime(1, 0), Qt::UTC);
    auto &config = record.result.config;
    config.taskId = record.resultId;
    config.strategyId = QStringLiteral("sma_crossover");
    config.instrument = {QStringLiteral("600519"), QStringLiteral("合成契约测试"),
                         pp::Market::AShare, QStringLiteral("SSE")};
    config.dataMode = pp::BacktestDataMode::Candles;
    config.frequency = QStringLiteral("1m");
    config.periodSeconds = 60;
    config.requestedBars = config.actualBars = 3;
    config.initialCapital = 100000.0;
    config.from = record.completedAt.addDays(-1);
    config.to = config.from.addSecs(120);
    if (window & 1) config.sampleFrom = config.from;
    if (window & 2) config.sampleTo = config.to;
    for (int index = 0; index < 3; ++index) {
        pp::Candle candle;
        candle.instrument = config.instrument;
        candle.startTime = config.from.addSecs(60 * index);
        candle.periodSeconds = 60;
        candle.open = candle.close = 100.0 + index;
        candle.high = candle.close + 1.0;
        candle.low = candle.close - 1.0;
        candle.volume = 100;
        candle.source = QStringLiteral("synthetic_contract_fixture");
        record.result.candles.append(candle);
        record.result.equityCurve.append({candle.startTime, 100000, 100000, 0, 0});
    }
    config.dataVersion = pp::computeBacktestDataVersion(record.result.candles,
                                                       config.instrument.symbol, 60);
    record.result.summary.initialCapital = record.result.summary.finalEquity = 100000;
    return record;
}
}

int runWizardContractProbe()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    const auto arguments = QCoreApplication::arguments();
    const int argumentIndex = arguments.indexOf(QStringLiteral("--wizard-contract-probe"));
    const QString reportPath = arguments.value(argumentIndex + 1,
                                              QStringLiteral("wizard-contract-probe.json"));
    const bool cancelOffline = arguments.contains(QStringLiteral("--exit-cancel-offline"));
    const auto phase = [&](const QString &name) {
        QFile trace(reportPath + QStringLiteral(".log"));
        if (trace.open(QIODevice::Append))
            trace.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
                         + QLatin1Char(' ') + name + QLatin1Char('\n')).toUtf8());
    };
    phase(QStringLiteral("probe-start platform=%1 args=%2")
              .arg(QGuiApplication::platformName(), arguments.join(QLatin1Char('|'))));
    QCoreApplication::setOrganizationName(QStringLiteral("PPTraderTests"));
    QCoreApplication::setApplicationName(QStringLiteral("WizardControllerProbe"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    const QString databasePath = temporary.filePath(QStringLiteral("fixture.db"));
    qputenv("PPTRADER_DATABASE_PATH", databasePath.toLocal8Bit());
    qputenv("PPTRADER_AUTOSTART_LIVE", "0");
    QJsonArray checks;
    bool succeeded = true;
    const auto check = [&](bool passed, const QString &name) {
        checks.append(QJsonObject{{QStringLiteral("name"), name},
                                 {QStringLiteral("passed"), passed}});
        succeeded = succeeded && passed;
        return passed;
    };
    const auto require = [&](bool passed, const QString &name) {
        if (!check(passed, name)) throw std::runtime_error(name.toStdString());
    };
    try {
        {
            pp::SqliteLocalDataStore store;
            require(store.initialize(databasePath), QStringLiteral("初始化临时库"));
            for (int window = 0; window < 4; ++window)
                require(store.saveBacktestArchive(fixture(window)), QStringLiteral("保存合成输入归档"));
            auto legacy = fixture(0);
            legacy.resultId = legacy.result.config.taskId = QStringLiteral("legacy-unknown-window");
            legacy.result.config.sampleWindowKnown = false;
            require(store.saveBacktestArchive(legacy), QStringLiteral("保存未知请求范围的旧归档样例"));
            if (cancelOffline) {
                const auto instrument = fixture(0).result.config.instrument;
                const QDate day(2026, 9, 4);
                QVector<pp::RawTradeTick> ticks;
                QVector<pp::Candle> candles;
                for (int index = 0; index < 120; ++index) {
                    pp::RawTradeTick tick;
                    tick.instrument = instrument;
                    tick.tradingDate = day;
                    tick.timestamp = QDateTime(day, QTime(9, 30), QTimeZone("Asia/Shanghai"))
                        .addSecs(index * 60).toUTC();
                    tick.sourceLine = index + 1;
                    tick.price = 100.0 + (index % 7) * 0.1;
                    tick.volumeShares = 100;
                    tick.amount = tick.price * tick.volumeShares;
                    tick.direction = pp::RawTradeDirection::Buy;
                    tick.source = QStringLiteral("tickdata_csv_tick");
                    ticks.append(tick);
                    pp::Candle candle;
                    candle.instrument = instrument;
                    candle.startTime = tick.timestamp;
                    candle.periodSeconds = 60;
                    candle.open = candle.high = candle.low = candle.close = tick.price;
                    candle.volume = tick.volumeShares;
                    candle.source = QStringLiteral("raw_tick_cache_v1");
                    candle.adjustment = QStringLiteral("none");
                    candles.append(candle);
                }
                require(store.beginRawTradeTickImport(instrument, day, QStringLiteral("tickdata_csv_tick")),
                        QStringLiteral("准备隔离合成Tick输入"));
                require(store.appendRawTradeTicks(ticks), QStringLiteral("写入隔离合成Tick"));
                pp::TickDataImportResult audit;
                audit.succeeded = true;
                audit.linesRead = audit.validRecords = ticks.size();
                audit.qualityStatus = QStringLiteral("healthy");
                require(store.commitRawTradeTickImport(audit), QStringLiteral("提交合成输入"));
                require(store.replaceRawTickCandleCache(instrument, day, 60,
                    store.rawTradeFingerprint(instrument, day), candles), QStringLiteral("生成本地派生Bar"));
            }
        }
        {
            pp::TradingController controller;
            check(!controller.saveValidationRun(QStringLiteral("无结果"))
                      && controller.validationRuns().isEmpty(), QStringLiteral("未加载结果不可保存"));
            QQmlEngine engine;
            QQmlComponent themeComponent(&engine, QUrl(QStringLiteral("qrc:/PPTrader/qml/components/PPTheme.qml")));
            std::unique_ptr<QObject> theme(themeComponent.create());
            require(bool(theme), themeComponent.errorString());
            QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/PPTrader/qml/components/CustomStrategyWizard.qml")));
            std::unique_ptr<QObject> wizard(component.createWithInitialProperties({
                {QStringLiteral("theme"), QVariant::fromValue(theme.get())},
                {QStringLiteral("controller"), QVariant::fromValue(&controller)},
                {QStringLiteral("width"), 1600}, {QStringLiteral("height"), 900}}));
            require(bool(wizard), component.errorString());
            for (int window = 0; window < 4; ++window) {
                require(controller.loadBacktestArchive(fixture(window).resultId), QStringLiteral("真实控制器请求归档"));
                require(waitUntil([&] { return !controller.backtestArchiveLoading(); }), QStringLiteral("异步归档完成"));
                require(controller.backtestArchiveError().isEmpty(), controller.backtestArchiveError());
                require(!controller.replayBacktestBar(2).isEmpty()
                            && controller.replayBacktestBar(3).isEmpty(), QStringLiteral("真实结果包含三根 Bar"));
                const int before = controller.validationRuns().size();
                check(controller.saveValidationRun(QStringLiteral("归档验证-%1").arg(window)),
                      QStringLiteral("保存返回成功"));
                pp::SqliteLocalDataStore reader;
                require(reader.initialize(databasePath), QStringLiteral("独立读取验证记录"));
                check(reader.loadValidationRuns(10).size() == before + 1,
                      QStringLiteral("归档%1：成功落库才增加计数").arg(window));
                check(controller.validationRuns().size() == reader.loadValidationRuns(10).size(),
                      QStringLiteral("内存与 SQLite 计数一致"));
                const auto savedRuns = controller.validationRuns();
                const auto saved = std::find_if(savedRuns.cbegin(), savedRuns.cend(), [&](const QVariant &value) {
                    return value.toMap().value(QStringLiteral("name")).toString()
                        == QStringLiteral("归档验证-%1").arg(window);
                });
                require(saved != savedRuns.cend(), QStringLiteral("真实控制器发布已保存记录"));
                check(saved->toMap().value(QStringLiteral("sampleFrom")).toDateTime() == fixture(window).result.config.sampleFrom
                          && saved->toMap().value(QStringLiteral("sampleTo")).toDateTime() == fixture(window).result.config.sampleTo,
                      QStringLiteral("归档→Controller验证记录保留样本窗%1").arg(window));
            }
            require(controller.loadBacktestArchive(QStringLiteral("legacy-unknown-window")), QStringLiteral("真实控制器加载旧未知请求范围"));
            require(waitUntil([&] { return !controller.backtestArchiveLoading(); })
                        && controller.backtestArchiveError().isEmpty(), QStringLiteral("旧未知归档加载完成"));
            const auto beforeUnknownSave = controller.validationRuns();
            check(!controller.saveValidationRun(QStringLiteral("未知范围不可保存")), QStringLiteral("未知请求范围拒绝保存验证"));
            check(controller.validationRuns() == beforeUnknownSave, QStringLiteral("未知范围拒绝后计数不变"));
            check(controller.statusText().contains(QStringLiteral("旧归档未记录请求日期范围")), QStringLiteral("未知范围给出明确说明"));
            require(controller.loadBacktestArchive(fixture(3).resultId), QStringLiteral("回到已知请求范围归档"));
            require(waitUntil([&] { return !controller.backtestArchiveLoading(); })
                        && controller.backtestArchiveError().isEmpty(), QStringLiteral("已知归档恢复完成"));
            for (const int index : {0, 2, 3, -1}) {
                const bool valid = index == 0 || index == 2;
                const auto bar = controller.replayBacktestBar(index);
                check(bar.isEmpty() != valid, QStringLiteral("Controller 回放边界 %1").arg(index));
                wizard->setProperty("workflowReplayIndex", QString::number(index));
                QVariant returned;
                require(QMetaObject::invokeMethod(wizard.get(), "replayWorkflowBar",
                                                  Q_RETURN_ARG(QVariant, returned)), QStringLiteral("真实 QML 回放调用"));
                check(returned.toBool() == valid, QStringLiteral("Controller→Wizard 回放 %1").arg(index));
                if (valid) {
                    const QVariant replayValue = wizard->property(
                        "workflowReplayData");
                    QVariant qmlTimestampValue;
                    QString javascriptTimestamp;
                    if (replayValue.metaType().id() == qMetaTypeId<QJSValue>()) {
                        const QJSValue replayData = replayValue.value<QJSValue>();
                        const QJSValue timestamp = replayData.property(
                            QStringLiteral("timestamp"));
                        qmlTimestampValue = timestamp.toVariant();
                        javascriptTimestamp = timestamp.toString();
                    } else {
                        qmlTimestampValue = replayValue.toMap().value(
                            QStringLiteral("timestamp"));
                        javascriptTimestamp = qmlTimestampValue.toString();
                    }
                    QDateTime qmlTimestamp = qmlTimestampValue.toDateTime();
                    if (!qmlTimestamp.isValid()) {
                        qmlTimestamp = QDateTime::fromString(
                            qmlTimestampValue.toString(), Qt::ISODateWithMs);
                    }
                    qmlTimestamp = qmlTimestamp.toUTC();
                    const QDateTime controllerTimestamp = bar.value(
                        QStringLiteral("timestamp")).toDateTime().toUTC();
                    check(qmlTimestamp == controllerTimestamp,
                          QStringLiteral("真实回放时间一致：QML=%1（%2，JS=%3），Controller=%4")
                              .arg(qmlTimestamp.toString(Qt::ISODateWithMs),
                                   QString::fromLatin1(qmlTimestampValue.metaType().name()),
                                   javascriptTimestamp,
                                   controllerTimestamp.toString(Qt::ISODateWithMs)));
                }
            }
            const auto beforeFailure = controller.validationRuns();
            const QString connection = QStringLiteral("wizard-failure-fixture");
            {
                auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
                database.setDatabaseName(databasePath);
                require(database.open(), QStringLiteral("打开故障注入临时库"));
                QSqlQuery query(database);
                require(query.exec(QStringLiteral("create trigger reject_validation before insert on backtest_validation_runs begin select raise(ABORT, 'fixture_validation_rejected'); end")),
                        QStringLiteral("仅临时库注入写入失败"));
            }
            QSqlDatabase::removeDatabase(connection);
            check(!controller.saveValidationRun(QStringLiteral("必须失败")), QStringLiteral("写入失败返回失败"));
            check(controller.validationRuns() == beforeFailure, QStringLiteral("写入失败不增加内存计数"));
            check(controller.statusText().contains(QStringLiteral("fixture_validation_rejected")), QStringLiteral("界面保留真实写入错误"));
            wizard.reset();
            {
                const bool visibleAcceptance = arguments.contains(QStringLiteral("--visible"));
                auto mainEngine = std::make_unique<QQmlApplicationEngine>();
                QObject::connect(mainEngine.get(), &QQmlApplicationEngine::warnings,
                                 mainEngine.get(), [&](const QList<QQmlError> &warnings) {
                    for (const QQmlError &warning : warnings)
                        phase(QStringLiteral("qml-warning ") + warning.toString());
                });
                mainEngine->rootContext()->setContextProperty(QStringLiteral("tradingController"), &controller);
                mainEngine->load(arguments.contains(QStringLiteral("--source-qml"))
                    ? QUrl::fromLocalFile(QStringLiteral(PPTRADER_SOURCE_DIR "/qml/Main.qml"))
                    : QUrl(QStringLiteral("qrc:/PPTrader/qml/Main.qml")));
                require(!mainEngine->rootObjects().isEmpty(), QStringLiteral("原生 Main 创建"));
                auto *window = qobject_cast<QQuickWindow *>(mainEngine->rootObjects().first());
                require(window != nullptr, QStringLiteral("原生窗口存在"));
                window->setTitle(QStringLiteral("PPTrader · 隔离向导验收"));
                const int widthIndex = arguments.indexOf(QStringLiteral("--visual-width"));
                const int requestedWidth = widthIndex >= 0 ? arguments.value(widthIndex + 1).toInt() : 1280;
                QScreen *screen = QGuiApplication::screens().size() > 1
                    ? QGuiApplication::screens().at(1) : QGuiApplication::primaryScreen();
                window->setScreen(screen);
                window->setGeometry(QRect(screen->availableGeometry().topLeft(),
                                          QSize(std::clamp(requestedWidth, 1120, 1600),
                                                requestedWidth <= 1120 ? 760 : 1000)));
                QMetaObject::invokeMethod(window, "activateWorkspace", Q_ARG(QVariant, 2));
                auto *nativeWizard = window->findChild<QQuickItem *>(QStringLiteral("customStrategyWizard"));
                require(nativeWizard != nullptr, QStringLiteral("原生向导存在"));
                nativeWizard->parent()->setProperty("wizardMode", true);
                for (QObject *parent = nativeWizard->parent(); parent; parent = parent->parent()) {
                    if (parent->property("subPage").isValid() && parent->property("strategyMode").isValid()) {
                        parent->setProperty("subPage", 1);
                        parent->setProperty("strategyMode", 1);
                    }
                }
                nativeWizard->setProperty("activeStep", 4);
                auto *workbench = window->findChild<QQuickItem *>(QStringLiteral("customStrategyWorkbench"));
                auto *jsonLayer = window->findChild<QQuickItem *>(QStringLiteral("customStrategyJsonLayer"));
                auto *pageScroll = window->findChild<QQuickItem *>(QStringLiteral("strategyResearchScroll"));
                auto *footer = nativeWizard->findChild<QQuickItem *>(QStringLiteral("wizardFooter"));
                require(workbench && jsonLayer && pageScroll && footer, QStringLiteral("真实宿主布局节点存在"));
                const auto settle = [] {
                    QElapsedTimer timer;
                    timer.start();
                    while (timer.elapsed() < 150) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                        QThread::msleep(5);
                    }
                };
                require(waitUntil([&] { return window->isExposed(); }), QStringLiteral("宿主窗口已呈现，避免隐藏启动造成零宽误判"));
                for (const QSize size : {QSize(1120, 760), QSize(1280, 1000), QSize(1600, 1000)}) {
                    window->resize(size);
                    settle();
                    check(nativeWizard->isVisible() && nativeWizard->width() >= 500
                              && footer->height() > 0, QStringLiteral("宿主向导实际可见且布局有效"));
                    const QPointF footerEnd = footer->mapToScene(QPointF(footer->width(), footer->height()));
                    const QPointF viewportEnd = pageScroll->mapToScene(QPointF(pageScroll->width(), pageScroll->height()));
                    check(footerEnd.y() <= viewportEnd.y() + 0.5,
                          QStringLiteral("宿主%1x%2底栏位于可见视口").arg(size.width()).arg(size.height()));
                    check(footerEnd.x() <= viewportEnd.x() + 0.5,
                          QStringLiteral("宿主%1底栏不越过右缘").arg(size.width()));
                    check(!jsonLayer->isVisible(), QStringLiteral("向导模式隐藏底层JSON"));
                    const QString draftName = nativeWizard->property("draftName").toString();
                    workbench->setProperty("wizardMode", false);
                    settle();
                    check(jsonLayer->isVisible() && !nativeWizard->isVisible(), QStringLiteral("退出向导恢复JSON模式"));
                    workbench->setProperty("wizardMode", true);
                    settle();
                    check(nativeWizard->property("draftName").toString() == draftName
                              && nativeWizard->property("activeStep").toInt() == 4,
                          QStringLiteral("模式切换保留向导草稿与步骤"));
                }
                window->resize(std::clamp(requestedWidth, 1120, 1600), requestedWidth <= 1120 ? 760 : 1000);
                settle();
                QCoreApplication::processEvents();
                const auto *area = nativeWizard->findChild<QQuickItem *>(QStringLiteral("wizardConditionArea"));
                check(area && area->width() >= 500, QStringLiteral("原生嵌入条件区宽度至少500"));
                phase(QStringLiteral("native-ready width=%1 wizard=%2").arg(window->width()).arg(nativeWizard->width()));
                const int activationIndex = arguments.indexOf(QStringLiteral("--exit-activation-count"));
                const int activations = activationIndex >= 0
                    ? arguments.value(activationIndex + 1).toInt() : 0;
                require(activations >= 0 && activations <= 20000,
                        QStringLiteral("退出测试激活次数有效"));
#ifdef Q_OS_WIN
                if (activations > 0
                    && QGuiApplication::platformName() == QStringLiteral("windows")) {
                    SendMessageW(reinterpret_cast<HWND>(window->winId()), WM_GETOBJECT, 0, -25);
                }
#endif
                if (activations > 0
                    && QGuiApplication::platformName() != QStringLiteral("windows")) {
                    QAccessible::setActive(true);
                }
#ifdef Q_OS_WIN
                if (activations > 0)
                    require(QAccessible::isActive(), QStringLiteral("退出测试保持原生无障碍激活"));
#else
                if (activations > 0) {
                    // Qt 6.8 起 QAccessible::setActive() 只负责通知激活观察者，
                    // isActive() 直接返回平台插件的无障碍实现状态（见 Qt 6.8
                    // qaccessible.cpp）。Windows 的 windows 平台插件内置原生 UIA，
                    // 激活后必为 true；Linux 平台插件在缺少可用的 AT-SPI 桥接时
                    // 恒为 false，本机 offscreen 与 wayland 均已复核。这是平台
                    // 事实，不是产品回退，因此记录实测状态而不把它当作门禁；
                    // 其后的等量重复激活、关闭时序与 PID 回收仍照常验收。
                    phase(QStringLiteral("accessibility-bridge=%1")
                              .arg(QAccessible::isActive() ? QStringLiteral("active")
                                                           : QStringLiteral("unavailable")));
                }
#endif
                for (int activation = 0; activation < activations; ++activation)
                    QAccessible::setActive(true);
                if (activationIndex >= 0)
                    phase(QStringLiteral("exit-activations=%1").arg(activations));
                if (cancelOffline) {
                    bool cancelQueued = false;
                    bool cancelledWhileRunning = false;
                    QObject cancellationContext;
                    const auto progressConnection = QObject::connect(&controller,
                        &pp::TradingController::backtestStateChanged, &cancellationContext, [&] {
                            if (!cancelQueued && controller.backtestRunning()
                                && controller.backtestProgress() > 0.0
                                && controller.backtestProgress() < 1.0) {
                                cancelQueued = true;
                                QMetaObject::invokeMethod(&cancellationContext, [&] {
                                    cancelledWhileRunning = controller.backtestRunning();
                                    controller.cancelBacktest();
                                }, Qt::QueuedConnection);
                            }
                        });
                    controller.setBacktestSampleWindow({}, {});
                    require(controller.runStrategyBacktest(QStringLiteral("sma_crossover"),
                        {{QStringLiteral("fastPeriod"), 2}, {QStringLiteral("slowPeriod"), 3}},
                        QStringLiteral("600519"), QStringLiteral("1m"), 120, 100000.0, 0.0003, 0.0) != 0,
                        QStringLiteral("通过真实控制器启动离线回测"));
                    const bool stopped = waitUntil([&] {
                        return cancelQueued && !controller.backtestRunning();
                    });
                    QObject::disconnect(progressConnection);
                    require(stopped && cancelledWhileRunning, QStringLiteral("回测已计算后取消且停止"));
                    require(controller.backtestStatusText() == QStringLiteral("已取消")
                        && controller.backtestError().isEmpty(), QStringLiteral("界面状态确认为取消而非完成或报错"));
                    phase(QStringLiteral("offline-cancel-confirmed progress=%1").arg(controller.backtestProgress()));
                }
                // 与 main 保持相同次序：隐藏请求退出→aboutToQuit 停服务→销毁 QML。
                const auto quitConnection = QObject::connect(QCoreApplication::instance(),
                    &QCoreApplication::aboutToQuit, &controller, [&] {
                        phase(QStringLiteral("controller-shutdown-begin"));
                        controller.shutdown();
                        phase(QStringLiteral("controller-shutdown-end"));
                    });
                QObject::connect(window, &QWindow::visibleChanged, window, [&](bool visible) {
                    if (!visible) {
                        phase(QStringLiteral("native-closed"));
                        QMetaObject::invokeMethod(QCoreApplication::instance(),
                            &QCoreApplication::quit, Qt::QueuedConnection);
                    }
                });
                // 桌面读取有延迟，最多等待五分钟；不触碰其他应用或用户设置。
                QTimer::singleShot(visibleAcceptance ? 300000 : 0, window, [window] { window->close(); });
                QCoreApplication::exec();
                QObject::disconnect(quitConnection);
                phase(QStringLiteral("native-engine-destruct-begin"));
                mainEngine.reset();
                phase(QStringLiteral("native-engine-destroyed"));
            }
            controller.shutdown();
        }
        phase(QStringLiteral("controller-destroyed"));
        {
            pp::TradingController reopened;
            check(reopened.validationRuns().size() == 4, QStringLiteral("重开控制器恢复四条验证记录"));
            reopened.shutdown();
        }
    } catch (const std::exception &error) {
        check(false, QString::fromUtf8(error.what()));
    }
    QFile output(reportPath);
    if (!output.open(QIODevice::WriteOnly)) return 2;
    output.write(QJsonDocument(QJsonObject{{QStringLiteral("passed"), succeeded},
        {QStringLiteral("checks"), checks}}).toJson());
    return succeeded ? 0 : 1;
}
