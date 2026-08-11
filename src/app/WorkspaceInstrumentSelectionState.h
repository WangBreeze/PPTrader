#pragma once

#include "domain/LocalHistoryTypes.h"

#include <QString>
#include <QVector>

namespace pp {

class WorkspaceInstrumentSelectionState final {
public:
    void setResearchInstrument(const Instrument &instrument);
    void setLiveUniverse(const QVector<Instrument> &instruments);
    void setLocalHistoryCatalog(const QVector<LocalHistoryDataset> &catalog);

    bool selectLiveInstrument(const QString &symbol, QString *error = nullptr);
    bool selectBacktestInstrument(const QString &symbol, int periodSeconds,
                                  int minimumBars, QString *error = nullptr);

    const Instrument &researchInstrument() const;
    const Instrument &liveInstrument() const;
    const Instrument &backtestInstrument() const;
    const LocalHistoryDataset *selectedBacktestDataset() const;
    const QVector<LocalHistoryDataset> &localHistoryCatalog() const;

private:
    Instrument m_researchInstrument;
    Instrument m_liveInstrument;
    Instrument m_backtestInstrument;
    QVector<Instrument> m_liveUniverse;
    QVector<LocalHistoryDataset> m_localHistoryCatalog;
    int m_selectedBacktestPeriodSeconds = 0;
};

} // namespace pp
