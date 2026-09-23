#pragma once

#include "domain/AuthorizedFactorImportTypes.h"

#include <QObject>
#include <QThread>

#include <functional>

namespace pp {

class AuthorizedFactorCsvImporter final {
public:
    using CancellationCheck = std::function<bool()>;

    static AuthorizedFactorImportResult parse(
        const AuthorizedFactorImportRequest &request,
        const CancellationCheck &isCancelled = {});
};

class AuthorizedFactorImportService final : public QObject {
    Q_OBJECT

public:
    explicit AuthorizedFactorImportService(QObject *parent = nullptr);
    ~AuthorizedFactorImportService() override;

    void setDatabasePath(const QString &databasePath);
    quint64 start(const AuthorizedFactorImportRequest &request);
    quint64 preview(const AuthorizedFactorImportRequest &request);
    bool running() const;
    void cancel();

signals:
    void finished(quint64 requestId,
                  const pp::AuthorizedFactorImportResult &result);

private:
    QString m_databasePath;
    quint64 m_nextRequestId = 0;
    QThread *m_thread = nullptr;
};

} // namespace pp
