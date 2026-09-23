#pragma once

#include "domain/PortfolioTypes.h"

#include <QObject>
#include <QThread>

namespace pp {

class AsyncFactorResearchReportService final : public QObject {
    Q_OBJECT

public:
    explicit AsyncFactorResearchReportService(QObject *parent = nullptr);
    ~AsyncFactorResearchReportService() override;

    bool requestExport(quint64 requestId,
                       const PortfolioArchiveRecord &record,
                       const QString &filePath);

signals:
    void exportFinished(quint64 requestId, const QVariantMap &result);

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
};

} // namespace pp
