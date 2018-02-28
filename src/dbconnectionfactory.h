#ifndef DBCONNECTIONFACTORY_H
#define DBCONNECTIONFACTORY_H

#include <QHash>
#include <QString>
#include <memory>

class DbConnection;

class DbConnectionFactory
{
public:
    DbConnectionFactory() = delete;
    static std::shared_ptr<DbConnection> connection(QString name);
    static std::shared_ptr<DbConnection> createConnection(QString name, QString connectionString = QString(), QString database = QString());
    static void removeConnection(QString name);
private:
    static QHash<QString, std::shared_ptr<DbConnection>> _connections;
};

#endif // DBCONNECTIONFACTORY_H
