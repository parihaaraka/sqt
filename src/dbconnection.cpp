#include "dbconnection.h"
#include <QVector>
#include <QVariant>

DbConnection::DbConnection() :
    QObject(nullptr)
{
    _query_state = QueryState::Inactive;
}

DbConnection::~DbConnection()
{
    clearResultsets();
}

void DbConnection::setDatabase(const QString &database)
{
    if (_database != database)
    {
        close();
        _database = database;
    }
}

void DbConnection::setConnectionString(const QString &connectionString)
{
    close();
    _connection_string = connectionString;
}

QString DbConnection::connectionString() const
{
    return _connection_string;
}

QueryState DbConnection::queryState() const
{
    return _query_state;
}

DataTable* DbConnection::execute(const QString &query, const QVariantList &params)
{
    QVector<QVariant> p = params.toVector();
    if (execute(query, &p) && !_resultsets.empty())
    {
        // return only last resultset to keep scripting api simple
        // (script takes ownership of the pointer)
        return _resultsets.takeLast();
    }
    return nullptr;
}

void DbConnection::setQueryState(QueryState state)
{
    if (_query_state != state)
    {
        _query_state = state;
        emit queryStateChanged();
    }
}

QString DbConnection::elapsed()
{
    int elapsed_ms = _timer.elapsed();
    return QDateTime::fromMSecsSinceEpoch(elapsed_ms).
            toString(elapsed_ms < 60000 ?
                         "s.z 'sec'" :
                         elapsed_ms < 60*60000 ?
                             "mm:ss.zzz":
                             "HH:mm:ss");
}

void DbConnection::clearResultsets()
{
    qDeleteAll(_resultsets);
    _resultsets.clear();
}
