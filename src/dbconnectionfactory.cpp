#include "dbconnectionfactory.h"
#include "odbcconnection.h"
#include "pgconnection.h"
#include <QRegularExpression>

QHash<QString, std::shared_ptr<DbConnection>> DbConnectionFactory::_connections;

std::shared_ptr<DbConnection> DbConnectionFactory::connection(QString name)
{
    if (_connections.contains(name))
        return _connections[name];
    return nullptr;
}

std::shared_ptr<DbConnection> DbConnectionFactory::createConnection(QString name, QString connectionString, QString database)
{
    std::shared_ptr<DbConnection> res;
    QRegularExpression re("\\b(dsn|driver)\\s*=", QRegularExpression::CaseInsensitiveOption);
    if (re.match(connectionString).hasMatch())
        res = std::shared_ptr<DbConnection>(new OdbcConnection());
    else
        res = std::shared_ptr<DbConnection>(new PgConnection());

    if (!name.isEmpty())
        _connections[name] = res;

    res->setConnectionString(connectionString);
    res->setDatabase(database);
    return res;
}

void DbConnectionFactory::removeConnection(QString name)
{
    if (!_connections.contains(name))
        return;
    _connections.remove(name);
}

