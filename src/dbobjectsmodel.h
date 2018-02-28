#ifndef DBOBJECTSMODEL_H
#define DBOBJECTSMODEL_H

#include <functional>
#include <QAbstractItemModel>
#include <memory>

class DbObject;
class DbConnection;
class DataTable;

template<class Fn>
class ScopeGuard
{
    Fn _exitHandler;
public:
    ScopeGuard(Fn exitHandler): _exitHandler(exitHandler) {}
    ~ScopeGuard() { _exitHandler(); }
};

class DbObjectsModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit DbObjectsModel(QObject *parent = 0);
    ~DbObjectsModel();

    virtual QModelIndex parent(const QModelIndex &index) const;
    virtual int rowCount(const QModelIndex &parent = QModelIndex()) const;
    virtual int columnCount(const QModelIndex & = QModelIndex()) const { return 1; }
    virtual QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const;
    virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
    virtual bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole);
    virtual Qt::ItemFlags flags(const QModelIndex &index) const;
    virtual bool insertRows(int pos, int count, const QModelIndex &parent = QModelIndex());
    virtual bool removeRows(int pos, int count, const QModelIndex &parent = QModelIndex());
    //QVariant dataAtCurrent(const QAbstractItemView *view, QString column_name) const;

    virtual bool hasChildren(const QModelIndex &parent = QModelIndex()) const;
    virtual bool canFetchMore(const QModelIndex & parent) const;
    virtual void fetchMore(const QModelIndex & parent);

    bool fillChildren(const QModelIndex &parent = QModelIndex());

    std::shared_ptr<DbConnection> dbConnection(const QModelIndex &index);
    QVariant parentNodeProperty(const QModelIndex &index, QString type);
    bool addServer(QString name, QString connectionString);
    bool removeConnection(QModelIndex &index);
    bool alterConnection(QModelIndex &index, QString name, QString connectionString);

private:
    DbObject *_rootItem;
    QModelIndex _curIndex;
    DataTable* nodeChildren(const QModelIndex &obj);

signals:
    void error(QString err);
    void message(QString msg);

public slots:
    QVariant parentNodeProperty(QString type);
    void saveConnectionSettings();
};

#endif // DBOBJECTSMODEL_H
