#include "services/CompositeMarketDataProvider.h"

#include "services/MootdxMarketDataProvider.h"
#include "services/RealtimeProviderRouter.h"
#include "services/TencentMarketDataProvider.h"

#include <QProcessEnvironment>

namespace pp {

CompositeMarketDataProvider::CompositeMarketDataProvider(QObject *parent)
    : MarketDataProvider(parent)
{
    m_router = new RealtimeProviderRouter(); // no parent — whole tree moves to worker thread
    const QString preferredProvider = qEnvironmentVariable(
        "PPTRADER_REALTIME_PROVIDER").trimmed().toLower();
    const bool tdxEnabled = preferredProvider != QStringLiteral("tencent");
    const bool tencentOnly = preferredProvider == QStringLiteral("tencent");
    const QString legacyServer = qEnvironmentVariable("PPTRADER_TDX_SERVER").trimmed();
    const QString primaryServer = qEnvironmentVariable(
        "PPTRADER_TDX_PRIMARY_SERVER", legacyServer.isEmpty()
            ? QStringLiteral("202.108.253.139:80") : legacyServer).trimmed();
    const QString backupServer = qEnvironmentVariable(
        "PPTRADER_TDX_BACKUP_SERVER", QStringLiteral("180.153.18.172:80")).trimmed();
    auto *primary = new MootdxMarketDataProvider(primaryServer, m_router);
    auto *backup = new MootdxMarketDataProvider(backupServer, m_router);
    auto *tencent = new TencentMarketDataProvider(m_router);
    auto *discovery = new MootdxMarketDataProvider({}, m_router);
    RealtimeProviderCapability primaryCapability;
    primaryCapability.providerId = QStringLiteral("public_tdx_primary");
    primaryCapability.displayName = QStringLiteral("公共 TDX 主源");
    primaryCapability.transport = QStringLiteral("TCP polling");
    primaryCapability.priority = 10;
    primaryCapability.configured = tdxEnabled && !primaryServer.isEmpty();
    primaryCapability.fullMarket = true;
    primaryCapability.polling = true;
    primaryCapability.minimumPeriodSeconds = 60;
    primaryCapability.pollingIntervalSeconds = 60;
    primaryCapability.watchdogTimeoutMilliseconds = 150000;
    primaryCapability.configurationMessage = QStringLiteral("免费兼容源，无生产 SLA");
    RealtimeProviderCapability backupCapability = primaryCapability;
    backupCapability.providerId = QStringLiteral("public_tdx_backup");
    backupCapability.displayName = QStringLiteral("公共 TDX 备用源");
    backupCapability.priority = 20;
    backupCapability.configured = tdxEnabled && !backupServer.isEmpty()
        && backupServer != primaryServer;
    backupCapability.configurationMessage = backupCapability.configured
        ? QStringLiteral("主动/被动备用端点")
        : QStringLiteral("备用端点未配置或与主源重复");
    m_router->addProvider(primary, primaryCapability);
    m_router->addProvider(backup, backupCapability);
    const QString tencentSetting = qEnvironmentVariable("PPTRADER_TENCENT_ENABLED")
        .trimmed().toLower();
    RealtimeProviderCapability tencentCapability;
    tencentCapability.providerId = QStringLiteral("tencent_public_quote");
    tencentCapability.displayName = QStringLiteral("腾讯公开行情源");
    tencentCapability.transport = QStringLiteral("HTTPS polling");
    tencentCapability.priority = 30;
    tencentCapability.configured = preferredProvider != QStringLiteral("tdx")
        && tencentSetting != QStringLiteral("0")
        && tencentSetting != QStringLiteral("false")
        && tencentSetting != QStringLiteral("off");
    tencentCapability.fullMarket = true;
    tencentCapability.rawTrades = false;
    tencentCapability.polling = true;
    tencentCapability.nativeMinuteBars = true;
    tencentCapability.minimumPeriodSeconds = 60;
    tencentCapability.pollingIntervalSeconds = 60;
    tencentCapability.watchdogTimeoutMilliseconds = 150000;
    tencentCapability.configurationMessage = tencentCapability.configured
        ? QStringLiteral("公开网页接口；支持 1 分钟 K 线，但无正式 SLA")
        : QStringLiteral("已通过 PPTRADER_TENCENT_ENABLED 禁用");
    m_router->addProvider(tencent, tencentCapability);
    RealtimeProviderCapability discoveryCapability = primaryCapability;
    discoveryCapability.providerId = QStringLiteral("public_tdx_discovery");
    discoveryCapability.displayName = QStringLiteral("公共 TDX 自动发现");
    discoveryCapability.priority = 40;
    discoveryCapability.configured = tdxEnabled && legacyServer.isEmpty();
    discoveryCapability.configurationMessage = discoveryCapability.configured
        ? QStringLiteral("仅在固定主备均不可用时启动服务器发现")
        : tencentOnly
            ? QStringLiteral("已选择腾讯单源验证模式")
            : QStringLiteral("已配置 legacy 固定端点，避免重复候选");
    m_router->addProvider(discovery, discoveryCapability);
    m_router->moveToThread(&m_workerThread);

    connect(m_router, &MarketDataProvider::tickReceived,
            this, &MarketDataProvider::tickReceived, Qt::QueuedConnection);
    connect(m_router, &MarketDataProvider::snapshotReceived,
            this, &MarketDataProvider::snapshotReceived, Qt::QueuedConnection);
    connect(m_router, &MarketDataProvider::providerError,
            this, &MarketDataProvider::providerError, Qt::QueuedConnection);
    connect(m_router, &MarketDataProvider::providerStateChanged,
            this, &MarketDataProvider::providerStateChanged, Qt::QueuedConnection);
    connect(m_router, &RealtimeProviderRouter::routeStateChanged,
            this, &MarketDataProvider::providerRouteChanged, Qt::QueuedConnection);
    connect(&m_workerThread, &QThread::finished,
            m_router, &QObject::deleteLater);

    m_workerThread.start();
}

CompositeMarketDataProvider::~CompositeMarketDataProvider()
{
    shutdown();
}

void CompositeMarketDataProvider::shutdown()
{
    if (!m_workerThread.isRunning()) return;
    if (QThread::currentThread() == &m_workerThread) {
        m_router->stop();
        m_workerThread.quit();
        return;
    }
    QMetaObject::invokeMethod(m_router, [this]() { m_router->stop(); },
                              Qt::BlockingQueuedConnection);
    m_workerThread.quit();
    m_workerThread.wait(3000);
}

void CompositeMarketDataProvider::start(const QVector<Instrument> &instruments)
{
    QVector<Instrument> aShares;

    for (const auto &inst : instruments) {
        if (inst.market == Market::AShare) {
            aShares.append(inst);
        }
    }

    if (!aShares.isEmpty()) {
        // invoke on worker thread
        QMetaObject::invokeMethod(m_router, [this, aShares]() {
            m_router->start(aShares);
        }, Qt::QueuedConnection);
    }
}

void CompositeMarketDataProvider::stop()
{
    QMetaObject::invokeMethod(m_router, [this]() {
        m_router->stop();
    }, Qt::QueuedConnection);
}

} // namespace pp
