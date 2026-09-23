#pragma once

#include "domain/MarketTypes.h"

#include <QDate>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace pp {

// 单个不可变行业数据集中的一条有效期记录。行业分类体系与来源版本由父数据集固定。
struct AShareIndustryMembership {
    Instrument instrument;
    QString industryCode;
    QString industryName;
    QDate effectiveFrom;
    QDate effectiveTo; // 无效日期表示开放区间。

    bool isValid() const
    {
        return !instrument.symbol.trimmed().isEmpty()
            && instrument.market == Market::AShare
            && !industryCode.trimmed().isEmpty()
            && !industryName.trimmed().isEmpty()
            && effectiveFrom.isValid()
            && (!effectiveTo.isValid() || effectiveTo >= effectiveFrom);
    }

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("symbol"), instrument.symbol},
                {QStringLiteral("name"), instrument.name},
                {QStringLiteral("venue"), instrument.venue},
                {QStringLiteral("industryCode"), industryCode},
                {QStringLiteral("industryName"), industryName},
                {QStringLiteral("effectiveFrom"), effectiveFrom},
                {QStringLiteral("effectiveTo"), effectiveTo}};
    }
};

// 一次完整导入形成一个不可变数据集；同一来源版本的不同内容也不会覆盖旧数据集。
struct AShareIndustryDataset {
    QString datasetId;
    QString classification;
    QString source;
    QString sourceVersion;
    QString contentSha256;
    QDateTime importedAt;
    QString fileName;
    qint64 linesRead = 0;
    qint64 recordCount = 0;
    QString revisionReason;
    QString previousDatasetId;
    QVector<AShareIndustryMembership> memberships;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("datasetId"), datasetId},
                {QStringLiteral("classification"), classification},
                {QStringLiteral("source"), source},
                {QStringLiteral("sourceVersion"), sourceVersion},
                {QStringLiteral("contentSha256"), contentSha256},
                {QStringLiteral("importedAt"), importedAt},
                {QStringLiteral("fileName"), fileName},
                {QStringLiteral("linesRead"), linesRead},
                {QStringLiteral("revisionReason"), revisionReason},
                {QStringLiteral("previousDatasetId"), previousDatasetId},
                {QStringLiteral("recordCount"),
                 recordCount > 0 ? recordCount : memberships.size()}};
    }
};

struct AShareIndustryDatasetAnalysis {
    bool valid = false;
    bool reused = false;
    bool requiresRevisionReason = false;
    QString existingDatasetId;
    QString previousDatasetId;
    QString error;
};

// 回测只消费固定数据集在固定日期、固定标的集合上的解析快照。
struct AShareIndustrySnapshot {
    QString datasetId;
    QString classification;
    QString source;
    QString sourceVersion;
    QString datasetContentSha256;
    QString snapshotVersion;
    QDate asOfDate;
    int expectedSymbols = 0;
    int resolvedSymbols = 0;
    bool coverageComplete = false;
    QVector<AShareIndustryMembership> memberships;

    QString industryCodeFor(const QString &symbol) const
    {
        for (const AShareIndustryMembership &membership : memberships) {
            if (membership.instrument.symbol == symbol)
                return membership.industryCode;
        }
        return {};
    }

    QVariantMap toVariantMap() const
    {
        QVariantList items;
        items.reserve(memberships.size());
        for (const AShareIndustryMembership &membership : memberships)
            items.append(membership.toVariantMap());
        return {{QStringLiteral("datasetId"), datasetId},
                {QStringLiteral("classification"), classification},
                {QStringLiteral("source"), source},
                {QStringLiteral("sourceVersion"), sourceVersion},
                {QStringLiteral("datasetContentSha256"), datasetContentSha256},
                {QStringLiteral("snapshotVersion"), snapshotVersion},
                {QStringLiteral("asOfDate"), asOfDate},
                {QStringLiteral("expectedSymbols"), expectedSymbols},
                {QStringLiteral("resolvedSymbols"), resolvedSymbols},
                {QStringLiteral("coverageComplete"), coverageComplete},
                {QStringLiteral("memberships"), items}};
    }
};

} // namespace pp
