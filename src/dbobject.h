#ifndef DBOBJECT_H
#define DBOBJECT_H

#include <QList>
#include <QVariant>
#include <QFont>

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
        Sort2Role
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

private:
    QList<DbObject*> _children;
    QList<DbObject*> _conceived;
    QHash<int, QVariant> _itemData;
    DbObject *_parent;
};

#endif // DBOBJECT_H
