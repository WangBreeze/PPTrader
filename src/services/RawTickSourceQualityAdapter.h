#pragma once

#include "domain/RawTickSourceQualityTypes.h"

namespace pp {

// 对用户明确指定的单个源文件做只读流式检查；不写源文件、SQLite 或网络。
class RawTickSourceQualityAdapter final {
public:
    static RawTickSourceQualityReport inspect(
        const RawTickSourceQualityRequest &request);
    static RawTickSourceQualityManifest inspectBatch(
        const QVector<RawTickSourceQualityRequest> &requests);
};

} // namespace pp
