#pragma once

#include "domain/PortfolioTypes.h"

#include <QString>

namespace pp {

class FactorResearchReportBuilder final {
public:
    static QString buildMarkdown(const PortfolioArchiveRecord &record,
                                 QString *error = nullptr);
    static bool writeMarkdownAtomically(const PortfolioArchiveRecord &record,
                                        const QString &filePath,
                                        QString *error = nullptr);
};

} // namespace pp
