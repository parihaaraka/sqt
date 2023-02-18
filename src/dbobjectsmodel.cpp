#include "dbobjectsmodel.h"
#include "dbobject.h"
#include <QSettings>
#include <QIcon>
#include <QFile>
#include <QDir>
#include <QMap>
#include <QApplication>
#include <QRegularExpression>
#include "dbconnectionfactory.h"
#include "dbconnection.h"
#include <memory>
#include "datatable.h"
#include "odbcconnection.h"
#include <QUuid>
#include "dbosortfilterproxymodel.h"
#include "scripting.h"

DbObjectsModel::DbObjectsModel(QObject *parent) :
    QAbstractItemModel(parent)
{
    _rootItem = new DbObject();
    _rootItem->setData("root", DbObject::TypeRole);
}

DbObjectsModel::~DbObjectsModel()
{
    delete _rootItem;
}

QModelIndex DbObjectsModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();
    DbObject *parentItem = static_cast<DbObject*>(index.internalPointer())->parent();
    if (parentItem == _rootItem)
        return QModelIndex();
    return createIndex(parentItem->row(), 0, parentItem);
}

int DbObjectsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;

    DbObject *parentItem = parent.isValid() ?
                static_cast<DbObject*>(parent.internalPointer()) :
                _rootItem;

    return parentItem->childCount();
}

QModelIndex DbObjectsModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    DbObject *parentItem = parent.isValid() ?
                static_cast<DbObject*>(parent.internalPointer()) :
                _rootItem;

    DbObject *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    return QModelIndex();
}

QVariant DbObjectsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();
    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    if (role == Qt::DisplayRole)
    {
        auto initial_value = item->data(role);
        if (!initial_value.isValid())
            initial_value = item->data(Qt::EditRole);
        QVariant childCount = item->data(DbObject::ChildObjectsCountRole);
        return initial_value.toString() + (childCount.isValid() ?
                                               " (" + QString::number(childCount.toInt()) + ")" : "");
    }
    return item->data(role);
}

bool DbObjectsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;

    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    item->setData(value, role);
    emit dataChanged(index, index);
    return true;
}

Qt::ItemFlags DbObjectsModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags(index);
}

bool DbObjectsModel::insertRows(int pos, int count, const QModelIndex &parent)
{
    DbObject *parentItem = parent.isValid() ?
                static_cast<DbObject*>(parent.internalPointer()) :
                _rootItem;

    if (count > parentItem->childCount())
        return false;
    beginInsertRows(parent, pos, pos + count - 1);
    for (int i = 0; i < count; ++i)
        parentItem->insertChild(pos);
    endInsertRows();
    return true;
}

bool DbObjectsModel::removeRows(int pos, int count, const QModelIndex &parent)
{
    DbObject *parentItem = parent.isValid() ?
                static_cast<DbObject*>(parent.internalPointer()) :
                _rootItem;

    if (pos + count > parentItem->childCount())
        return false;
    beginRemoveRows(parent, pos, pos + count - 1);
    for (int i = 0; i < count; ++i)
        parentItem->removeChild(pos);
    endRemoveRows();
    return true;
}

bool DbObjectsModel::hasChildren(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return true;
    DbObject *item = static_cast<DbObject*>(parent.internalPointer());
    return item->data(DbObject::ParentRole).toBool();
}

bool DbObjectsModel::canFetchMore(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return false;
    DbObject *item = static_cast<DbObject*>(parent.internalPointer());
    return (!item->childCount() && item->data(DbObject::ParentRole).toBool());
}

void DbObjectsModel::fetchMore(const QModelIndex &parent)
{
    if (!parent.isValid())
        return;
    fillChildren(parent);
    DbObject *item = static_cast<DbObject*>(parent.internalPointer());
    setData(parent, item->childCount() ? true : false, DbObject::ParentRole);
}

bool DbObjectsModel::fillChildren(const QModelIndex &parent)
{
    DbObject *parentNode = parent.isValid() ?
                static_cast<DbObject*>(parent.internalPointer()) :
                _rootItem;
    QString type = parentNode->data(DbObject::TypeRole).toString();

    if (type == "root")  //servers
    {
        QSettings settings;
        int size = settings.beginReadArray("servers");
        for (int i = 0; i < size; ++i) {
            settings.setArrayIndex(i);
            beginInsertRows(parent, i, i);
            parentNode->insertChild(i);
            DbObject *newItem = parentNode->child(i);
            newItem->setData(QUuid::createUuid(), DbObject::IdRole);
            newItem->setData(settings.value("connection_string").toString(), DbObject::DataRole);
            auto name = settings.value("name").toString();
            newItem->setData(name, Qt::EditRole);
            // data() returns adjusted DisplayRole (with number of children)
            newItem->setData(settings.value("user").toString(), DbObject::NameRole);
            newItem->setData("connection", DbObject::TypeRole);
            //newItem->setData(QString::number(std::intptr_t(newItem)), DbObject::IdRole); // id to use within connections storage
            newItem->setData(false, DbObject::ParentRole);
            newItem->setData(QIcon(":img/server.png"), Qt::DecorationRole);
            endInsertRows();
        }
        settings.endArray();
        return true;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    ScopeGuard<void(*)()> cursorGuard(QApplication::restoreOverrideCursor);

    int insertPosition = parentNode->childCount();
    int childObjectsCount = 0;
    try
    {
        std::shared_ptr<DbConnection> con = dbConnection(parent);
        if (!con->open())
            throw QString("");

        auto c = Scripting::execute(con, Scripting::Context::Tree, type,
                                    [this, &parent](QString macro) -> QVariant
            {
                return parentNodeProperty(parent, macro);
            });
        DataTable *table = (c && !c->resultsets.isEmpty() ? c->resultsets.back() : nullptr);

        if (!table)
            return true;
        /*
        // remove current node in case of error during scripting
        if (!table)
        {
            if (type != "connection" && parent.data(DbObject::IdRole).isValid())
                removeRow(parent.row(), parent.parent());
            return false;
        }
        */

        // http://msdn.microsoft.com/en-us/library/ms403629(v=sql.105).aspx
        int typeInd = table->getColumnOrd("node_type");
        int textInd = table->getColumnOrd("ui_name");
        int nameInd = table->getColumnOrd("name");
        int idInd = table->getColumnOrd("id");
        int iconInd = table->getColumnOrd("icon");
        int sort1Ind = table->getColumnOrd("sort1");
        int sort2Ind = table->getColumnOrd("sort2");
        int multiselectInd = table->getColumnOrd("allow_multiselect");
        int tagInd = table->getColumnOrd("tag");

        for (int i = 0; i < table->rowCount(); ++i)
        {
            DataRow &r = table->getRow(i);
            std::unique_ptr<DbObject> newItem(new DbObject(parentNode));
            newItem->setData(r[textInd].toString(), Qt::DisplayRole);
            if (idInd >= 0 && !r[idInd].isNull())
            {
                newItem->setData(r[idInd].toString(), DbObject::IdRole);
                ++childObjectsCount;
            }
            if (nameInd >= 0 && !r[nameInd].isNull())
                newItem->setData(r[nameInd].toString(), DbObject::NameRole);
            if (iconInd >= 0 && !r[iconInd].isNull())
                newItem->setData(QIcon(QApplication::applicationDirPath() + "/decor/" + r[iconInd].toString()), Qt::DecorationRole);

            // children detection
            newItem->setData(Scripting::getScript(
                                 dbConnection(parent).get(),
                                 Scripting::Context::Tree,
                                 r[typeInd].toString()) != nullptr, DbObject::ParentRole);

            if (sort1Ind >= 0 && !r[sort1Ind].isNull())
                newItem->setData(r[sort1Ind], DbObject::Sort1Role);
            if (sort2Ind >= 0 && !r[sort2Ind].isNull())
                newItem->setData(r[sort2Ind], DbObject::Sort2Role);
            if (multiselectInd >= 0 && !r[multiselectInd].isNull())
                newItem->setData(r[multiselectInd].toBool(), DbObject::MultiselectRole);
            if (tagInd >= 0 && !r[tagInd].isNull())
                newItem->setData(r[tagInd], DbObject::TagRole);
            if (typeInd >= 0)
            {
                QString value = r[typeInd].toString();
                newItem->setData(value, DbObject::TypeRole);
                if (value == "database")
                {
                    // find connection string donor (top-level connection)
                    DbObject *parent = newItem->parent();
                    while (parent && parent->data(DbObject::TypeRole).toString() != "connection")
                        parent = parent->parent();

                    // initialize database-specific connection
                    if (parent)
                    {
                        QString cs = DbConnectionFactory::connection(QString::number(std::intptr_t(parent)))->connectionString();
                        QString id = QString::number(std::intptr_t(newItem.get()));
                        auto db = DbConnectionFactory::createConnection(id, cs, newItem->data(DbObject::NameRole).toString());
                        connect(db.get(), &DbConnection::error, this, &DbObjectsModel::error);
                        connect(db.get(), &DbConnection::message, this, &DbObjectsModel::message);
                    }
                }
            }

            beginInsertRows(parent, insertPosition, insertPosition);
            parentNode->appendChild(newItem.release());
            endInsertRows();
            ++insertPosition;
        }
    }
    catch (const QString &err)
    {
        emit error(err);
    }

    parentNode->setData(parentNode->childCount() > 0, DbObject::ParentRole);
    QVariant parentDbId = parentNode->data(DbObject::IdRole);
    // to show number of db objects if parent is folder or counter > 5 (it's about function.arguments, table.columns, etc)
    if (childObjectsCount && (!parentDbId.isValid() || childObjectsCount > 5))
        parentNode->setData(childObjectsCount, DbObject::ChildObjectsCountRole);
    return true;
}

std::shared_ptr<DbConnection> DbObjectsModel::dbConnection(const QModelIndex &index)
{
    std::shared_ptr<DbConnection> con;
    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    while (!con && item)
    {
        QString type = item->data(DbObject::TypeRole).toString();
        if (type == "connection" || type == "database")
        {
            con = DbConnectionFactory::connection(QString::number(std::intptr_t(item)));
            break; // con may be nullptr
        }
        else
            item = item->parent();
    }
    return con;
}

QVariant DbObjectsModel::parentNodeProperty(const QModelIndex &index, QString type)
{
    QVariant envValue;
    QStringList parts = type.split('.',
                               #if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
                                   Qt::SkipEmptyParts
                               #else
                                   QString::SkipEmptyParts
                               #endif
                                   );
    if (parts.size() > 2 || parts.isEmpty())
        return envValue;
    QString searchType = parts.at(0);
    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    do
    {
        if (item->data(DbObject::TypeRole) == searchType)
        {
            if (parts.size() == 1 || !parts.at(1).compare("id", Qt::CaseInsensitive))
                envValue = item->data(DbObject::IdRole);
            else if (!parts.at(1).compare("name", Qt::CaseInsensitive))
                envValue = item->data(DbObject::NameRole);
            else if (!parts.at(1).compare("tag", Qt::CaseInsensitive))
                envValue = item->data(DbObject::TagRole);
            else
                break;
        }
        item = item->parent();
    }
    while (!envValue.isValid() && item);
    return envValue;
}

bool DbObjectsModel::addServer(QString name, QString connectionString)
{
    QModelIndex parentIndex;
    int ind = _rootItem->childCount();
    // prevent duplicate server name
    for (int i = 0; i < ind; ++i)
    {
        if (_rootItem->child(i)->data(Qt::DisplayRole).toString() == name)
            return false;
    }

    DbObject *new_connection = new DbObject(_rootItem);
    beginInsertRows(parentIndex, ind, ind);
    new_connection->setData(connectionString, DbObject::DataRole);
    new_connection->setData(name, Qt::DisplayRole);
    new_connection->setData("connection", DbObject::TypeRole);
    new_connection->setData(false, DbObject::ParentRole);
    new_connection->setData(QIcon(":img/server.png"), Qt::DecorationRole);
    _rootItem->appendChild(new_connection);
    endInsertRows();

    saveConnectionSettings();
    return true;
}

bool DbObjectsModel::removeConnection(QModelIndex &index)
{
    if (!index.isValid())
        return false;
    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    if (item->data(DbObject::TypeRole) != "connection")
        return false;

    removeRows(index.row(), 1);
    saveConnectionSettings();
    return true;
}

bool DbObjectsModel::alterConnection(QModelIndex &index, QString name, QString connectionString)
{
    if (!index.isValid())
        return false;
    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    if (item->data(DbObject::TypeRole) != "connection")
        return false;

    setData(index, name, Qt::DisplayRole);
    setData(index, connectionString, DbObject::DataRole);
    saveConnectionSettings();
    return true;
}

void DbObjectsModel::saveConnectionSettings()
{
    QSettings settings;
    int count = _rootItem->childCount();
    settings.beginWriteArray("servers", count);
    for (int i = 0; i < count; ++i)
    {
        settings.setArrayIndex(i);
        settings.setValue("connection_string", _rootItem->child(i)->data(DbObject::DataRole).toString());
        settings.setValue("name", _rootItem->child(i)->data(Qt::DisplayRole).toString());
        settings.setValue("user", _rootItem->child(i)->data(DbObject::NameRole).toString());
    }
    settings.endArray();
}
