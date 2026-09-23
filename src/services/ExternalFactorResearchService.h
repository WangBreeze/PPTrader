#pragma once

#include "domain/ExternalFactorResearchTypes.h"

#include <QObject>
#include <QThread>

namespace pp {

class ExternalFactorResearchRequestGate final {
public:
    void activate(quint64 requestId)
    {
        m_currentRequestId = requestId;
        m_loading = requestId > 0;
    }

    bool finish(quint64 requestId)
    {
        if (!m_loading || requestId != m_currentRequestId) return false;
        m_loading = false;
        m_resolvedRequestId = requestId;
        return true;
    }

    quint64 currentRequestId() const { return m_currentRequestId; }
    quint64 resolvedRequestId() const { return m_resolvedRequestId; }
    bool loading() const { return m_loading; }

private:
    quint64 m_currentRequestId = 0;
    quint64 m_resolvedRequestId = 0;
    bool m_loading = false;
};

class ExternalFactorResearchService final : public QObject {
    Q_OBJECT

public:
    explicit ExternalFactorResearchService(QObject *parent = nullptr);
    ~ExternalFactorResearchService() override;

    void setDatabasePath(const QString &databasePath);
    quint64 preflight(const ExternalFactorResearchRequest &request);
    quint64 start(const ExternalFactorResearchRequest &request);
    bool running() const;
    void cancel();

signals:
    void preflightFinished(
        quint64 requestId,
        const pp::ExternalFactorResearchPreflightResult &result);
    void finished(
        quint64 requestId,
        const pp::ExternalFactorResearchExecutionResult &result);

private:
    QString m_databasePath;
    quint64 m_nextRequestId = 0;
    QThread *m_thread = nullptr;
};

} // namespace pp
