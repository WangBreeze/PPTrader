#pragma once

#include "services/MarketDataProvider.h"
#include "services/MarketSnapshotAssembler.h"
#include "services/MootdxCompactSnapshotParser.h"

#include <QHash>
#include <QProcess>

namespace pp {

class MootdxMarketDataProvider final : public MarketDataProvider {
    Q_OBJECT

public:
    explicit MootdxMarketDataProvider(const QString &serverOverride = {},
                                      QObject *parent = nullptr);
    ~MootdxMarketDataProvider() override;

    static void warmUpPython();

    void start(const QVector<Instrument> &instruments) override;
    void stop() override;

private slots:
    void onReadyRead();
    void onReadyReadStderr();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void drainStandardOutput(bool finalDrain);
    void parseLine(const QByteArray &line);
    Tick jsonToTick(const QJsonObject &obj) const;
    void completeActiveSnapshot(const QDateTime &completedAt = {});
    void publishState(ProviderHealthState health, const QString &message,
                      const QString &error = {}, int receivedCount = 0,
                      const QString &batchId = {}, const QDateTime &updatedAt = {});

    QProcess *m_process = nullptr;
    QHash<QString, Instrument> m_instrumentBySymbol;
    QHash<int, Instrument> m_instrumentByCode;
    MootdxCompactSnapshotChunk m_compactChunkBuffer;
    MarketSnapshotAssembler m_snapshotAssembler {Market::AShare};
    QString m_symbolsFilePath;
    QString m_activeBatchId;
    QString m_activeBatchError;
    QByteArray m_stdoutBuffer;
    QString m_lastProcessDiagnostic;
    QString m_lastProcessStderr;
    int m_expectedCount = 0;
    int m_lastReceivedCount = 0;
    QString m_lastBatchId;
    QDateTime m_lastUpdatedAt;
    bool m_stopping = false;
    QString m_serverOverride;
};

} // namespace pp
