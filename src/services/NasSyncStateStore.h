#pragma once

#include "domain/NasDataNodeTypes.h"

#include <QStringList>

namespace pp {

struct NasPersistedSyncState {
    QStringList symbols;
    QVector<NasSyncPlanItem> items;
};

class NasSyncStateStore final {
public:
    void setFilePath(const QString &filePath);
    QString filePath() const;
    bool save(const NasPersistedSyncState &state, QString *error = nullptr) const;
    bool load(NasPersistedSyncState *state, QString *error = nullptr) const;
    bool clear(QString *error = nullptr) const;

private:
    QString m_filePath;
};

} // namespace pp
