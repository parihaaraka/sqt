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
    explicit DbObjectsModel(QObject *parent = nullptr);
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
    /// Repaints every node's icon. The names are kept, only the files behind
    /// them are looked up again - so a change of the assets directory applies
    /// without collapsing the tree or reopening a single connection.
    void reloadIcons(const QModelIndex &parent = QModelIndex());

    std::shared_ptr<DbConnection> dbConnection(const QModelIndex &index);
    QVariant parentNodeProperty(const QModelIndex &index, QString type);
    bool addServer(QString name, QString connectionString);
    bool removeConnection(QModelIndex &index);
    /// Ends the server sessions of \a item and of every node below it. A database
    /// node holds a session of its own, so removing a connection node has to walk
    /// the subtree rather than close one link.
    void closeSubtreeConnections(DbObject *item) noexcept;
    bool alterConnection(QModelIndex &index, QString name, QString connectionString);

private:
    DbObject *_rootItem;

signals:
    void error(QString err);
    void message(QString msg);
    /// A connection behind some node has changed its state without any change
    /// of the tree itself, so the state indicators have to be redrawn.
    void connectionStateChanged();

public slots:
    void saveConnectionSettings();
};

#endif // DBOBJECTSMODEL_H
