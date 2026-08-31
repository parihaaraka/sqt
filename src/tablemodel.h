#ifndef TABLEMODEL_H
#define TABLEMODEL_H

#include <QAbstractItemModel>

class DataTable;
class TableModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit TableModel(QObject *parent = nullptr);
    virtual ~TableModel() override;

    virtual QModelIndex parent(const QModelIndex &) const override;
    virtual int rowCount(const QModelIndex & = QModelIndex()) const override;
    virtual int columnCount(const QModelIndex & = QModelIndex()) const override;
    virtual QModelIndex index(int row, int column, const QModelIndex & = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    virtual Qt::ItemFlags flags(const QModelIndex &index) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void take(DataTable *srcTable);
    void clear();
    DataTable* table() const { return _table; }
    /// The dbms type name of a column ("jsonb", "numeric(10,2)"), as the
    /// backend filled it in - what tells json and arbitrary-precision columns
    /// apart when a row is rendered as json (Ctrl+J, see rowjson.h).
    ///
    /// Empty until the types have been clarified: for postgres that happens
    /// once the run has finished (QueryWidget does it on queryFinished), so a
    /// grid still being filled has none.
    QString columnTypeName(int column) const;

private:
    DataTable *_table;
    
};

#endif // TABLEMODEL_H
