#include "services/NotificationDispatcher.h"
#include "services/WebhookNotificationService.h"

#include <QSignalSpy>
#include <QTest>

#include <functional>

namespace pp {

class FakeNotificationChannel final : public NotificationService {
public:
    explicit FakeNotificationChannel(QString name)
        : NotificationService()
        , m_name(std::move(name))
    {
    }

    QString channelName() const override { return m_name; }
    bool send(const NotificationMessage &message) override
    {
        messages.append(message);
        return !failSend;
    }

    QString m_name;
    QVector<NotificationMessage> messages;
    bool failSend = false;
};

class FakeWebhookTransport final : public WebhookTransport {
public:
    void post(const QString &url, const QByteArray &payload,
              std::function<void(bool, const QString &)> done) override
    {
        lastUrl = url;
        lastPayload = payload;
        if (done) done(success, error);
    }

    QString lastUrl;
    QByteArray lastPayload;
    bool success = true;
    QString error;
};

class NotificationServiceTest : public QObject {
    Q_OBJECT

private slots:
    void dispatcherRoutesByChannel();
    void dispatcherFiltersEvents();
    void dispatcherUnregisterChannel();
    void webhookBuildsPayload();
    void webhookEmptyUrlFails();
    void webhookTransportFailureSignals();
};

namespace {
TradeSignal makeSignal()
{
    TradeSignal signal;
    signal.instrument.symbol = QStringLiteral("600519");
    signal.side = SignalSide::Buy;
    signal.price = 100.0;
    signal.volume = 100;
    signal.reason = QStringLiteral("SMA 金叉");
    signal.timestamp = QDateTime::currentDateTime();
    return signal;
}

NotificationMessage makeMessage(NotificationEvent event)
{
    NotificationMessage message;
    message.event = event;
    message.title = QStringLiteral("标题");
    message.body = QStringLiteral("正文");
    message.timestamp = QDateTime::currentDateTime();
    return message;
}
} // namespace

void NotificationServiceTest::dispatcherRoutesByChannel()
{
    NotificationDispatcher dispatcher;
    FakeNotificationChannel desktop(QStringLiteral("desktop"));
    FakeNotificationChannel webhook(QStringLiteral("webhook"));
    dispatcher.registerChannel(&desktop);
    dispatcher.registerChannel(&webhook);

    NotificationSettings settings;
    settings.desktopEnabled = true;
    settings.webhookEnabled = false;
    dispatcher.setSettings(settings);

    dispatcher.notifySignal(makeSignal());

    QCOMPARE(desktop.messages.size(), 1);
    QCOMPARE(webhook.messages.size(), 0);
    QCOMPARE(desktop.messages.first().event, NotificationEvent::SignalGenerated);
    QVERIFY(desktop.messages.first().title.contains(QStringLiteral("600519")));
    QCOMPARE(dispatcher.channels().size(), 2);
}

void NotificationServiceTest::dispatcherFiltersEvents()
{
    NotificationDispatcher dispatcher;
    FakeNotificationChannel desktop(QStringLiteral("desktop"));
    dispatcher.registerChannel(&desktop);

    NotificationSettings settings;
    settings.desktopEnabled = true;
    settings.notifySignal = false;
    settings.notifyApproval = true;
    dispatcher.setSettings(settings);

    dispatcher.notifySignal(makeSignal());
    QCOMPARE(desktop.messages.size(), 0);

    StrategyOrderIntent intent;
    intent.strategyId = QStringLiteral("strategy-1");
    intent.signal = makeSignal();
    intent.orderRequest.quantity = 100;
    intent.createdAt = QDateTime::currentDateTime();
    dispatcher.notifyApprovalRequired(intent);
    QCOMPARE(desktop.messages.size(), 1);
    QCOMPARE(desktop.messages.first().event, NotificationEvent::ApprovalRequired);
}

void NotificationServiceTest::dispatcherUnregisterChannel()
{
    NotificationDispatcher dispatcher;
    FakeNotificationChannel desktop(QStringLiteral("desktop"));
    dispatcher.registerChannel(&desktop);

    NotificationSettings settings;
    settings.desktopEnabled = true;
    dispatcher.setSettings(settings);

    dispatcher.unregisterChannel(QStringLiteral("desktop"));
    dispatcher.notifySignal(makeSignal());
    QCOMPARE(desktop.messages.size(), 0);
    QCOMPARE(dispatcher.channels().size(), 0);
}

void NotificationServiceTest::webhookBuildsPayload()
{
    FakeWebhookTransport transport;
    WebhookNotificationService service;
    service.setTransport(&transport);
    service.setWebhookUrl(QStringLiteral("http://example.com/hook"));

    QSignalSpy sentSpy(&service, &NotificationService::notificationSent);

    QVERIFY(service.send(makeMessage(NotificationEvent::ApprovalRequired)));
    QCOMPARE(transport.lastUrl, QStringLiteral("http://example.com/hook"));
    QVERIFY(transport.lastPayload.contains("ApprovalRequired"));
    QVERIFY(QString::fromUtf8(transport.lastPayload).contains(QStringLiteral("标题")));
    QCOMPARE(sentSpy.count(), 1);
    QCOMPARE(sentSpy.first().at(0).toString(), QStringLiteral("webhook"));
}

void NotificationServiceTest::webhookEmptyUrlFails()
{
    FakeWebhookTransport transport;
    WebhookNotificationService service;
    service.setTransport(&transport);

    QSignalSpy failedSpy(&service, &NotificationService::notificationFailed);

    QVERIFY(!service.send(makeMessage(NotificationEvent::SignalGenerated)));
    QCOMPARE(transport.lastUrl, QString{});
    QCOMPARE(failedSpy.count(), 1);
}

void NotificationServiceTest::webhookTransportFailureSignals()
{
    FakeWebhookTransport transport;
    transport.success = false;
    transport.error = QStringLiteral("timeout");
    WebhookNotificationService service;
    service.setTransport(&transport);
    service.setWebhookUrl(QStringLiteral("http://example.com/hook"));

    QSignalSpy failedSpy(&service, &NotificationService::notificationFailed);

    QVERIFY(service.send(makeMessage(NotificationEvent::OrderSubmitted)));
    QCOMPARE(failedSpy.count(), 1);
    QVERIFY(failedSpy.first().at(1).toString().contains(QStringLiteral("timeout")));
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::NotificationServiceTest)

#include "tst_notification_service.moc"
