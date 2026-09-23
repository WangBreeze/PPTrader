#include "services/AsyncFactorResearchReportService.h"

#include "services/FactorResearchReportBuilder.h"

#include <QMetaObject>
#include <QPointer>

namespace pp {

AsyncFactorResearchReportService::AsyncFactorResearchReportService(QObject *parent)
    : QObject(parent), m_worker(new QObject)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished,
            m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderFactorResearchReport"));
    m_thread.start();
}

AsyncFactorResearchReportService::~AsyncFactorResearchReportService()
{
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

bool AsyncFactorResearchReportService::requestExport(
    quint64 requestId, const PortfolioArchiveRecord &record,
    const QString &filePath)
{
    if (!m_thread.isRunning() || !m_worker || requestId == 0
        || filePath.trimmed().isEmpty()) {
        return false;
    }
    QPointer<AsyncFactorResearchReportService> guard(this);
    return QMetaObject::invokeMethod(
        m_worker,
        [guard, requestId, record, filePath] {
            QString error;
            const bool success =
                FactorResearchReportBuilder::writeMarkdownAtomically(
                    record, filePath, &error);
            const QVariantMap result{
                {QStringLiteral("success"), success},
                {QStringLiteral("resultId"), record.resultId},
                {QStringLiteral("filePath"), filePath},
                {QStringLiteral("error"), error}};
            if (!guard) return;
            QMetaObject::invokeMethod(
                guard,
                [guard, requestId, result] {
                    if (guard) emit guard->exportFinished(requestId, result);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

} // namespace pp
