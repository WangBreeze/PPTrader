#pragma once

#include "domain/RawTradeTypes.h"

namespace pp {

class SqliteLocalDataStore;

// 真实逐笔 Provider/NAS 数据节点的统一增量入口。调用方必须提供稳定 source
// 和 sourceLine；该服务不接受由轮询快照推断出的伪逐笔数据。
class RawTradeIncrementalIngestService final {
public:
    explicit RawTradeIncrementalIngestService(SqliteLocalDataStore *store);

    RawTradeIncrementalAppendResult ingest(const QVector<RawTradeTick> &ticks) const;

private:
    SqliteLocalDataStore *m_store = nullptr;
};

} // namespace pp
