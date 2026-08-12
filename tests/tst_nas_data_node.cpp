#include "services/NasDataNodeClient.h"
#include "services/NasSyncStateStore.h"
#include "services/NasTickCacheManager.h"
#include "services/NasTickSyncQueue.h"

#include <QCryptographicHash>
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace pp;

namespace {

QByteArray httpResponse(int status, const QByteArray &contentType, const QByteArray &body)
{
    const QByteArray reason = status == 200 ? "OK" : "Not Found";
    return "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n"
        + "Content-Type: " + contentType + "\r\n"
        + "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
        + "Connection: close\r\n\r\n" + body;
}

class FakeNasServer final : public QTcpServer {
    Q_OBJECT
public:
    QByteArray tickData = "09:30:00,10.00,1,1000,B\n09:30:30,10.10,2,2020,S\n";
    QByteArray expectedAuthorization = "Bearer test-token";
    int catalogRequests = 0;
    int manifestRequests = 0;
    int downloadRequests = 0;
    bool authorized = true;
    bool enforceAuthorization = false;
    QByteArray lastAuthorization;
    bool resumeSupported = true;
    int failAfterBytes = -1;

protected:
    void incomingConnection(qintptr handle) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray request = socket->property("request").toByteArray();
            request += socket->readAll();
            socket->setProperty("request", request);
            const int headerEnd = request.indexOf("\r\n\r\n");
            if (headerEnd < 0) return;
            int contentLength = 0;
            const QRegularExpression lengthExpression(
                QStringLiteral("Content-Length: ([0-9]+)"),
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch lengthMatch = lengthExpression.match(
                QString::fromLatin1(request.left(headerEnd)));
            if (lengthMatch.hasMatch()) contentLength = lengthMatch.captured(1).toInt();
            if (request.size() < headerEnd + 4 + contentLength) return;
            authorized = authorized && request.contains(
                "Authorization: " + expectedAuthorization + "\r\n");
            const QByteArray firstLine = request.left(request.indexOf("\r\n"));
            const QRegularExpression authExpression(
                QStringLiteral("Authorization: ([^\r\n]+)"),
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch authMatch = authExpression.match(
                QString::fromLatin1(request.left(headerEnd)));
            if (authMatch.hasMatch()) {
                lastAuthorization = authMatch.captured(1).toLatin1();
            }
            if (enforceAuthorization
                && lastAuthorization != expectedAuthorization) {
                socket->write(httpResponse(
                    401, "application/json", "{\"error\":\"unauthorized\"}"));
                socket->disconnectFromHost();
                return;
            }
            if (firstLine.contains("POST /v1/manifest ")) {
                ++manifestRequests;
                const QByteArray digest = QCryptographicHash::hash(
                    tickData, QCryptographicHash::Sha256).toHex();
                const QByteArray entry600519 = QByteArray("{")
                    + "\"symbol\":\"600519\",\"tradingDate\":\"2026-08-07\"," 
                    + "\"format\":\"tick_csv\",\"size\":"
                    + QByteArray::number(tickData.size()) + ",\"sha256\":\"" + digest
                    + "\",\"version\":\"sha256:" + digest
                    + "\",\"downloadPath\":\"/v1/ticks/600519/20260807\"}";
                const QByteArray entry000001 = QByteArray("{")
                    + "\"symbol\":\"000001\",\"tradingDate\":\"2026-08-07\"," 
                    + "\"format\":\"tick_csv\",\"size\":"
                    + QByteArray::number(tickData.size()) + ",\"sha256\":\"" + digest
                    + "\",\"version\":\"sha256:" + digest
                    + "\",\"downloadPath\":\"/v1/ticks/000001/20260807\"}";
                const QByteArray body = QByteArray(
                    "{\"schemaVersion\":1,\"requestedSymbols\":3,\"entries\":[")
                    + entry600519 + "," + entry000001 + "]}";
                socket->write(httpResponse(200, "application/json", body));
            } else if (firstLine.contains("/v1/catalog?")) {
                ++catalogRequests;
                const QByteArray digest = QCryptographicHash::hash(
                    tickData, QCryptographicHash::Sha256).toHex();
                const QByteArray body = QByteArray("{\"schemaVersion\":1,\"entries\":[{")
                    + "\"symbol\":\"600519\",\"tradingDate\":\"2026-08-07\","
                    + "\"format\":\"tick_csv\",\"size\":"
                    + QByteArray::number(tickData.size()) + ",\"sha256\":\"" + digest
                    + "\",\"version\":\"sha256:" + digest
                    + "\",\"downloadPath\":\"/v1/ticks/600519/20260807\"}]}";
                socket->write(httpResponse(200, "application/json", body));
            } else if (firstLine.contains("/v1/ticks/600519/20260807")) {
                ++downloadRequests;
                QByteArray payload = tickData;
                qint64 start = 0;
                const QRegularExpression rangeExpression(
                    QStringLiteral("Range: bytes=([0-9]+)-"),
                    QRegularExpression::CaseInsensitiveOption);
                const QRegularExpressionMatch rangeMatch = rangeExpression.match(
                    QString::fromLatin1(request.left(headerEnd)));
                if (resumeSupported && rangeMatch.hasMatch()) {
                    start = rangeMatch.captured(1).toLongLong();
                }
                QByteArray response;
                if (start > 0 && resumeSupported) {
                    payload = payload.mid(start);
                    response = QByteArray("HTTP/1.1 206 Partial Content\r\n")
                        + "Content-Type: text/csv\r\n"
                        + "Content-Range: bytes " + QByteArray::number(start)
                        + "-" + QByteArray::number(tickData.size() - 1)
                        + "/" + QByteArray::number(tickData.size()) + "\r\n"
                        + "Content-Length: " + QByteArray::number(payload.size())
                        + "\r\nConnection: close\r\n\r\n" + payload;
                } else {
                    response = httpResponse(200, "text/csv", tickData);
                    payload = tickData;
                }
                if (failAfterBytes > 0 && payload.size() > failAfterBytes) {
                    const int headerEndOffset =
                        response.indexOf("\r\n\r\n") + 4;
                    socket->write(response.left(
                        headerEndOffset + failAfterBytes));
                } else {
                    socket->write(response);
                }
                socket->disconnectFromHost();
            } else if (firstLine.contains("/v1/ticks/000001/20260807")) {
                ++downloadRequests;
                QByteArray payload = tickData;
                qint64 start = 0;
                const QRegularExpression rangeExpression(
                    QStringLiteral("Range: bytes=([0-9]+)-"),
                    QRegularExpression::CaseInsensitiveOption);
                const QRegularExpressionMatch rangeMatch = rangeExpression.match(
                    QString::fromLatin1(request.left(headerEnd)));
                if (resumeSupported && rangeMatch.hasMatch()) {
                    start = rangeMatch.captured(1).toLongLong();
                }
                QByteArray response;
                if (start > 0 && resumeSupported) {
                    payload = payload.mid(start);
                    response = QByteArray("HTTP/1.1 206 Partial Content\r\n")
                        + "Content-Type: text/csv\r\n"
                        + "Content-Range: bytes " + QByteArray::number(start)
                        + "-" + QByteArray::number(tickData.size() - 1)
                        + "/" + QByteArray::number(tickData.size()) + "\r\n"
                        + "Content-Length: " + QByteArray::number(payload.size())
                        + "\r\nConnection: close\r\n\r\n" + payload;
                } else {
                    response = httpResponse(200, "text/csv", tickData);
                    payload = tickData;
                }
                if (failAfterBytes > 0 && payload.size() > failAfterBytes) {
                    const int headerEndOffset =
                        response.indexOf("\r\n\r\n") + 4;
                    socket->write(response.left(
                        headerEndOffset + failAfterBytes));
                } else {
                    socket->write(response);
                }
                socket->disconnectFromHost();
            } else if (firstLine.contains("/v1/health")) {
                socket->write(httpResponse(
                    200, "application/json", "{\"status\":\"ok\",\"version\":\"test-v1\"}"));
            } else {
                socket->write(httpResponse(404, "application/json", "{}"));
            }
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
};

} // namespace

class NasDataNodeTest final : public QObject {
    Q_OBJECT

private slots:
    void endpointPolicy();
    void catalogDownloadChecksumAndCacheReuse();
    void controlledManifestRequestAndQueue();
    void resumeDownloadAfterInterruption();
    void tokenRotationHotReloadAndRetry();
    void multiDayQueueFailureResumeAndCancellation();
    void persistedQueueRecoveryAndCacheRetention();
};

void NasDataNodeTest::endpointPolicy()
{
    QString error;
    QVERIFY(NasDataNodeClient::validateEndpoint(QUrl(QStringLiteral("http://127.0.0.1:8765")),
                                                &error));
    QVERIFY(!NasDataNodeClient::validateEndpoint(
        QUrl(QStringLiteral("https://user:secret@example.invalid")), &error));
    QVERIFY(error.contains(QStringLiteral("用户名或密码")));
    const QByteArray previous = qgetenv("PPTRADER_ALLOW_INSECURE_NAS");
    qunsetenv("PPTRADER_ALLOW_INSECURE_NAS");
    QVERIFY(!NasDataNodeClient::validateEndpoint(
        QUrl(QStringLiteral("http://192.0.2.1:8765")), &error));
    QVERIFY(error.contains(QStringLiteral("HTTPS")));
    if (previous.isNull()) qunsetenv("PPTRADER_ALLOW_INSECURE_NAS");
    else qputenv("PPTRADER_ALLOW_INSECURE_NAS", previous);
}

void NasDataNodeTest::controlledManifestRequestAndQueue()
{
    FakeNasServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    NasDataNodeClient client;
    QString error;
    QVERIFY(client.setEndpoint(QUrl(QStringLiteral("http://127.0.0.1:%1/")
                                        .arg(server.serverPort())), &error));
    client.setBearerToken("test-token");
    QSignalSpy catalogSpy(&client, &NasDataNodeClient::catalogReady);
    const QStringList symbols{QStringLiteral("600519"), QStringLiteral("000001"),
                              QStringLiteral("300750")};
    QVERIFY(client.requestManifest(20, symbols,
                                   QDate(2026, 8, 7), QDate(2026, 8, 7)));
    QTRY_COMPARE_WITH_TIMEOUT(catalogSpy.count(), 1, 5000);
    const NasCatalogResult manifest =
        qvariant_cast<NasCatalogResult>(catalogSpy.takeFirst().at(1));
    QVERIFY2(manifest.succeeded, qPrintable(manifest.error));
    QCOMPARE(manifest.requestedSymbols, 3);
    QCOMPARE(manifest.availableSymbols, 2);
    QCOMPARE(manifest.missingSymbols, 1);
    QCOMPARE(manifest.entries.size(), 2);
    QCOMPARE(server.manifestRequests, 1);
    QVERIFY(server.authorized);

    NasTickSyncQueue queue;
    QVector<NasCatalogEntry> entries = manifest.entries;
    entries.append(manifest.entries.first());
    QVERIFY(queue.prepareManifest(entries, symbols));
    QCOMPARE(queue.items().size(), 2);
    QCOMPARE(queue.items().first().entry.symbol, QStringLiteral("000001"));
    QVERIFY(queue.start());
    int index = queue.takeNextReady();
    QVERIFY(queue.markImporting(index, 12, true));
    QVERIFY(queue.markSucceeded(index, 4000, 239, 3, 5, 2, 11));
    index = queue.takeNextReady();
    QVERIFY(queue.markImporting(index, 20, false));
    QVERIFY(queue.markSucceeded(index, 4100, 240, 4, 6, 3, 14));
    QCOMPARE(queue.takeNextReady(), -1);
    const NasSyncSummary summary = queue.summary();
    QCOMPARE(summary.symbols, 2);
    QCOMPARE(summary.succeeded, 2);
    QCOMPARE(summary.cacheHits, 1);
    QCOMPARE(summary.rawTicksWritten, 8100);
    QCOMPARE(summary.candleBarsWritten, 479);
    QCOMPARE(summary.transferElapsedMilliseconds, 32);
    QCOMPARE(summary.parseElapsedMilliseconds, 7);
    QCOMPARE(summary.rawDatabaseElapsedMilliseconds, 11);
    QCOMPARE(summary.candleDatabaseElapsedMilliseconds, 5);
}

void NasDataNodeTest::catalogDownloadChecksumAndCacheReuse()
{
    FakeNasServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    NasDataNodeClient client;
    QString error;
    QVERIFY(client.setEndpoint(QUrl(QStringLiteral("http://127.0.0.1:%1/")
                                        .arg(server.serverPort())), &error));
    client.setBearerToken("test-token");

    QSignalSpy catalogSpy(&client, &NasDataNodeClient::catalogReady);
    QVERIFY(client.requestCatalog(1, QStringLiteral("600519"),
                                  QDate(2026, 8, 7), QDate(2026, 8, 7)));
    QTRY_COMPARE_WITH_TIMEOUT(catalogSpy.count(), 1, 5000);
    const NasCatalogResult catalog =
        qvariant_cast<NasCatalogResult>(catalogSpy.takeFirst().at(1));
    QVERIFY2(catalog.succeeded, qPrintable(catalog.error));
    QCOMPARE(catalog.entries.size(), 1);
    QCOMPARE(catalog.entries.first().size, server.tickData.size());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSignalSpy downloadSpy(&client, &NasDataNodeClient::downloadReady);
    QVERIFY(client.downloadTickFile(2, catalog.entries.first(), directory.path()));
    QTRY_COMPARE_WITH_TIMEOUT(downloadSpy.count(), 1, 5000);
    NasDownloadResult download =
        qvariant_cast<NasDownloadResult>(downloadSpy.takeFirst().at(1));
    QVERIFY2(download.succeeded, qPrintable(download.error));
    QVERIFY(!download.fromCache);
    QFile file(download.localFilePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), server.tickData);
    QCOMPARE(server.downloadRequests, 1);
    QVERIFY(server.authorized);

    QVERIFY(client.downloadTickFile(3, catalog.entries.first(), directory.path()));
    QCOMPARE(downloadSpy.count(), 1);
    download = qvariant_cast<NasDownloadResult>(downloadSpy.takeFirst().at(1));
    QVERIFY(download.succeeded);
    QVERIFY(download.fromCache);
    QCOMPARE(server.downloadRequests, 1);
}

void NasDataNodeTest::resumeDownloadAfterInterruption()
{
    FakeNasServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    NasDataNodeClient client;
    QString error;
    QVERIFY(client.setEndpoint(QUrl(QStringLiteral("http://127.0.0.1:%1/")
                                        .arg(server.serverPort())), &error));

    QSignalSpy catalogSpy(&client, &NasDataNodeClient::catalogReady);
    QVERIFY(client.requestCatalog(1, QStringLiteral("600519"),
                                  QDate(2026, 8, 7), QDate(2026, 8, 7)));
    QTRY_COMPARE_WITH_TIMEOUT(catalogSpy.count(), 1, 5000);
    const NasCatalogResult catalog =
        qvariant_cast<NasCatalogResult>(catalogSpy.takeFirst().at(1));
    QVERIFY2(catalog.succeeded, qPrintable(catalog.error));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    server.failAfterBytes = server.tickData.size() / 2;
    QSignalSpy downloadSpy(&client, &NasDataNodeClient::downloadReady);
    QVERIFY(client.downloadTickFile(2, catalog.entries.first(), directory.path()));
    QTRY_COMPARE_WITH_TIMEOUT(downloadSpy.count(), 1, 5000);
    NasDownloadResult interrupted =
        qvariant_cast<NasDownloadResult>(downloadSpy.takeFirst().at(1));
    QVERIFY(!interrupted.succeeded);
    QVERIFY(QFileInfo::exists(
        interrupted.localFilePath + QStringLiteral(".part")));
    QCOMPARE(QFileInfo(interrupted.localFilePath
                           + QStringLiteral(".part")).size(),
             server.tickData.size() / 2);
    QCOMPARE(server.downloadRequests, 1);

    // 恢复服务器后重试：应携带 Range 续传并最终校验通过。
    server.failAfterBytes = -1;
    QVERIFY(client.downloadTickFile(3, catalog.entries.first(), directory.path()));
    QTRY_COMPARE_WITH_TIMEOUT(downloadSpy.count(), 1, 5000);
    NasDownloadResult resumed =
        qvariant_cast<NasDownloadResult>(downloadSpy.takeFirst().at(1));
    QVERIFY2(resumed.succeeded, qPrintable(resumed.error));
    QVERIFY(!resumed.fromCache);
    QCOMPARE(server.downloadRequests, 2);
    QVERIFY(!QFileInfo::exists(resumed.localFilePath
                               + QStringLiteral(".part")));
    QFile file(resumed.localFilePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), server.tickData);
}

void NasDataNodeTest::tokenRotationHotReloadAndRetry()
{
    FakeNasServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    server.enforceAuthorization = true;
    server.expectedAuthorization = QByteArray("Bearer alpha");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString tokenPath = directory.filePath(QStringLiteral("token.txt"));
    {
        QFile tokenFile(tokenPath);
        QVERIFY(tokenFile.open(QIODevice::WriteOnly));
        tokenFile.write("alpha\n");
    }

    NasDataNodeClient client;
    QString error;
    QVERIFY(client.setEndpoint(QUrl(QStringLiteral("http://127.0.0.1:%1/")
                                        .arg(server.serverPort())), &error));
    client.setBearerTokenProvider([tokenPath]() {
        QFile file(tokenPath);
        if (file.open(QIODevice::ReadOnly)) {
            return file.readAll().trimmed();
        }
        return QByteArray{};
    });

    QSignalSpy healthSpy(&client, &NasDataNodeClient::healthReady);
    QVERIFY(client.requestHealth(1));
    QTRY_COMPARE_WITH_TIMEOUT(healthSpy.count(), 1, 5000);
    QVERIFY(healthSpy.takeFirst().at(1).toBool());
    QCOMPARE(server.lastAuthorization, QByteArray("Bearer alpha"));

    // 文件热重载：不重建客户端，下一请求自动使用新 Token。
    server.expectedAuthorization = QByteArray("Bearer beta");
    {
        QFile tokenUpdate(tokenPath);
        QVERIFY(tokenUpdate.open(QIODevice::WriteOnly | QIODevice::Truncate));
        tokenUpdate.write("beta\n");
    }
    QVERIFY(client.requestHealth(2));
    QTRY_COMPARE_WITH_TIMEOUT(healthSpy.count(), 1, 5000);
    QVERIFY(healthSpy.takeFirst().at(1).toBool());
    QCOMPARE(server.lastAuthorization, QByteArray("Bearer beta"));

    // 401 自愈：首次携带旧 Token 被拒，重试时使用新 Token 成功。
    server.expectedAuthorization = QByteArray("Bearer gamma");
    int providerCalls = 0;
    client.setBearerTokenProvider([&providerCalls]() {
        return providerCalls++ == 0
            ? QByteArray("old") : QByteArray("gamma");
    });
    QVERIFY(client.requestHealth(3));
    QTRY_COMPARE_WITH_TIMEOUT(healthSpy.count(), 1, 5000);
    QVERIFY(healthSpy.takeFirst().at(1).toBool());
    QCOMPARE(server.lastAuthorization, QByteArray("Bearer gamma"));
}

void NasDataNodeTest::multiDayQueueFailureResumeAndCancellation()
{
    // 并发下载与多活动队列（测试前置：直接插入独立用例）
    {
        auto entry = [](const QString &symbol) {
            NasCatalogEntry value;
            value.symbol = symbol;
            value.tradingDate = QDate(2026, 8, 7);
            value.format = QStringLiteral("tick_csv");
            value.size = 100;
            value.sha256 = QString(64, QLatin1Char('a'));
            value.version = QStringLiteral("sha256:") + QString(64, QLatin1Char('a'));
            value.downloadPath =
                QStringLiteral("/v1/ticks/%1/20260807").arg(symbol);
            return value;
        };
        NasTickSyncQueue queue;
        QVERIFY(queue.prepareManifest(
            {entry(QStringLiteral("600519")), entry(QStringLiteral("000001"))},
            {QStringLiteral("600519"), QStringLiteral("000001")}));
        QVERIFY(queue.start());
        const QVector<int> batch = queue.takeReadyBatch(2);
        QCOMPARE(batch.size(), 2);
        QCOMPARE(queue.activeCount(), 2);
        QCOMPARE(queue.downloadingCount(), 2);
        QVERIFY(queue.markImporting(batch.at(0), 10, false));
        QVERIFY(queue.markSucceeded(batch.at(0), 4000, 239));
        QCOMPARE(queue.activeCount(), 1);
        QVERIFY(queue.markImporting(batch.at(1), 20, true));
        QVERIFY(queue.markSucceeded(batch.at(1), 4100, 240));
        QCOMPARE(queue.activeCount(), 0);
        QVERIFY(queue.takeReadyBatch(2).isEmpty());
        QVERIFY(!queue.running());
    }
    {
        FakeNasServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        NasDataNodeClient client;
        QString error;
        QVERIFY(client.setEndpoint(QUrl(QStringLiteral("http://127.0.0.1:%1/")
                                            .arg(server.serverPort())), &error));
        QSignalSpy manifestSpy(&client, &NasDataNodeClient::catalogReady);
        QVERIFY(client.requestManifest(1, {QStringLiteral("600519"),
                                           QStringLiteral("000001")},
                                       QDate(2026, 8, 7), QDate(2026, 8, 7)));
        QTRY_COMPARE_WITH_TIMEOUT(manifestSpy.count(), 1, 5000);
        const NasCatalogResult manifest =
            qvariant_cast<NasCatalogResult>(manifestSpy.takeFirst().at(1));
        QVERIFY2(manifest.succeeded, qPrintable(manifest.error));
        QCOMPARE(manifest.entries.size(), 2);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSignalSpy downloadSpy(&client, &NasDataNodeClient::downloadReady);
        QVERIFY(client.downloadTickFile(11, manifest.entries.at(0), directory.path()));
        QVERIFY(client.downloadTickFile(12, manifest.entries.at(1), directory.path()));
        QTRY_COMPARE_WITH_TIMEOUT(downloadSpy.count(), 2, 5000);
        QSet<QString> symbols;
        for (const QList<QVariant> &arguments : downloadSpy) {
            const NasDownloadResult result =
                qvariant_cast<NasDownloadResult>(arguments.at(1));
            QVERIFY2(result.succeeded, qPrintable(result.error));
            symbols.insert(result.entry.symbol);
        }
        QCOMPARE(symbols.size(), 2);
        QCOMPARE(server.downloadRequests, 2);
    }

    auto entry = [](const QDate &date) {
        NasCatalogEntry value;
        value.symbol = QStringLiteral("600519");
        value.tradingDate = date;
        value.format = QStringLiteral("tick_csv");
        value.size = 100;
        value.sha256 = QString(64, QLatin1Char('a'));
        value.downloadPath = QStringLiteral("/v1/ticks/600519/%1")
                                 .arg(date.toString(QStringLiteral("yyyyMMdd")));
        return value;
    };
    const QDate first(2026, 8, 5);
    const QDate second(2026, 8, 6);
    const QDate third(2026, 8, 7);
    NasTickSyncQueue queue;
    QVERIFY(queue.prepare({entry(third), entry(first), entry(second), entry(second)},
                          QStringLiteral("600519")));
    QCOMPARE(queue.items().size(), 3);
    QCOMPARE(queue.items().first().entry.tradingDate, first);
    QVERIFY(queue.start());

    int index = queue.takeNextReady();
    QCOMPARE(index, 0);
    QVERIFY(queue.markImporting(index));
    QVERIFY(queue.markSucceeded(index, 4000, 239));
    index = queue.takeNextReady();
    QCOMPARE(index, 1);
    QVERIFY(queue.markFailed(index, QStringLiteral("受控失败")));
    index = queue.takeNextReady();
    QCOMPARE(index, 2);
    QVERIFY(queue.markImporting(index));
    QVERIFY(queue.markSucceeded(index, 4100, 240));
    QCOMPARE(queue.takeNextReady(), -1);
    NasSyncSummary summary = queue.summary();
    QCOMPARE(summary.completed, 3);
    QCOMPARE(summary.succeeded, 2);
    QCOMPARE(summary.failed, 1);
    QVERIFY(queue.canResume());

    QVERIFY(queue.resume());
    index = queue.takeNextReady();
    QCOMPARE(index, 1);
    QVERIFY(queue.markImporting(index));
    QVERIFY(queue.markSucceeded(index, 4050, 239));
    QCOMPARE(queue.takeNextReady(), -1);
    summary = queue.summary();
    QCOMPARE(summary.succeeded, 3);
    QCOMPARE(summary.failed, 0);
    QVERIFY(!queue.canResume());
    QCOMPARE(queue.items().at(1).toVariantMap().value(
                 QStringLiteral("statusText")).toString(), QStringLiteral("已完成"));

    QVERIFY(queue.start());
    QCOMPARE(queue.takeNextReady(), 0);
    queue.cancelActiveAndPending();
    QVERIFY(!queue.running());
    QVERIFY(queue.canResume());
    QCOMPARE(queue.items().first().status, NasSyncItemStatus::Cancelled);
    QVERIFY(queue.resume());
}

void NasDataNodeTest::persistedQueueRecoveryAndCacheRetention()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto entry = [](const QString &symbol, const QDate &date) {
        NasCatalogEntry value;
        value.symbol = symbol;
        value.tradingDate = date;
        value.format = QStringLiteral("tick_csv");
        value.size = 100;
        value.sha256 = QString(64, QLatin1Char('a'));
        value.version = QStringLiteral("sha256:") + value.sha256;
        value.downloadPath = QStringLiteral("/v1/ticks/%1/%2")
                                 .arg(symbol, date.toString(QStringLiteral("yyyyMMdd")));
        return value;
    };
    NasTickSyncQueue queue;
    const QStringList symbols{QStringLiteral("600519"), QStringLiteral("000001")};
    QVERIFY(queue.prepareManifest(
        {entry(symbols.at(0), QDate(2026, 8, 7)),
         entry(symbols.at(1), QDate(2026, 8, 7))}, symbols));
    QVERIFY(queue.start());
    const int active = queue.takeNextReady();
    QVERIFY(queue.markImporting(active, 7, false));

    NasSyncStateStore store;
    const QString statePath = directory.filePath(QStringLiteral("state/sync.json"));
    store.setFilePath(statePath);
    NasPersistedSyncState saved{symbols, queue.items()};
    QString error;
    QVERIFY2(store.save(saved, &error), qPrintable(error));
    NasPersistedSyncState loaded;
    QVERIFY2(store.load(&loaded, &error), qPrintable(error));
    QCOMPARE(loaded.items.size(), 2);
    QCOMPARE(loaded.items.first().status, NasSyncItemStatus::Cancelled);
    QVERIFY(loaded.items.first().error.contains(QStringLiteral("可重试")));
    NasTickSyncQueue restored;
    QVERIFY(restored.restore(loaded.items));
    QVERIFY(restored.canResume());
    QVERIFY(restored.resume());

    QFile corrupt(statePath);
    QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(corrupt.write("not-json") > 0);
    corrupt.close();
    QVERIFY(!store.load(&loaded, &error));
    QVERIFY(error.contains(QStringLiteral("JSON")));

    NasPersistedSyncState tampered = saved;
    tampered.items.first().entry.downloadPath = QStringLiteral(
        "/v1/ticks/999999/20260807");
    QVERIFY(!store.save(tampered, &error));
    QVERIFY(error.contains(QStringLiteral("无效条目")));

    const QString cacheRoot = directory.filePath(QStringLiteral("cache"));
    const QString dateRoot = QDir(cacheRoot).filePath(QStringLiteral("20260807"));
    QVERIFY(QDir().mkpath(dateRoot));
    const QDateTime now(QDate(2026, 8, 9), QTime(12, 0), Qt::UTC);
    auto writeFile = [](const QString &path, const QByteArray &bytes,
                        const QDateTime &modified) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
        const bool timeSet = file.setFileTime(modified, QFileDevice::FileModificationTime);
        file.close();
        return timeSet;
    };
    const QString oldFile = QDir(dateRoot).filePath(QStringLiteral("600519.csv"));
    const QString youngFile = QDir(dateRoot).filePath(QStringLiteral("000001.csv"));
    const QString unrelated = QDir(dateRoot).filePath(QStringLiteral("notes.txt"));
    QVERIFY(writeFile(oldFile, "old", now.addDays(-40)));
    QVERIFY(writeFile(youngFile, "young", now.addDays(-2)));
    QVERIFY(writeFile(unrelated, "keep", now.addDays(-50)));
    NasTickCacheManager cache;
    cache.setRootDirectory(cacheRoot);
    NasTickCacheSummary summary = cache.scan();
    QCOMPARE(summary.files, 2);
    QCOMPARE(summary.bytes, 8);
    QVERIFY2(cache.pruneOlderThan(30, &summary, &error, now), qPrintable(error));
    QCOMPARE(summary.removedFiles, 1);
    QCOMPARE(summary.removedBytes, 3);
    QCOMPARE(summary.files, 1);
    QVERIFY(!QFile::exists(oldFile));
    QVERIFY(QFile::exists(youngFile));
    QVERIFY(QFile::exists(unrelated));
    QVERIFY(!cache.pruneOlderThan(0, &summary, &error, now));
}

QTEST_GUILESS_MAIN(NasDataNodeTest)
#include "tst_nas_data_node.moc"
