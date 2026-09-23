#pragma once

#include "domain/AShareIndustryImportTypes.h"

#include <QObject>
#include <QThread>

#include <functional>

namespace pp {

class AShareIndustryCsvImporter final {
public:
    using CancellationCheck = std::function<bool()>;

    static AShareIndustryImportResult parse(
        const AShareIndustryImportRequest &request,
        const CancellationCheck &isCancelled = {});
};

class AShareIndustryImportService final : public QObject {
    Q_OBJECT

public:
    explicit AShareIndustryImportService(QObject *parent = nullptr);
    ~AShareIndustryImportService() override;

    void setDatabasePath(const QString &databasePath);
    quint64 start(const AShareIndustryImportRequest &request);
    quint64 preview(const AShareIndustryImportRequest &request);
    bool running() const;
    void cancel();

signals:
    void finished(quint64 requestId,
                  const pp::AShareIndustryImportResult &result);

private:
    QString m_databasePath;
    quint64 m_nextRequestId = 0;
    QThread *m_thread = nullptr;
};

} // namespace pp
