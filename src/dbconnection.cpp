#include "dbconnection.h"
#include <QVector>
#include <QVariant>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QTimeZone>
#endif

DbConnection::DbConnection() :
    QObject(nullptr)
{
    _query_state = QueryState::Inactive;
}

DbConnection::~DbConnection()
{
    clearResultsets();
}

QString DbConnection::dbmsScriptingID() const noexcept
{
    return _dbmsScriptingID;
}

QString DbConnection::transactionStatus() const noexcept
{
    return "";
}

QString DbConnection::escapeIdentifier(const QString &identifier)
{
    // TODO
    return identifier;
}

QPair<QString, int> DbConnection::typeInfo(int /*sqlType*/)
{
    // TODO
    return {"unknown", -1};
}

void DbConnection::setDatabase(const QString &database) noexcept
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

QString DbConnection::connectionString() const noexcept
{
    return _connection_string;
}

QueryState DbConnection::queryState() const noexcept
{
    return _query_state;
}

DataTable* DbConnection::execute(const QString &query, const QVariantList &params)
{
    QVector<QVariant> p = params.toVector();
    // * synchronous usage only - no need to use _resultsetsGuard
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
        if (state == QueryState::Inactive)
            _elapsed_ms = _timer.elapsed();
        emit queryStateChanged(state);
    }
}

QString DbConnection::elapsed() const noexcept
{
    int elapsed_ms = _elapsed_ms;
    int precision = 3;
    if (_query_state != QueryState::Inactive)
    {
        // round milliseconds during execution because of refresh period 0.2 sec
        elapsed_ms = _timer.elapsed() / 100 * 100;
        // rough precision to display simple float
        precision = 1;
    }

    if (elapsed_ms < 60000)
        return QString::number(elapsed_ms / 1000.0, 'f', precision) + " sec";

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    return QDateTime::fromMSecsSinceEpoch(elapsed_ms, Qt::UTC).
#else
    return QDateTime::fromMSecsSinceEpoch(elapsed_ms, QTimeZone::UTC).
#endif
            toString(elapsed_ms < 60 * 60000 ?
                         "mm:ss.zzz":
                         "HH:mm:ss");
}

void DbConnection::clearResultsets() noexcept
{
    QMutexLocker lk(&_resultsetsGuard);
    qDeleteAll(_resultsets);
    _resultsets.clear();
}
