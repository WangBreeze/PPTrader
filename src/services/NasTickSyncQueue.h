#pragma once

#include "domain/NasDataNodeTypes.h"

#include <QSet>
#include <QStringList>

namespace pp {

class NasTickSyncQueue final {
public:
    bool prepare(const QVector<NasCatalogEntry> &entries, const QString &symbol);
    bool prepareManifest(const QVector<NasCatalogEntry> &entries,
                         const QStringList &symbols);
    bool restore(const QVector<NasSyncPlanItem> &items);
    bool start();
    bool resume();
    int takeNextReady();
    QVector<int> takeReadyBatch(int maximum);
    bool markImporting(int index, qint64 transferElapsedMilliseconds = 0,
                       bool fromCache = false);
    bool markSucceeded(int index, qint64 rawTicksWritten, int candleBarsWritten,
                       qint64 parseElapsedMilliseconds = 0,
                       qint64 rawDatabaseElapsedMilliseconds = 0,
                       qint64 candleDatabaseElapsedMilliseconds = 0,
                       qint64 importElapsedMilliseconds = 0);
    bool markFailed(int index, const QString &error);
    void cancelActiveAndPending();

    const QVector<NasSyncPlanItem> &items() const;
    NasSyncSummary summary() const;
    bool running() const;
    bool canResume() const;
    int activeIndex() const;
    int activeCount() const;
    int downloadingCount() const;

private:
    bool validIndexAndStatus(int index, NasSyncItemStatus status) const;

    QVector<NasSyncPlanItem> m_items;
    bool m_running = false;
    QVector<int> m_activeIndices;
};

} // namespace pp
