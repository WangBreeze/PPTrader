#pragma once

#include "services/HistoricalDataProvider.h"

#include <QObject>
#include <QProcess>
#include <QVector>

namespace pp {

enum class KlineProviderBackend {
    Mootdx,
    BaoStock,
    Tushare
};

class KlineDataFetcher final : public HistoricalDataProvider {
    Q_OBJECT

public:
    explicit KlineDataFetcher(
        KlineProviderBackend backend = KlineProviderBackend::Mootdx,
        QObject *parent = nullptr);
    ~KlineDataFetcher() override;

    HistoricalProviderCapability capability() const override;
    void fetch(quint64 requestId, const Instrument &instrument,
               const QString &freq, int count) override;
    void fetchAdjusted(quint64 requestId, const Instrument &instrument,
                       const QString &freq, int count,
                       const QString &adjustment) override;

private:
    QString bridgeFileName() const;
    QString providerId() const;

    KlineProviderBackend m_backend = KlineProviderBackend::Mootdx;
    QProcess *m_process = nullptr;
};

} // namespace pp
