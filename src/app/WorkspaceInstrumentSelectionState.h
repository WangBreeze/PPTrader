#pragma once

#include "domain/LocalHistoryTypes.h"

#include <QString>
#include <QDateTime>
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

    // NAS 数据选择器与回测任务页共享的样本窗草稿/已应用范围。
    bool setBacktestSampleDateRange(const QString &from, const QString &to,
                                    QString *error = nullptr);
    void setBacktestSampleWindow(const QDateTime &from, const QDateTime &to);
    void clearBacktestSampleWindow();
    const QString &backtestSampleFromDate() const;
    const QString &backtestSampleToDate() const;
    const QDateTime &backtestSampleFrom() const;
    const QDateTime &backtestSampleTo() const;

private:
    Instrument m_researchInstrument;
    Instrument m_liveInstrument;
    Instrument m_backtestInstrument;
    QVector<Instrument> m_liveUniverse;
    QVector<LocalHistoryDataset> m_localHistoryCatalog;
    int m_selectedBacktestPeriodSeconds = 0;
    QString m_backtestSampleFromDate;
    QString m_backtestSampleToDate;
    QDateTime m_backtestSampleFrom;
    QDateTime m_backtestSampleTo;
};

} // namespace pp
