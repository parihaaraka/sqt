#include "dbobject.h"
#include "dbconnectionfactory.h"
#include "odbcconnection.h"
#include <QUuid>

namespace
{

/// A key no other node can ever hold, minted once per object. See
/// DbObject::connectionKey() for why the node's address would not do.
QString newConnectionKey()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace

DbObject::DbObject(DbObject *parent) : _connectionKey(newConnectionKey())
{
    _parent = parent;
    //setData(true, DbObject::ParentRole);
    setData(0, DbObject::CurrentSortRole);
    setData(false, DbObject::MultiselectRole);
}

DbObject::DbObject(DbObject *parent, QString text, QString type, QFont font) :
    _parent(parent), _connectionKey(newConnectionKey())
{
    setData(text);
    setData(type, DbObject::TypeRole);
    //setData(true, DbObject::ParentRole);
    setData(font, Qt::FontRole);
    setData(0, DbObject::CurrentSortRole);
    setData(false, DbObject::MultiselectRole);
}

DbObject::~DbObject()
{
    qDeleteAll(_conceived);
    _conceived.clear();
    qDeleteAll(_children);
    _children.clear();
    // Unconditional, rather than only for a node whose TypeRole reads
    // connection/database: that role is ordinary node data which a refresh may
    // overwrite, so it cannot be trusted to decide whether there is an entry to
    // drop. Removing a key that was never registered is a no-op, and doing it
    // here keeps the registry from growing for the whole session.
    DbConnectionFactory::removeConnection(_connectionKey);
}

void DbObject::setData(const QVariant &value, int role)
{
    _itemData[role] = value;
}

void DbObject::appendChild(DbObject *item)
{
    _children.append(item);
    item->setParent(this);
}

QVariant DbObject::data(int role) const
{
    QVariant defValue;
    switch (role)
    {
    case DbObject::ParentRole:
        defValue = false;
        break;
    }
    return _itemData.value(role, defValue);
}

int DbObject::row() const
{
    if (_parent)
        return _parent->_children.indexOf(const_cast<DbObject*>(this));
    return 0;
}

bool DbObject::insertChild(int beforeRow)
{
    if (beforeRow > _children.count())
        return false;
    _children.insert(beforeRow, new DbObject(this));
    return true;
}

bool DbObject::removeChild(int pos)
{
    if (pos >= _children.count())
        return false;
    delete _children.at(pos);
    _children.removeAt(pos);
    return true;
}
