#include "app/TradingController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrl>
#include <QFileInfo>
#include <algorithm>
#include <limits>

namespace pp {

QVariantList TradingController::paperStrategyRuns() const {
    return m_paperStrategyBook.snapshots() + m_paperReplayBook.snapshots();
}
QString TradingController::paperStrategyError() const { return m_paperStrategyError; }

void TradingController::initializePaperTrading()
{
    m_paperStrategyBook.bindAccount(m_tradingAccount, m_broker);
    m_paperReplayBook.bindAccount(m_tradingAccount, m_broker);
    m_paperStateNotifyTimer.setSingleShot(true);
    m_paperStateNotifyTimer.setInterval(100);
    connect(&m_paperStateNotifyTimer, &QTimer::timeout, this, &TradingController::paperStrategyStateChanged);
    const auto scheduleDisplay = [this] {
        if (!m_paperStateNotifyTimer.isActive()) m_paperStateNotifyTimer.start();
    };
    connect(&m_paperStrategyBook, &PaperStrategyBook::changed, this, scheduleDisplay);
    connect(&m_paperReplayBook, &PaperStrategyBook::changed, this, scheduleDisplay);
    m_paperMinuteTimer.setSingleShot(true);
    connect(&m_paperMinuteTimer, &QTimer::timeout,
            this, &TradingController::refreshPaperMinuteSubscriptions);
}

bool TradingController::canStartPaperStrategy()
{
    QString error;
    if (m_paperStrategyBook.hasRun() || m_paperReplayBook.hasRun())
        error = QStringLiteral("工作台已有策略记录；停止保留资金持仓，安全接管验收前不能再次启动");
    else if (m_customStrategyReplayTimer.isActive())
        error = QStringLiteral("请先停止旧策略回放，不能混用账户输入时钟");
    else {
        for (const auto &config : m_strategyExecutionCoordinator.configurations())
            if (config.enabled) error = QStringLiteral("请先关闭旧策略执行配置，工作台只允许一个生效策略");
        for (const auto &order : m_tradingAccount.orders())
            if (isActiveOrder(order.status)) error = QStringLiteral("账户存在未完成委托；接管前须先处理");
    }
    m_paperStrategyError = error;
    if (!error.isEmpty()) emit paperStrategyStateChanged();
    return error.isEmpty();
}

qulonglong TradingController::publishPaperStrategyVersion(const QString &strategyId, int version)
{
    if (customStrategySaveLoading()) return 0;
    QJsonObject document;
    int latestVersion = 0;
    for (const auto &definition : m_customStrategyDefinitions) {
        if (definition.strategyId != strategyId) continue;
        latestVersion = std::max(latestVersion, definition.version);
        if (definition.version == version) document = definition.document;
    }
    if (document.isEmpty() || latestVersion == std::numeric_limits<int>::max()) {
        m_paperStrategyError = QStringLiteral("未找到所选版本，无法发布模拟副本");
        emit paperStrategyStateChanged();
        return 0;
    }
    document.insert(QStringLiteral("version"), latestVersion + 1);
    document.insert(QStringLiteral("executionScope"), QStringLiteral("mock_execution"));
    document.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    m_paperStrategyError.clear();
    emit paperStrategyStateChanged();
    // 显式创建新版本；不改变历史 backtest_only 定义，也不自动启动模拟或真实下单。
    return requestSaveCustomStrategyJson(QString::fromUtf8(QJsonDocument(document).toJson()));
}

QString TradingController::startPaperStrategyRun(const QString &strategyId, int version,
    const QString &symbol, double initialCapital, double commissionRate, double slippageRate)
{
    m_paperStrategyError.clear();
    const auto fail = [this](const QString &message) {
        m_paperStrategyError = message;
        emit paperStrategyStateChanged();
        return QString{};
    };
    if (!m_realtimeLiveSessionState.requested())
        return fail(QStringLiteral("请先启动顶部实时行情任务；模拟仓复用同一 Router，不额外启动 TDX 桥"));
    if (!canStartPaperStrategy()) return {};
    Q_UNUSED(initialCapital);
    if (commissionRate != m_broker.commissionRate() || slippageRate != m_broker.slippageRate())
        return fail(QStringLiteral("一仓使用工作台现有费率，不允许候选单独改变撮合费用"));
    const Instrument instrument = findInstrument(symbol.trimmed());
    if (instrument.symbol.isEmpty() || instrument.market != Market::AShare
        || !(instrument.symbol.startsWith('0') || instrument.symbol.startsWith('3')
             || instrument.symbol.startsWith('6')))
        return fail(QStringLiteral("请选择已加载股票池内的沪深 A 股代码"));
    for (const auto &definition : m_customStrategyDefinitions) {
        if (definition.strategyId != strategyId || definition.version != version) continue;
        PaperStrategyConfig config;
        config.initialCapital = m_tradingAccount.snapshot().initialEquity;
        config.commissionRate = commissionRate;
        config.slippageRate = slippageRate;
        QString error;
        const QString id = m_paperStrategyBook.createRun(definition, instrument, config,
                                                        QDateTime::currentDateTimeUtc(), &error);
        if (id.isEmpty()) return fail(error);
        LogService::instance().info(QStringLiteral("paper-strategy"),
            QStringLiteral("start run=%1 strategy=%2 version=%3 symbol=%4 capital=%5 scope=paper-only")
                .arg(id, strategyId).arg(version).arg(instrument.symbol).arg(config.initialCapital));
        refreshPaperMinuteSubscriptions();
        return id;
    }
    return fail(QStringLiteral("未找到所选不可变策略版本"));
}

void TradingController::clearUnusedPaperMinuteSubscriptions()
{
    const auto symbols = m_paperStrategyBook.subscribedSymbols();
    for (const auto &symbol : m_paperMinuteServices.keys()) {
        if (symbols.contains(symbol)) continue;
        auto *service = m_paperMinuteServices.take(symbol);
        service->cancel();
        service->deleteLater();
    }
    if (symbols.isEmpty()) m_paperMinuteTimer.stop();
}

void TradingController::refreshPaperMinuteSubscriptions()
{
    clearUnusedPaperMinuteSubscriptions();
    if (!m_realtimeLiveSessionState.requested()) return;
    const auto symbols = m_paperStrategyBook.subscribedSymbols();
    for (const auto &symbol : symbols) {
        auto *service = m_paperMinuteServices.value(symbol, nullptr);
        if (!service) {
            service = new TencentMinuteBarService(this);
            m_paperMinuteServices.insert(symbol, service);
            connect(service, &TencentMinuteBarService::resultReady, this,
                [this](quint64 requestId, const RealtimeMinuteSeriesResult &result) {
                    const bool accepted = m_paperStrategyBook.acceptMinutes(requestId, result);
                    LogService::instance().info(QStringLiteral("paper-minute"),
                        QStringLiteral("symbol=%1 requestId=%2 accepted=%3 rows=%4 error=%5")
                            .arg(result.instrument.symbol).arg(requestId).arg(accepted)
                            .arg(result.candles.size()).arg(result.error));
                    clearUnusedPaperMinuteSubscriptions();
                });
        }
        const quint64 requestId = m_paperStrategyBook.beginMinuteRequest(symbol);
        if (!requestId) continue;
        if (!service->request(requestId, findInstrument(symbol))) {
            RealtimeMinuteSeriesResult result;
            result.instrument = findInstrument(symbol);
            result.error = QStringLiteral("模拟分钟订阅提交失败");
            m_paperStrategyBook.acceptMinutes(requestId, result);
        }
    }
    if (!symbols.isEmpty()) m_paperMinuteTimer.start(
        TencentMinuteBarService::millisecondsUntilNextMinuteRefresh(QDateTime::currentDateTimeUtc()));
}

bool TradingController::stopPaperStrategyRun(const QString &runId)
{
    const bool stopped = m_paperStrategyBook.stopRun(runId) || m_paperReplayBook.stopRun(runId);
    clearUnusedPaperMinuteSubscriptions();
    return stopped;
}

void TradingController::stopAllPaperStrategies()
{
    m_paperMinuteTimer.stop();
    m_paperStrategyBook.stopAll();
    m_paperReplayBook.stopAll();
    clearUnusedPaperMinuteSubscriptions();
    emit paperStrategyStateChanged();
}

bool TradingController::exportPaperStrategyReport(const QUrl &destination)
{
    if (!destination.isLocalFile() || destination.toLocalFile().isEmpty()
        || QFileInfo(destination.toLocalFile()).suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0) {
        m_paperStrategyError = QStringLiteral("请选择本地 .json 报告文件；不允许覆盖数据库等其他类型文件");
        emit paperStrategyStateChanged();
        return false;
    }
    const QJsonObject document{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("scope"), m_paperReplayBook.snapshots().isEmpty() ? QStringLiteral("realtime-paper-only") : QStringLiteral("historical-replay-paper-only")},
        {QStringLiteral("exportedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("profitabilityAccepted"), false},
        {QStringLiteral("limitations"), QStringLiteral("研究分钟确认非交易所闭合证明；报价撮合无盘口深度、印花税和最小佣金模型；运行记录在内存，未覆盖重启恢复；不代表真实收益")},
        {QStringLiteral("runs"), QJsonArray::fromVariantList(paperStrategyRuns())}};
    QSaveFile file(destination.toLocalFile());
    const QByteArray data = QJsonDocument(document).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        m_paperStrategyError = QStringLiteral("导出模拟报告失败：%1").arg(file.errorString());
        emit paperStrategyStateChanged();
        return false;
    }
    m_paperStrategyError = QStringLiteral("模拟报告已导出；不代表策略已通过盈利验收");
    emit paperStrategyStateChanged();
    return true;
}

QString TradingController::startPaperReplayRun(const QString &strategyId, int version,
    const QString &symbol, const QDateTime &sourceStart, double initialCapital)
{
    PaperStrategyConfig config;
    config.initialCapital = initialCapital;
    return startPaperReplayPortfolio(strategyId, version, QStringList{symbol}, sourceStart, config);
}

QString TradingController::startPaperReplayPortfolio(const QString &strategyId, int version,
    const QStringList &symbols, const QDateTime &sourceStart, const PaperStrategyConfig &settings)
{
    if (!canStartPaperStrategy()) return {};
    if (m_realtimeLiveSessionState.requested()) {
        m_paperStrategyError = QStringLiteral("历史回放不可与真实行情模拟混用");
        emit paperStrategyStateChanged();
        return {};
    }
    // 旧账户已有交易时禁止倒灌历史；此处不清理或新建资金。
    if (!m_tradingAccount.orders().isEmpty() || !m_tradingAccount.executions().isEmpty()
        || !m_tradingAccount.positions().isEmpty()) {
        m_paperStrategyError = QStringLiteral("历史回放仅允许无交易的工作台账户，不能倒灌历史覆盖既有资金持仓");
        emit paperStrategyStateChanged();
        return {};
    }
    QVector<Instrument> instruments;
    for (const auto &symbol : symbols) instruments.append(findInstrument(symbol.trimmed()));
    for (const auto &definition : m_customStrategyDefinitions) {
        if (definition.strategyId != strategyId || definition.version != version) continue;
        PaperStrategyConfig config = settings;
        config.initialCapital = m_tradingAccount.snapshot().initialEquity;
        config.commissionRate = m_broker.commissionRate();
        config.slippageRate = m_broker.slippageRate();
        return m_paperReplayBook.createRun(definition, instruments, config,
                                          sourceStart, &m_paperStrategyError);
    }
    m_paperStrategyError = QStringLiteral("未找到历史回放的固定模拟版本");
    return {};
}

bool TradingController::acceptPaperReplayBar(const Candle &bar, const QDateTime &sourceClock, bool warmup)
{
    if (m_realtimeLiveSessionState.requested()) return false;
    return m_paperReplayBook.acceptReplayBar(bar, sourceClock, warmup);
}

void TradingController::acceptPaperReplayQuote(const Tick &quote)
{
    if (!m_realtimeLiveSessionState.requested()) m_paperReplayBook.acceptReplayQuote(quote);
}

} // namespace pp
