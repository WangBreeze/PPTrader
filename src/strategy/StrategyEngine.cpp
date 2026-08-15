#include "strategy/StrategyEngine.h"

#include <QPointer>
#include <QRunnable>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <utility>

namespace pp {

namespace {

template <typename Fn>
void runOnPool(QThreadPool &pool, Fn &&fn)
{
    auto *task = QRunnable::create(std::forward<Fn>(fn));
    task->setAutoDelete(true);
    pool.start(task);
}

} // 匿名命名空间

StrategyEngine::StrategyEngine(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
}

int StrategyEngine::maxThreadCount() const
{
    return m_pool.maxThreadCount();
}

void StrategyEngine::setMaxThreadCount(int count)
{
    const int normalized = std::max(1, count);
    if (m_pool.maxThreadCount() == normalized) {
        return;
    }

    m_pool.setMaxThreadCount(normalized);
    emit maxThreadCountChanged();
}

int StrategyEngine::strategyCount() const
{
    return m_strategies.size();
}

Strategy *StrategyEngine::strategyById(const QString &id) const
{
    for (Strategy *strategy : m_strategies) {
        if (strategy->id() == id) {
            return strategy;
        }
    }

    return nullptr;
}

bool StrategyEngine::isStrategyEnabled(const QString &id) const
{
    return m_enabledById.value(id, false);
}

void StrategyEngine::addStrategy(Strategy *strategy)
{
    if (!strategy || m_strategies.contains(strategy)) {
        return;
    }

    strategy->setParent(this);
    const QString strategyId = strategy->id();
    connect(strategy, &Strategy::signalGenerated, this,
            [this, strategyId](const TradeSignal &signal) {
                emit signalGenerated(signal);
                emit strategySignalGenerated(strategyId, signal);
            });
    m_strategies.push_back(strategy);
    const QStringList subscribedSymbols = strategy->subscribedSymbols();
    if (subscribedSymbols.isEmpty()) {
        m_broadcastStrategies.append(strategy);
    } else {
        QSet<QString> uniqueSymbols;
        for (const QString &symbol : subscribedSymbols) {
            if (!symbol.isEmpty() && !uniqueSymbols.contains(symbol)) {
                m_strategiesBySymbol[symbol].append(strategy);
                uniqueSymbols.insert(symbol);
            }
        }
    }
    m_enabledById.insert(strategy->id(), true);
    emit strategyCountChanged();
}

void StrategyEngine::clearStrategies()
{
    if (m_strategies.isEmpty()) {
        return;
    }

    for (Strategy *strategy : std::as_const(m_strategies)) {
        strategy->deleteLater();
    }
    m_strategies.clear();
    m_broadcastStrategies.clear();
    m_strategiesBySymbol.clear();
    m_enabledById.clear();
    emit strategyCountChanged();
}

bool StrategyEngine::removeStrategy(const QString &id)
{
    for (int index = 0; index < m_strategies.size(); ++index) {
        Strategy *strategy = m_strategies.at(index);
        if (strategy->id() != id) continue;
        m_broadcastStrategies.removeAll(strategy);
        for (auto iterator = m_strategiesBySymbol.begin();
             iterator != m_strategiesBySymbol.end(); ++iterator) {
            iterator.value().removeAll(strategy);
        }
        m_enabledById.remove(id);
        m_strategies.removeAt(index);
        strategy->deleteLater();
        emit strategyCountChanged();
        return true;
    }
    return false;
}

void StrategyEngine::setStrategyEnabled(const QString &id, bool enabled)
{
    if (!m_enabledById.contains(id) || m_enabledById.value(id) == enabled) {
        return;
    }

    m_enabledById.insert(id, enabled);
    emit strategyCountChanged();
}

void StrategyEngine::dispatchTick(const Tick &tick)
{
    QVector<Strategy *> candidates = m_broadcastStrategies;
    const auto symbolStrategies = m_strategiesBySymbol.constFind(tick.instrument.symbol);
    if (symbolStrategies != m_strategiesBySymbol.constEnd()) {
        candidates += *symbolStrategies;
    }
    for (Strategy *strategy : std::as_const(candidates)) {
        if (!isStrategyEnabled(strategy->id())) {
            continue;
        }

        const QPointer<Strategy> guardedStrategy(strategy);
        runOnPool(m_pool, [guardedStrategy, tick]() {
            if (guardedStrategy) {
                guardedStrategy->onTick(tick);
            }
        });
    }
}

void StrategyEngine::dispatchCandle(const Candle &candle)
{
    QVector<Strategy *> candidates = m_broadcastStrategies;
    const auto symbolStrategies = m_strategiesBySymbol.constFind(candle.instrument.symbol);
    if (symbolStrategies != m_strategiesBySymbol.constEnd()) {
        candidates += *symbolStrategies;
    }
    for (Strategy *strategy : std::as_const(candidates)) {
        if (!isStrategyEnabled(strategy->id())) {
            continue;
        }

        const QPointer<Strategy> guardedStrategy(strategy);
        runOnPool(m_pool, [guardedStrategy, candle]() {
            if (guardedStrategy) {
                guardedStrategy->onCandle(candle);
            }
        });
    }
}

} // 命名空间 pp
