#include "app/BatchBacktestEvidence.h"

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

namespace {

class ResultModel final : public QAbstractListModel {
public:
    explicit ResultModel(QVector<QVariantMap> rows, QObject *parent = nullptr)
        : QAbstractListModel(parent), m_rows(std::move(rows))
    {
        const QList<QByteArray> names{
            "symbol", "succeeded", "error", "dataVersion", "datasetVersion",
            "datasetRawTickCount", "finalEquity", "totalReturnPercent",
            "maxDrawdownPercent", "totalTrades", "elapsedMilliseconds",
            "requestId", "archivedResultId"
        };
        for (int i = 0; i < names.size(); ++i)
            m_roles.insert(Qt::UserRole + 1 + i, names.at(i));
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
            return {};
        return m_rows.at(index.row()).value(
            QString::fromLatin1(m_roles.value(role)));
    }

    QHash<int, QByteArray> roleNames() const override { return m_roles; }

private:
    QVector<QVariantMap> m_rows;
    QHash<int, QByteArray> m_roles;
};

QVariantMap row(const QString &symbol, qint64 ticks, double finalEquity,
                int elapsed, quint64 requestId)
{
    return {{QStringLiteral("symbol"), symbol},
            {QStringLiteral("succeeded"), true},
            {QStringLiteral("error"), QString{}},
            {QStringLiteral("dataVersion"), QStringLiteral("data-%1").arg(symbol)},
            {QStringLiteral("datasetVersion"), QStringLiteral("set-%1").arg(symbol)},
            {QStringLiteral("datasetRawTickCount"), ticks},
            {QStringLiteral("finalEquity"), finalEquity},
            {QStringLiteral("totalReturnPercent"), 1.25},
            {QStringLiteral("maxDrawdownPercent"), 0.4},
            {QStringLiteral("totalTrades"), 3},
            {QStringLiteral("elapsedMilliseconds"), elapsed},
            {QStringLiteral("requestId"), QVariant::fromValue(requestId)},
            {QStringLiteral("archivedResultId"), QStringLiteral("archive-%1").arg(requestId)}};
}

} // namespace

class BatchBacktestEvidenceTest final : public QObject {
    Q_OBJECT

private slots:
    void digestIgnoresOrderingAndRuntimeMetadata()
    {
        ResultModel first({row(QStringLiteral("000002"), 20, 101.0, 10, 1),
                           row(QStringLiteral("000001"), 10, 100.0, 20, 1)});
        ResultModel second({row(QStringLiteral("000001"), 10, 100.0, 999, 88),
                            row(QStringLiteral("000002"), 20, 101.0, 888, 88)});
        QCOMPARE(pp::batchBacktestEvidence::canonicalDigest(&first),
                 pp::batchBacktestEvidence::canonicalDigest(&second));
        QCOMPARE(pp::batchBacktestEvidence::totalRawTickCount(&first), 30);
        const QJsonArray rows = pp::batchBacktestEvidence::canonicalRows(&first);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toObject().value(QStringLiteral("symbol")).toString(),
                 QStringLiteral("000001"));
    }

    void digestChangesWhenResultIdentityChanges()
    {
        ResultModel baseline({row(QStringLiteral("000001"), 10, 100.0, 10, 1)});
        ResultModel changed({row(QStringLiteral("000001"), 11, 100.0, 10, 1)});
        QVERIFY(pp::batchBacktestEvidence::canonicalDigest(&baseline)
                != pp::batchBacktestEvidence::canonicalDigest(&changed));
    }

    void digestChangesWhenResultSummaryChanges()
    {
        ResultModel baseline({row(QStringLiteral("000001"), 10, 100.0, 10, 1)});
        ResultModel changed({row(QStringLiteral("000001"), 10, 101.0, 10, 1)});
        QVERIFY(pp::batchBacktestEvidence::canonicalDigest(&baseline)
                != pp::batchBacktestEvidence::canonicalDigest(&changed));
    }

    void nullModelIsStable()
    {
        QCOMPARE(pp::batchBacktestEvidence::canonicalRows(nullptr).size(), 0);
        QCOMPARE(pp::batchBacktestEvidence::totalRawTickCount(nullptr), 0);
        QVERIFY(pp::batchBacktestEvidence::canonicalDigest(nullptr)
                    .startsWith(QStringLiteral("sha256:")));
    }
};

QTEST_MAIN(BatchBacktestEvidenceTest)
#include "tst_batch_backtest_evidence.moc"
