#pragma once

#include "domain/AShareInstrumentDayImportTypes.h"

#include <QObject>
#include <QThread>

#include <functional>

namespace pp {

class AShareInstrumentDayCsvImporter final {
public:
    using CancellationCheck = std::function<bool()>;

    static AShareInstrumentDayImportResult parse(
        const AShareInstrumentDayImportRequest &request,
        const CancellationCheck &isCancelled = {});
};

class AShareInstrumentDayImportService final : public QObject {
    Q_OBJECT

public:
    explicit AShareInstrumentDayImportService(QObject *parent = nullptr);
    ~AShareInstrumentDayImportService() override;

    void setDatabasePath(const QString &databasePath);
    quint64 start(const AShareInstrumentDayImportRequest &request);
    quint64 preview(const AShareInstrumentDayImportRequest &request);
    bool running() const;
    void cancel();

signals:
    void finished(quint64 requestId,
                  const pp::AShareInstrumentDayImportResult &result);

private:
    QString m_databasePath;
    quint64 m_nextRequestId = 0;
    QThread *m_thread = nullptr;
};

} // namespace pp
