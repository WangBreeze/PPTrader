#include "services/NasDataNodeClient.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QUrlQuery>

namespace pp {

namespace {

QString normalizedDigest(QString value)
{
    value = value.trimmed().toLower();
    if (value.startsWith(QStringLiteral("sha256:"))) value.remove(0, 7);
    return value;
}

bool validDigest(const QString &value)
{
    const QString digest = normalizedDigest(value);
    if (digest.size() != 64) return false;
    for (const QChar character : digest) {
        if (!character.isDigit() && (character < QLatin1Char('a')
                                     || character > QLatin1Char('f'))) return false;
    }
    return true;
}

bool validSymbol(const QString &symbol)
{
    if (symbol.size() != 6) return false;
    for (const QChar character : symbol) {
        if (!character.isDigit()) return false;
    }
    return true;
}

bool validEntry(const NasCatalogEntry &entry)
{
    const QString digest = normalizedDigest(entry.sha256);
    const QString expectedPath = QStringLiteral("/v1/ticks/%1/%2")
        .arg(entry.symbol, entry.tradingDate.toString(QStringLiteral("yyyyMMdd")));
    return validSymbol(entry.symbol) && entry.tradingDate.isValid()
        && entry.format == QStringLiteral("tick_csv") && entry.size > 0
        && validDigest(digest)
        && entry.version == QStringLiteral("sha256:") + digest
        && entry.downloadPath == expectedPath;
}

QString fileDigest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

NasCatalogResult parseCatalogPayload(const QByteArray &payload,
                                     int requestedSymbols,
                                     int maximumPayloadBytes)
{
    NasCatalogResult result;
    result.requestedSymbols = requestedSymbols;
    if (payload.size() > maximumPayloadBytes) {
        result.error = QStringLiteral("NAS manifest 响应超过客户端限制");
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("NAS catalog JSON 无效");
        return result;
    }
    const QJsonObject root = document.object();
    result.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt();
    QSet<QString> availableSymbols;
    for (const QJsonValue &value : root.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject object = value.toObject();
        NasCatalogEntry entry;
        entry.symbol = object.value(QStringLiteral("symbol")).toString();
        entry.tradingDate = QDate::fromString(
            object.value(QStringLiteral("tradingDate")).toString(), Qt::ISODate);
        entry.format = object.value(QStringLiteral("format")).toString();
        entry.size = object.value(QStringLiteral("size")).toInteger();
        entry.sha256 = object.value(QStringLiteral("sha256")).toString();
        entry.version = object.value(QStringLiteral("version")).toString();
        entry.downloadPath = object.value(QStringLiteral("downloadPath")).toString();
        if (!validEntry(entry)) {
            result.error = QStringLiteral("NAS catalog 包含无效条目");
            result.entries.clear();
            return result;
        }
        result.totalBytes += entry.size;
        availableSymbols.insert(entry.symbol);
        result.entries.append(entry);
    }
    result.availableSymbols = availableSymbols.size();
    result.missingSymbols = qMax(0, requestedSymbols - result.availableSymbols);
    result.succeeded = result.schemaVersion == 1;
    if (!result.succeeded)
        result.error = QStringLiteral("NAS catalog schema 版本不受支持");
    return result;
}

} // namespace

struct NasDataNodeClient::DownloadState {
    NasCatalogEntry entry;
    QString localRoot;
    QString localFilePath;
    QString partFilePath;
    std::unique_ptr<QFile> file;
    std::unique_ptr<QCryptographicHash> hash;
    qint64 receivedBytes = 0;
    qint64 resumeOffset = 0;
    bool writeFailed = false;
};

NasDataNodeClient::NasDataNodeClient(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<NasCatalogResult>();
    qRegisterMetaType<NasDownloadResult>();
}

bool NasDataNodeClient::validateEndpoint(const QUrl &endpoint, QString *error)
{
    if (error) error->clear();
    if (!endpoint.isValid() || endpoint.host().isEmpty()) {
        if (error) *error = QStringLiteral("NAS 数据节点地址无效");
        return false;
    }
    if (!endpoint.userInfo().isEmpty()) {
        if (error) *error = QStringLiteral("NAS 地址不能包含用户名或密码");
        return false;
    }
    if (endpoint.scheme() == QStringLiteral("https")) return true;
    const bool loopback = endpoint.scheme() == QStringLiteral("http")
        && (endpoint.host() == QStringLiteral("127.0.0.1")
            || endpoint.host() == QStringLiteral("localhost")
            || endpoint.host() == QStringLiteral("::1"));
    const bool explicitlyAllowed = endpoint.scheme() == QStringLiteral("http")
        && qEnvironmentVariableIntValue("PPTRADER_ALLOW_INSECURE_NAS") == 1;
    if (!loopback && !explicitlyAllowed) {
        if (error) *error = QStringLiteral(
            "局域网 NAS 默认必须使用 HTTPS；临时 HTTP 调试需显式启用不安全模式");
        return false;
    }
    return true;
}

bool NasDataNodeClient::setEndpoint(const QUrl &endpoint, QString *error)
{
    if (!validateEndpoint(endpoint, error)) return false;
    m_endpoint = endpoint;
    QString path = m_endpoint.path();
    if (!path.endsWith('/')) path.append('/');
    m_endpoint.setPath(path);
    m_endpoint.setQuery(QString{});
    m_endpoint.setFragment(QString{});
    return true;
}

QUrl NasDataNodeClient::endpoint() const { return m_endpoint; }
void NasDataNodeClient::setBearerToken(const QByteArray &token) { m_bearerToken = token; }

QNetworkRequest NasDataNodeClient::requestFor(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("Accept", "application/json");
    if (!m_bearerToken.isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_bearerToken);
    return request;
}

QUrl NasDataNodeClient::resolve(const QString &path) const
{
    QString relative = path;
    while (relative.startsWith('/')) relative.remove(0, 1);
    return m_endpoint.resolved(QUrl(relative));
}

bool NasDataNodeClient::requestHealth(quint64 requestId)
{
    if (requestId == 0 || m_endpoint.isEmpty() || m_replies.contains(requestId)) return false;
    QNetworkReply *reply = m_network.get(requestFor(resolve(QStringLiteral("v1/health"))));
    m_replies.insert(requestId, reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, requestId, reply]() { finishHealth(requestId, reply); });
    return true;
}

bool NasDataNodeClient::requestCatalog(quint64 requestId, const QString &symbol,
                                       const QDate &startDate, const QDate &endDate)
{
    if (requestId == 0 || m_endpoint.isEmpty() || m_replies.contains(requestId)
        || symbol.size() != 6 || !startDate.isValid() || !endDate.isValid()
        || startDate > endDate || startDate.daysTo(endDate) > 366) return false;
    QUrl url = resolve(QStringLiteral("v1/catalog"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("symbol"), symbol);
    query.addQueryItem(QStringLiteral("start"), startDate.toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("end"), endDate.toString(Qt::ISODate));
    url.setQuery(query);
    QNetworkReply *reply = m_network.get(requestFor(url));
    m_replies.insert(requestId, reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, requestId, reply]() { finishCatalog(requestId, reply, 1, 2 * 1024 * 1024); });
    return true;
}

bool NasDataNodeClient::requestManifest(quint64 requestId,
                                        const QStringList &symbols,
                                        const QDate &startDate,
                                        const QDate &endDate)
{
    if (requestId == 0 || m_endpoint.isEmpty() || m_replies.contains(requestId)
        || symbols.isEmpty() || symbols.size() > 6000 || !startDate.isValid()
        || !endDate.isValid() || startDate > endDate
        || startDate.daysTo(endDate) > 30) return false;
    QSet<QString> unique;
    QJsonArray symbolArray;
    for (const QString &symbol : symbols) {
        if (symbol.size() != 6 || unique.contains(symbol)) return false;
        for (const QChar character : symbol) {
            if (!character.isDigit()) return false;
        }
        unique.insert(symbol);
        symbolArray.append(symbol);
    }
    QJsonObject body;
    body.insert(QStringLiteral("symbols"), symbolArray);
    body.insert(QStringLiteral("start"), startDate.toString(Qt::ISODate));
    body.insert(QStringLiteral("end"), endDate.toString(Qt::ISODate));
    QNetworkRequest request = requestFor(resolve(QStringLiteral("v1/manifest")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(5 * 60 * 1000);
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_replies.insert(requestId, reply);
    connect(reply, &QNetworkReply::finished, this, [this, requestId, reply, symbols]() {
        finishCatalog(requestId, reply, symbols.size(), 16 * 1024 * 1024);
    });
    return true;
}

bool NasDataNodeClient::downloadTickFile(quint64 requestId,
                                         const NasCatalogEntry &entry,
                                         const QString &localRoot)
{
    if (requestId == 0 || m_endpoint.isEmpty() || m_replies.contains(requestId)
        || !validEntry(entry)) return false;
    const QString dateDirectory = QDir(localRoot).filePath(
        entry.tradingDate.toString(QStringLiteral("yyyyMMdd")));
    if (!QDir().mkpath(dateDirectory)) return false;
    const QString targetPath = QDir(dateDirectory).filePath(entry.symbol + QStringLiteral(".csv"));
    if (QFileInfo(targetPath).size() == entry.size
        && fileDigest(targetPath) == normalizedDigest(entry.sha256)) {
        NasDownloadResult result;
        result.succeeded = true;
        result.fromCache = true;
        result.entry = entry;
        result.localRoot = localRoot;
        result.localFilePath = targetPath;
        result.receivedBytes = entry.size;
        emit downloadReady(requestId, result);
        return true;
    }

    auto state = std::make_shared<DownloadState>();
    state->entry = entry;
    state->localRoot = localRoot;
    state->localFilePath = targetPath;
    state->partFilePath = targetPath + QStringLiteral(".part");
    state->file = std::make_unique<QFile>(state->partFilePath);
    state->hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    QIODevice::OpenMode mode = QIODevice::WriteOnly;
    qint64 partSize = 0;
    if (state->file->exists()) {
        partSize = state->file->size();
        if (partSize > 0 && partSize < entry.size) {
            mode = QIODevice::WriteOnly | QIODevice::Append;
        } else if (partSize >= entry.size) {
            // 残留部分文件超过目标：从头开始。
            if (!QFile::remove(state->partFilePath)) return false;
            partSize = 0;
        }
    }
    if (!state->file->open(mode)) return false;
    if (partSize > 0) {
        // 把已下载的部分计入哈希与字节数，便于续传后整体校验。
        QFile partReader(state->partFilePath);
        if (!partReader.open(QIODevice::ReadOnly)) return false;
        while (!partReader.atEnd()) {
            state->hash->addData(partReader.read(1024 * 1024));
        }
        state->receivedBytes = partSize;
        state->resumeOffset = partSize;
    }
    QNetworkRequest request = requestFor(resolve(entry.downloadPath));
    request.setRawHeader("Accept", "text/csv");
    if (state->resumeOffset > 0) {
        request.setRawHeader("Range", QByteArray("bytes=")
                                 + QByteArray::number(state->resumeOffset)
                                 + QByteArray("-"));
    }
    QNetworkReply *reply = m_network.get(request);
    m_replies.insert(requestId, reply);
    m_downloads.insert(requestId, state);
    connect(reply, &QNetworkReply::readyRead, this, [reply, state]() {
        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) return;
        state->hash->addData(bytes);
        state->receivedBytes += bytes.size();
        if (state->file->write(bytes) != bytes.size()) state->writeFailed = true;
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, requestId](qint64 received, qint64 total) {
                emit downloadProgress(requestId, received, total);
            });
    connect(reply, &QNetworkReply::finished, this, [this, requestId, reply, state]() {
        finishDownload(requestId, reply, state);
    });
    return true;
}

void NasDataNodeClient::cancel(quint64 requestId)
{
    if (QNetworkReply *reply = m_replies.value(requestId)) reply->abort();
}

void NasDataNodeClient::finishHealth(quint64 requestId, QNetworkReply *reply)
{
    m_replies.remove(requestId);
    QString version;
    QString error;
    const QByteArray payload = reply->readAll();
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (reply->error() == QNetworkReply::NoError && document.isObject())
        version = document.object().value(QStringLiteral("version")).toString();
    else error = reply->errorString();
    emit healthReady(requestId, error.isEmpty() && !version.isEmpty(), version, error);
    reply->deleteLater();
}

void NasDataNodeClient::finishCatalog(quint64 requestId, QNetworkReply *reply,
                                      int requestedSymbols, int maximumPayloadBytes)
{
    m_replies.remove(requestId);
    NasCatalogResult result;
    const QByteArray payload = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        result.error = reply->errorString();
    } else {
        result = parseCatalogPayload(payload, requestedSymbols, maximumPayloadBytes);
    }
    emit catalogReady(requestId, result);
    reply->deleteLater();
}

void NasDataNodeClient::finishDownload(
    quint64 requestId, QNetworkReply *reply,
    const std::shared_ptr<DownloadState> &state)
{
    const QByteArray trailing = reply->readAll();
    if (!trailing.isEmpty()) {
        state->hash->addData(trailing);
        state->receivedBytes += trailing.size();
        if (state->file->write(trailing) != trailing.size()) state->writeFailed = true;
    }
    m_replies.remove(requestId);
    m_downloads.remove(requestId);
    NasDownloadResult result;
    result.entry = state->entry;
    result.localRoot = state->localRoot;
    result.localFilePath = state->localFilePath;
    result.receivedBytes = state->receivedBytes;
    const int statusCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        result.error = reply->errorString();
    } else if (statusCode == 200 && state->resumeOffset > 0) {
        // 服务器忽略了 Range：从头下载，清理部分文件。
        state->file->close();
        if (!QFile::remove(state->partFilePath)) {
            result.error = QStringLiteral("清理 NAS 续传缓存失败");
        } else {
            result.error = QStringLiteral("NAS 服务器未支持续传，请重试");
        }
    } else if (state->writeFailed) {
        result.error = QStringLiteral("写入 NAS 下载缓存失败");
    } else if (state->receivedBytes != state->entry.size) {
        result.error = QStringLiteral("NAS 下载字节数与 catalog 不一致（可续传）");
    } else if (QString::fromLatin1(state->hash->result().toHex())
               != normalizedDigest(state->entry.sha256)) {
        result.error = QStringLiteral("NAS 下载 SHA-256 校验失败");
    } else {
        state->file->close();
        if (!QFile::rename(state->partFilePath, state->localFilePath)) {
            result.error = QStringLiteral("提交 NAS 下载缓存失败");
        } else {
            result.succeeded = true;
        }
    }
    if (!result.succeeded) {
        state->file->close();
        const bool retainForResume =
            reply->error() != QNetworkReply::NoError
            || result.error.startsWith(
                QStringLiteral("NAS 下载字节数与 catalog 不一致"));
        if (!retainForResume) {
            QFile::remove(state->partFilePath);
        }
    }
    emit downloadReady(requestId, result);
    reply->deleteLater();
}

} // namespace pp
