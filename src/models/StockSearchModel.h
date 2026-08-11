#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QStringList>

namespace pp {

struct StockEntry {
    QString code;
    QString name;
    QString market;
};

class StockSearchModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultCountChanged)

public:
    enum Role {
        CodeRole = Qt::UserRole + 1,
        NameRole,
        MarketRole,
    };

    explicit StockSearchModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int resultCount() const;

    void loadFromJson(const QString &path);
    void setFilterText(const QString &text);

    Q_INVOKABLE QVariantMap get(int index) const;
    Q_INVOKABLE QString codeAt(int index) const;

signals:
    void resultCountChanged();

private:
    QVector<StockEntry> m_all;
    QVector<StockEntry> m_filtered;
};

} // namespace pp
