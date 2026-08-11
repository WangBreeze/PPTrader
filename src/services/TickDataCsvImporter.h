#pragma once

#include "domain/TickDataImportTypes.h"

#include <QObject>
#include <QThread>

#include <functional>

namespace pp {

class TickDataCsvImporter final {
public:
    using CancellationCheck = std::function<bool()>;
    using RawTradeSink = std::function<bool(const QVector<RawTradeTick> &)>;

    static TickDataImportResult parse(const TickDataImportRequest &request,
                                      const CancellationCheck &isCancelled = {},
                                      const RawTradeSink &rawTradeSink = {});
    static QString expectedFilePath(const TickDataImportRequest &request);
};

class TickDataCsvImportService final : public QObject {
    Q_OBJECT

public:
    explicit TickDataCsvImportService(QObject *parent = nullptr);
    ~TickDataCsvImportService() override;

    quint64 start(const TickDataImportRequest &request);
    void setDatabasePath(const QString &databasePath);
    bool running() const;
    void cancel();

signals:
    void finished(quint64 requestId, const pp::TickDataImportResult &result);

private:
    quint64 m_nextRequestId = 0;
    QThread *m_thread = nullptr;
    QString m_databasePath;
};

} // namespace pp
