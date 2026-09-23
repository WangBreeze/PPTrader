#include "app/TradingController.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

// 隔离真实 Controller/编译后 Main 的公开入口，不接行情网络，不绕过运行门禁。
int runPaperStrategyContractProbe()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    const auto arguments = QCoreApplication::arguments();
    const QString reportPath = arguments.value(arguments.indexOf(QStringLiteral("--paper-panel-probe")) + 1);
    QCoreApplication::setOrganizationName(QStringLiteral("PPTraderTests"));
    QCoreApplication::setApplicationName(QStringLiteral("PaperControllerProbe"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    qputenv("PPTRADER_DATABASE_PATH", temporary.filePath(QStringLiteral("fixture.db")).toLocal8Bit());
    qputenv("PPTRADER_AUTOSTART_LIVE", "0");
    qputenv("PPTRADER_LOG_PATH", (reportPath + QStringLiteral(".log")).toLocal8Bit());
    // 主页组件可能尝试读取初始分钟图；在外部网络边界拦截，绝不访问公共源。
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy,
                                                      QStringLiteral("127.0.0.1"), 9));
    QJsonArray checks;
    QString failure;
    const auto require = [&](bool passed, const QString &name) {
        checks.append(QJsonObject{{"name", name}, {"passed", passed}});
        if (!passed) throw std::runtime_error(name.toStdString());
    };
    const auto waitUntil = [](const std::function<bool()> &ready) {
        QElapsedTimer timer;
        timer.start();
        while (!ready() && timer.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(5);
        }
        return ready();
    };
    try {
        pp::TradingController controller;
        const QString id = QStringLiteral("custom_paper_flow");
        const QString json = QString::fromUtf8(R"({"schemaVersion":2,"strategyId":"custom_paper_flow","version":1,
            "name":"模拟候选流程样例","description":"只验证版本发布，不代表盈利","author":"Qt Test",
            "market":"A股","executionScope":"backtest_only","createdAt":"2026-09-07T00:00:00Z",
            "updatedAt":"2026-09-07T00:00:00Z","parameters":[],
            "position":{"type":"fixedPercent","percent":0.1},
            "entry":{"op":"and","conditions":[
                {"op":"gt","left":{"indicator":"SMA","period":2},"right":{"value":100}},
                {"op":"gt","left":{"indicator":"WMA","period":2},"right":{"value":100}}]},
            "exit":{"op":"lt","left":{"indicator":"SMA","period":2},"right":{"value":90}}})");
        require(controller.requestSaveCustomStrategyJson(json) != 0, QStringLiteral("请求保存仅回测版本"));
        const bool saved = waitUntil([&] { return !controller.customStrategySaveLoading(); });
        require(saved && controller.customStrategySaveError().isEmpty(),
                QStringLiteral("异步保存完成：%1").arg(controller.customStrategySaveError()));
        const QString original = controller.exportCustomStrategyJson(id, 1);
        require(!original.isEmpty(), QStringLiteral("原版本可重载"));
        require(controller.publishPaperStrategyVersion(id, 1) != 0, QStringLiteral("显式发布新的模拟版本"));
        const bool published = waitUntil([&] { return !controller.customStrategySaveLoading(); });
        require(published && controller.customStrategySaveError().isEmpty(),
                QStringLiteral("模拟新版本保存完成：%1").arg(controller.customStrategySaveError()));
        require(controller.exportCustomStrategyJson(id, 1) == original, QStringLiteral("不改写原始仅回测版本"));
        require(controller.customStrategyDefinition(id, 2).value("executionScope").toString()
                == QStringLiteral("mock_execution"), QStringLiteral("重载 v2 模拟范围"));
        require(controller.paperStrategyRuns().isEmpty(), QStringLiteral("发布不自动交易"));
        require(controller.startPaperStrategyRun(id, 2, QStringLiteral("600519"), 100000, .0003, .0002).isEmpty(),
                QStringLiteral("无实时任务不得暗中联网启动模拟"));
        require(controller.paperStrategyRuns().isEmpty(), QStringLiteral("启动门禁失败不产生账户"));
        require(!controller.exportPaperStrategyReport(QUrl::fromLocalFile(temporary.filePath("blocked.db"))),
                QStringLiteral("报告禁止覆盖数据库类型"));
        const QString exportPath = temporary.filePath(QStringLiteral("report.json"));
        require(controller.exportPaperStrategyReport(QUrl::fromLocalFile(exportPath)), QStringLiteral("显式导出 JSON 报告"));
        QFile exported(exportPath);
        require(exported.open(QIODevice::ReadOnly), QStringLiteral("报告可读取"));
        const auto document = QJsonDocument::fromJson(exported.readAll()).object();
        require(document.value("profitabilityAccepted").isBool()
            && !document.value("profitabilityAccepted").toBool()
            && document.value("scope").toString() == QStringLiteral("realtime-paper-only"),
            QStringLiteral("报告不冒充盈利或真实交易验收"));

        const double originalCapital = controller.accountInitialEquity();
        const QDateTime sourceStart(QDate(2026, 9, 3), QTime(1, 30), Qt::UTC);
        pp::PaperStrategyConfig portfolioConfig;
        portfolioConfig.initialCapital = 12345; // 不得用候选配置重置工作台资金。
        portfolioConfig.maximumTotalPositionFraction = .3;
        portfolioConfig.maximumHoldingSymbols = 2;
        require(!controller.startPaperReplayPortfolio(id, 2,
            QStringList{QStringLiteral("600519"), QStringLiteral("000001")},
            sourceStart, portfolioConfig).isEmpty(), QStringLiteral("启动双股票单账户回放"));
        const auto runs = controller.paperStrategyRuns();
        require(runs.size() == 1 && runs.first().toMap().value("symbols").toStringList().size() == 2
            && runs.first().toMap().value("accountScope").toString() == QStringLiteral("workstation-single-account")
            && runs.first().toMap().value("initialCapital").toDouble() == originalCapital,
            QStringLiteral("实际 Controller 共用工作台资金而非新建候选资金"));
        require(controller.startPaperReplayRun(id, 2, QStringLiteral("600519"), sourceStart).isEmpty(),
            QStringLiteral("重复启动不新增账户"));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("tradingController"), &controller);
        engine.load(QUrl(QStringLiteral("qrc:/PPTrader/qml/Main.qml")));
        require(!engine.rootObjects().isEmpty(), QStringLiteral("真实 Main QML 加载"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        require(window != nullptr, QStringLiteral("真实应用窗口可用"));
        const auto findQuickItem = [&](const QString &objectName) {
            std::function<QQuickItem *(QQuickItem *)> visit = [&](QQuickItem *item) -> QQuickItem * {
                if (!item) return nullptr;
                if (item->objectName() == objectName) return item;
                for (QQuickItem *child : item->childItems())
                    if (QQuickItem *found = visit(child)) return found;
                return nullptr;
            };
            return visit(window->contentItem());
        };
        QScreen *screen = QGuiApplication::screens().last();
        window->setScreen(screen);
        window->setGeometry(QRect(screen->availableGeometry().topLeft(), QSize(1120, 760)));
        require(QMetaObject::invokeMethod(window, "activateWorkspace", Q_ARG(QVariant, QVariant(4))),
                QStringLiteral("公开导航进入执行中心"));
        const QVector<pp::Instrument> stocks{
            {QStringLiteral("600519"), QStringLiteral("测试一"), pp::Market::AShare},
            {QStringLiteral("000001"), QStringLiteral("测试二"), pp::Market::AShare}};
        const auto deliverBars = [&](const QDateTime &dayStart, double price, bool warmFirst) {
            for (int minute = 0; minute < 2; ++minute) {
                for (const auto &stock : stocks) {
                    pp::Candle bar;
                    bar.instrument = stock; bar.startTime = dayStart.addSecs(minute * 60);
                    bar.periodSeconds = 60; bar.open = bar.high = bar.low = bar.close = price; bar.volume = 1000;
                    require(controller.acceptPaperReplayBar(bar, bar.startTime.addSecs(180), warmFirst && minute == 0),
                        QStringLiteral("多股分钟顺序交付 %1 %2").arg(stock.symbol, bar.startTime.toString(Qt::ISODate)));
                }
            }
        };
        const auto deliverQuotes = [&](const QDateTime &time, double price) {
            for (const auto &stock : stocks) {
                pp::Tick tick; tick.instrument = stock; tick.timestamp = time;
                tick.price = tick.bid = tick.ask = price;
                controller.acceptPaperReplayQuote(tick);
            }
        };
        deliverBars(sourceStart, 110, true);
        deliverQuotes(sourceStart.addSecs(241), 100);
        auto state = controller.paperStrategyRuns().first().toMap();
        require(state.value("fillCount").toInt() == 2 && state.value("quantity").toDouble() == 1800,
            QStringLiteral("两股各买入900股，共用一个账户"));
        require(state.value("buyFillCount").toInt() == 2 && state.value("sellFillCount").toInt() == 0,
            QStringLiteral("运行模型按方向公开两笔买入成交"));
        require(state.value("executionRecordCount").toInt() == 2
            && state.value("executionRecords").toList().size() == 2
            && state.value("executionRecords").toList().first().toMap().value("side").toString() == QStringLiteral("买入"),
            QStringLiteral("运行模型公开可供 QML 显示的逐笔买入记录"));
        require(std::abs(controller.accountCash() - 819909.9892) < .001,
            QStringLiteral("买入含双边报价滑点及佣金后的现金正确"));
        deliverQuotes(sourceStart.addSecs(241), 100);
        require(controller.paperStrategyRuns().first().toMap().value("fillCount").toInt() == 2,
            QStringLiteral("重复报价不重复成交"));
        deliverBars(sourceStart.addDays(1), 80, false);
        deliverQuotes(sourceStart.addDays(1).addSecs(241), 80);
        state = controller.paperStrategyRuns().first().toMap();
        require(state.value("fillCount").toInt() == 4 && state.value("quantity").toDouble() == 0,
            QStringLiteral("次日两股各卖出900股，回到现金仓"));
        require(state.value("buyFillCount").toInt() == 2 && state.value("sellFillCount").toInt() == 2,
            QStringLiteral("运行模型按方向公开两笔买入和两笔卖出"));
        const auto executionRecords = state.value("executionRecords").toList();
        require(state.value("executionRecordCount").toInt() == state.value("fillCount").toInt()
            && executionRecords.size() == 4
            && executionRecords.last().toMap().value("side").toString() == QStringLiteral("卖出")
            && executionRecords.last().toMap().value("quantity").toDouble() == 900,
            QStringLiteral("运行模型逐笔成交与买卖汇总严格一致"));
        require(std::abs(controller.accountCash() - 963837.99784) < .001
            && std::abs(controller.accountTotalEquity() - 963837.99784) < .001,
            QStringLiteral("四笔成交后现金和净值与独立算例一致"));
        QDateTime replayDay = sourceStart.addDays(2);
        const auto nextWeekday = [&]() {
            while (replayDay.date().dayOfWeek() > 5) replayDay = replayDay.addDays(1);
            const QDateTime value = replayDay;
            replayDay = replayDay.addDays(1);
            return value;
        };
        for (int cycle = 0; cycle < 30; ++cycle) {
            const QDateTime buyDay = nextWeekday();
            const QDateTime sellDay = nextWeekday();
            deliverBars(buyDay, 110, false);
            deliverQuotes(buyDay.addSecs(241), 100);
            deliverBars(sellDay, 80, false);
            // 低价 Bar 只触发出场；相同 100 元新报价避免循环消耗账户，确保覆盖百笔滚动窗口。
            deliverQuotes(sellDay.addSecs(241), 100);
        }
        state = controller.paperStrategyRuns().first().toMap();
        const int totalFills = state.value("fillCount").toInt();
        const int buyFills = state.value("buyFillCount").toInt();
        const int sellFills = state.value("sellFillCount").toInt();
        require(totalFills > 100 && buyFills == sellFills
            && buyFills + sellFills == totalFills
            && state.value("executionRecordCount").toInt() == totalFills
            && state.value("displayedExecutionRecordCount").toInt() == 100,
            QStringLiteral("超过一百笔成交后总数与最近一百笔窗口一致"));
        require(waitUntil([&] {
            return findQuickItem(QStringLiteral("paperAccountSummary"))
                && findQuickItem(QStringLiteral("paperOrderSummary"))
                && findQuickItem(QStringLiteral("paperExecutionList"));
        }), QStringLiteral("执行中心 QML 成交组件已实例化"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        auto *accountSummary = findQuickItem(QStringLiteral("paperAccountSummary"));
        auto *orderSummary = findQuickItem(QStringLiteral("paperOrderSummary"));
        auto *executionList = findQuickItem(QStringLiteral("paperExecutionList"));
        const QString accountText = accountSummary->property("text").toString();
        const QString orderText = orderSummary->property("text").toString();
        require(accountText.contains(QStringLiteral("可用现金 %1").arg(state.value("cash").toDouble(), 0, 'f', 2))
            && accountText.contains(QStringLiteral("账户净值 %1").arg(state.value("equity").toDouble(), 0, 'f', 2))
            && accountText.contains(QStringLiteral("总盈亏 %1").arg(state.value("totalPnl").toDouble(), 0, 'f', 2))
            && accountText.contains(QStringLiteral("持仓标的 0 只")),
            QStringLiteral("QML 资金摘要逐值绑定 C++ 快照"));
        require(orderText.contains(QStringLiteral("成交 %1（买 %2 / 卖 %3）")
                .arg(totalFills).arg(buyFills).arg(sellFills))
            && orderText.contains(QStringLiteral("订单拒绝 0")),
            QStringLiteral("QML 成交方向与订单拒绝逐值绑定 C++ 快照"));
        require(executionList->property("count").toInt() == 100
            && executionList->property("contentHeight").toDouble() > executionList->property("height").toDouble()
            && executionList->property("interactive").toBool(),
            QStringLiteral("QML 最近一百笔列表具有真实可滚动范围"));
        window->setGeometry(QRect(screen->availableGeometry().topLeft(), QSize(1600, 1000)));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
        executionList = findQuickItem(QStringLiteral("paperExecutionList"));
        const QPointF wheelPosition = executionList->mapToScene(
            QPointF(executionList->width() / 2.0, executionList->height() / 2.0));
        for (int wheelStep = 0; wheelStep < 3; ++wheelStep) {
            QWheelEvent wheelEvent(wheelPosition,
                QPointF(window->mapToGlobal(wheelPosition.toPoint())), QPoint(), QPoint(0, -120),
                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window, &wheelEvent);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        }
        require(executionList->property("contentY").toDouble() > 0.0,
            QStringLiteral("QML 成交列表接收真实滚轮并移动到后续记录"));
        const double preservedExecutionContentY = executionList->property("contentY").toDouble();
        const QDateTime refreshDay = nextWeekday();
        deliverBars(refreshDay, 110, false);
        deliverQuotes(refreshDay.addSecs(241), 100);
        for (int refresh = 0; refresh < 12; ++refresh) {
            deliverQuotes(refreshDay.addSecs(242 + refresh), 100 + refresh * .01);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
        }
        QThread::msleep(500);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        executionList = findQuickItem(QStringLiteral("paperExecutionList"));
        const double refreshedExecutionContentY = executionList
            ? executionList->property("contentY").toDouble() : -1.0;
        const double retainedExecutionContentY = executionList
            ? executionList->property("retainedContentY").toDouble() : -1.0;
        const bool refreshPending = executionList
            && executionList->property("modelRefreshPending").toBool();
        const double refreshedContentHeight = executionList
            ? executionList->property("contentHeight").toDouble() : -1.0;
        const double refreshedHeight = executionList
            ? executionList->property("height").toDouble() : -1.0;
        require(executionList
            && std::abs(refreshedExecutionContentY - preservedExecutionContentY) < .5,
            QStringLiteral("QML 成交列表在运行数据刷新后保持用户滚动位置（刷新前 %1，刷新后 %2，已保存 %3，刷新保护 %4，内容 %5/%6）")
                .arg(preservedExecutionContentY).arg(refreshedExecutionContentY)
                .arg(retainedExecutionContentY).arg(refreshPending)
                .arg(refreshedContentHeight).arg(refreshedHeight));
        const double extendedCash = controller.accountCash();
        require(controller.placeOrder(QStringLiteral("600519"), QStringLiteral("buy"), QStringLiteral("market"), 100, 0).isEmpty(),
            QStringLiteral("策略绑定后拒绝混入手动下单"));
        controller.stopAllPaperStrategies();
        controller.resetPaperAccount(999);
        require(controller.accountInitialEquity() == originalCapital
            && std::abs(controller.accountCash() - extendedCash) < .001,
            QStringLiteral("停止和重置请求不清除已发生成交的账户资金"));
        const int holdSeconds = std::clamp(qEnvironmentVariableIntValue("PPTRADER_PAPER_PANEL_HOLD_SECONDS"), 0, 300);
        QElapsedTimer held;
        held.start();
        do {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(10);
        } while (window->isVisible() && held.elapsed() < std::max(800, holdSeconds * 1000));
        if (!reportPath.isEmpty() && window->isVisible()) window->grabWindow().save(reportPath + QStringLiteral(".png"));
        controller.stopAllPaperStrategies();
        controller.stopAllPaperStrategies();
        require(controller.paperStrategyRuns().size() == 1
            && controller.paperStrategyRuns().first().toMap().value("stopped").toBool(),
            QStringLiteral("重复停止保留账户和运行记录"));
        window->close();
        controller.shutdown();
    } catch (const std::exception &error) {
        failure = QString::fromUtf8(error.what());
    }
    QFile report(reportPath);
    if (!report.open(QIODevice::WriteOnly)) return 3;
    report.write(QJsonDocument(QJsonObject{{"passed", failure.isEmpty()}, {"failure", failure},
        {"scope", "offline-controller-and-qml-only"}, {"qtVersion", QString::fromLatin1(qVersion())},
        {"checks", checks}}).toJson(QJsonDocument::Indented));
    return failure.isEmpty() ? 0 : 1;
}
