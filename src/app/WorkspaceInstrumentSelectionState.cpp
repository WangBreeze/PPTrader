#include "app/WorkspaceInstrumentSelectionState.h"

#include <algorithm>

namespace pp {

void WorkspaceInstrumentSelectionState::setResearchInstrument(const Instrument &instrument)
{
    m_researchInstrument = instrument;
}

void WorkspaceInstrumentSelectionState::setLiveUniverse(
    const QVector<Instrument> &instruments)
{
    m_liveUniverse = instruments;
    if (!m_liveInstrument.symbol.isEmpty()) {
        const auto match = std::find_if(m_liveUniverse.cbegin(), m_liveUniverse.cend(),
                                        [this](const Instrument &instrument) {
            return instrument.symbol == m_liveInstrument.symbol
                && instrument.market == Market::AShare;
        });
        if (match == m_liveUniverse.cend()) m_liveInstrument = {};
        else m_liveInstrument = *match;
    }
}

void WorkspaceInstrumentSelectionState::setLocalHistoryCatalog(
    const QVector<LocalHistoryDataset> &catalog)
{
    m_localHistoryCatalog = catalog;
}

bool WorkspaceInstrumentSelectionState::selectLiveInstrument(
    const QString &symbol, QString *error)
{
    const QString normalized = symbol.trimmed();
    const auto match = std::find_if(m_liveUniverse.cbegin(), m_liveUniverse.cend(),
                                    [&](const Instrument &instrument) {
        return instrument.symbol == normalized && instrument.market == Market::AShare;
    });
    if (match == m_liveUniverse.cend()) {
        if (error) *error = QStringLiteral("实时品种不在已加载的 A 股股票池中：%1").arg(normalized);
        return false;
    }
    m_liveInstrument = *match;
    if (error) error->clear();
    return true;
}

bool WorkspaceInstrumentSelectionState::selectBacktestInstrument(
    const QString &symbol, int periodSeconds, int minimumBars, QString *error)
{
    const QString normalized = symbol.trimmed();
    const int requiredBars = std::max(1, minimumBars);
    const auto match = std::find_if(
        m_localHistoryCatalog.cbegin(), m_localHistoryCatalog.cend(),
        [&](const LocalHistoryDataset &item) {
            return item.instrument.symbol == normalized
                && item.instrument.market == Market::AShare
                && item.periodSeconds == periodSeconds
                && item.isBacktestEligible();
        });
    if (match == m_localHistoryCatalog.cend()) {
        if (error) {
            *error = QStringLiteral(
                "本地没有 %1 的 %2 历史 Tick 派生 K 线；远端/缓存 K 线不能用于回测")
                         .arg(normalized, localHistoryFrequency(periodSeconds));
        }
        return false;
    }
    if (match->candleCount < requiredBars) {
        if (error) {
            *error = QStringLiteral("本地 %1 %2 K 线仅 %3 根，至少需要 %4 根")
                         .arg(normalized, match->frequency())
                         .arg(match->candleCount).arg(requiredBars);
        }
        return false;
    }
    m_backtestInstrument = match->instrument;
    m_selectedBacktestPeriodSeconds = periodSeconds;
    if (error) error->clear();
    return true;
}

const Instrument &WorkspaceInstrumentSelectionState::researchInstrument() const
{
    return m_researchInstrument;
}

const Instrument &WorkspaceInstrumentSelectionState::liveInstrument() const
{
    return m_liveInstrument;
}

const Instrument &WorkspaceInstrumentSelectionState::backtestInstrument() const
{
    return m_backtestInstrument;
}

const LocalHistoryDataset *WorkspaceInstrumentSelectionState::selectedBacktestDataset() const
{
    if (m_backtestInstrument.symbol.isEmpty() || m_selectedBacktestPeriodSeconds <= 0) return nullptr;
    const auto match = std::find_if(
        m_localHistoryCatalog.cbegin(), m_localHistoryCatalog.cend(),
        [this](const LocalHistoryDataset &item) {
            return item.instrument.symbol == m_backtestInstrument.symbol
                && item.periodSeconds == m_selectedBacktestPeriodSeconds
                && item.isBacktestEligible();
        });
    return match == m_localHistoryCatalog.cend() ? nullptr : &(*match);
}

const QVector<LocalHistoryDataset> &WorkspaceInstrumentSelectionState::localHistoryCatalog() const
{
    return m_localHistoryCatalog;
}

} // namespace pp
