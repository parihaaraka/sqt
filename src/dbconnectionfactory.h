#ifndef DBCONNECTIONFACTORY_H
#define DBCONNECTIONFACTORY_H

#include <QString>
#include <memory>

class DbConnection;

/// Picks OdbcConnection or PgConnection for a connection string and wires up
/// the connection string and database. No registry here on purpose - the
/// resulting object is owned by whoever calls this, normally a DbObject tree
/// node (see DbObject::setConnection()) or a clone() of one already in the
/// tree; nothing else needs to look a connection up by name any more.
std::unique_ptr<DbConnection> createDbConnection(const QString &connectionString, const QString &database = QString());

#endif // DBCONNECTIONFACTORY_H
