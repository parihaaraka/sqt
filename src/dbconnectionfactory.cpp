#include "dbconnectionfactory.h"
#include "odbcconnection.h"
#include "pgconnection.h"
#include <QRegularExpression>

std::unique_ptr<DbConnection> createDbConnection(const QString &connectionString, const QString &database)
{
    std::unique_ptr<DbConnection> res;
    static const QRegularExpression re("\\b(dsn|driver)\\s*=", QRegularExpression::CaseInsensitiveOption);
    if (re.match(connectionString).hasMatch())
        res.reset(new OdbcConnection());
    else
        res.reset(new PgConnection());

    res->setConnectionString(connectionString);
    res->setDatabase(database);
    return res;
}
