#pragma once

#include "domain/NasDataNodeTypes.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <memory>

class QCryptographicHash;
class QNetworkReply;
class QSaveFile;

namespace pp {

class NasDataNodeClient final : public QObject {
    Q_OBJECT

public:
    explicit NasDataNodeClient(QObject *parent = nullptr);

    static bool validateEndpoint(const QUrl &endpoint, QString *error = nullptr);
    bool setEndpoint(const QUrl &endpoint, QString *error = nullptr);
    QUrl endpoint() const;
    void setBearerToken(const QByteArray &token);

    bool requestHealth(quint64 requestId);
    bool requestCatalog(quint64 requestId, const QString &symbol,
                        const QDate &startDate, const QDate &endDate);
    bool requestManifest(quint64 requestId, const QStringList &symbols,
                         const QDate &startDate, const QDate &endDate);
    bool downloadTickFile(quint64 requestId, const NasCatalogEntry &entry,
                          const QString &localRoot);
    void cancel(quint64 requestId);

signals:
    void healthReady(quint64 requestId, bool succeeded,
                     const QString &version, const QString &error);
    void catalogReady(quint64 requestId, const pp::NasCatalogResult &result);
    void downloadProgress(quint64 requestId, qint64 receivedBytes, qint64 totalBytes);
    void downloadReady(quint64 requestId, const pp::NasDownloadResult &result);

private:
    struct DownloadState;
    QNetworkRequest requestFor(const QUrl &url) const;
    QUrl resolve(const QString &path) const;
    void finishCatalog(quint64 requestId, QNetworkReply *reply,
                       int requestedSymbols, int maximumPayloadBytes);
    void finishHealth(quint64 requestId, QNetworkReply *reply);
    void finishDownload(quint64 requestId, QNetworkReply *reply,
                        const std::shared_ptr<DownloadState> &state);

    QNetworkAccessManager m_network;
    QUrl m_endpoint;
    QByteArray m_bearerToken;
    QHash<quint64, QNetworkReply *> m_replies;
    QHash<quint64, std::shared_ptr<DownloadState>> m_downloads;
};

} // namespace pp
