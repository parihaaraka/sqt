#ifndef DBOBJECT_H
#define DBOBJECT_H

#include <QList>
#include <QVariant>
#include <QFont>
#include <memory>

class DbConnection;

class DbObject
{
public:
    DbObject(DbObject *parent = nullptr);
    DbObject(DbObject *parent, QString text, QString type, QFont font = QFont());
    ~DbObject();

    enum ObjectRole
    {
        IdRole = Qt::UserRole + 1,  ///< unique id
        NameRole,                   ///< unquoted (typically) name
        TypeRole,                   ///< tree node type [connection | database | schema | table | function ...]
        ContentRole,                ///< content data
        ContentTypeRole,            ///< content type [script | text | html | table]
        FavouriteRole,              ///< marked as favourite
        ParentRole,                 ///< if node may have children
        ChildObjectsCountRole,      ///< number of child items with id
        DataRole,
        CurrentSortRole,
        MultiselectRole,
        TagRole,                    ///< any valuable data accessible by $<node type>.tag$ macro
        Sort1Role,
        Sort2Role,
        IconRole                    ///< icon file name as the tree script named it
    };

    void setData(const QVariant &value, int role = Qt::DisplayRole);
    void appendChild(DbObject *item);
    void setParent(DbObject *parent) { _parent = parent; }
    DbObject *child(int row) const { return _children.value(row, nullptr); }
    DbObject *parent() const { return _parent; }
    int childCount() const { return _children.count(); }
    QVariant data(int role = Qt::DisplayRole) const;
    int row() const;
    bool insertChild(int beforeRow);
    bool removeChild(int pos);

    /// This node's own connection if it has one, otherwise the nearest
    /// ancestor's - normally a "connection" or "database" node's, found by
    /// walking up rather than by type, so a caller holding any descendant
    /// (table, column...) does not need to know which ancestor actually owns
    /// it. Returns nullptr if no ancestor (including this node) has one, e.g.
    /// a connection node that has never been connected.
    DbConnection *connection() const;
    /// This node's own connection, without walking up to an ancestor's -
    /// nullptr for every node except the "connection"/"database" one that
    /// actually owns a link.
    DbConnection *ownConnection() const { return _connection.get(); }
    /// Transfers ownership of \a connection to this node; the previous one,
    /// if any, is destroyed. Pass nullptr to drop the connection without
    /// replacing it (e.g. on an explicit "Disconnect").
    void setConnection(std::unique_ptr<DbConnection> connection);

private:
    QList<DbObject*> _children;
    QList<DbObject*> _conceived;
    QHash<int, QVariant> _itemData;
    DbObject *_parent;
    std::unique_ptr<DbConnection> _connection;
};

#endif // DBOBJECT_H
