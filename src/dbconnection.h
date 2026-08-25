#ifndef DBCONNECTION_H
#define DBCONNECTION_H

#include <QObject>
#include <QMutex>
#include <QTime>
#include <QElapsedTimer>
#include <atomic>
#include <QJSValueList>
#include <QVector>
#include <memory>
#include "datatable.h"

#define FETCH_COUNT_NOTIFY 1000

enum class QueryState : int { Inactive, Reconnecting, Running, Cancelling };
enum SocketWatchMode { None = 0, Read, Write };

Q_DECLARE_METATYPE(QueryState)
class DataTable;

/*
class ResultSets : QObject
{
    Q_OBJECT
public:
    ResultSets(QObject *parent = 0) : QObject(parent) {}
    ~ResultSets() {
        qDeleteAll(resultsets);
        resultsets.clear();
    }

public slots:
    Q_INVOKABLE DataTable* at(int i);

private:
    QList<DataTable*> resultsets;
};
*/

class DbConnection : public QObject
{
    Q_OBJECT
public:
    DbConnection();
    virtual ~DbConnection();
    virtual DbConnection* clone() = 0;

    virtual bool isOpened() const noexcept = 0;
    virtual void cancel() noexcept = 0;

    virtual QString context() const noexcept = 0;
    virtual QString database() const noexcept = 0;
    virtual QString dbmsInfo() const noexcept = 0;
    virtual QString dbmsName() const noexcept = 0;
    virtual QString dbmsVersion() const noexcept = 0;
    virtual QString dbmsScriptingID() const noexcept;
    virtual QString transactionStatus() const noexcept;
    virtual int dbmsComparableVersion() = 0;
    /*!
     * \brief determine if a value of sqlType must be quoted
     * \param sqlType provider-specific data type identity
     *
     * Although pg's Oid is unsigned int, it's small values let us use signed int to
     * support both ms sql and postgresql. Or may be we should not spare bits and switch
     * to int64_t?
     */
    virtual bool isUnquotedType(int sqlType) const noexcept = 0;
    /*!
     * \brief determine if sqlType is a numeric type, to right-align value when needed
     * \param sqlType provider-specific data type identity
     */
    virtual bool isNumericType(int sqlType) const noexcept = 0;
    virtual QMetaType::Type sqlTypeToVariant(int sqlType) const noexcept = 0;
    /*!
     * \brief asynchronous query execution from within query editor
     */
    /// Starts a query and returns false if it was refused right away.
    /// A refused query reports nothing afterwards - no state change and no
    /// queryFinished() - so the caller must not wait for it.
    virtual bool executeAsync(const QString &query, const QVector<QVariant> *params = nullptr) noexcept = 0;
    /*!
     * \brief synchronous query execution used by objects tree and so on
     */
    virtual bool execute(const QString &query, const QVector<QVariant> *params = nullptr) = 0;

    virtual QString escapeIdentifier(const QString &identifier);

    /*!
     * \brief name of internal dbms type and its element internal id (if array)
     * \param sqlType dbms type id
     * \return name, element id (-1 if not an array)
     */
    virtual QPair<QString, int> typeInfo(int sqlType);

    virtual void clarifyTableStructure(DataTable &table) = 0;

    void setDatabase(const QString &database) noexcept;
    void setConnectionString(const QString &connectionString);
    QString connectionString() const noexcept;
    QueryState queryState() const noexcept;
    QString elapsed() const noexcept;
    /// The tables the connection has accumulated so far.
    ///
    /// Do *not* walk this list in place from another thread. It is appended to
    /// by the query thread (fetch()) and by libpq's notice callback, which runs
    /// in whichever thread talks to the server, so an unguarded traversal or
    /// removal races with a reallocation of the list and corrupts the heap.
    /// Use takeResultsets() to consume the tables and resultsetsSnapshot() to
    /// look at them; both do their work under _resultsetsGuard.
    QList<DataTable*> _resultsets;
    /// Hands the accumulated tables over to the caller and leaves the list
    /// empty. Ownership travels with them: nothing else will free them.
    QList<DataTable*> takeResultsets() noexcept;
    /// A copy of the list, for a reader that only looks at the tables. The
    /// tables stay owned by the connection, so the copy is valid for as long as
    /// no one clears the resultsets (the next query run does).
    QList<DataTable*> resultsetsSnapshot() const noexcept;

public slots: // to use from QJSEngine
    virtual DataTable* execute(const QString &query, const QVariantList &params);
    void clearResultsets() noexcept;
    virtual bool open() = 0;
    virtual void close() noexcept = 0;

signals:
    void message(const QString &msg) const;
    void error(const QString &msg) const;
    /// The link is gone (it will be restored by the next query). The connection
    /// object itself stays alive and registered, so this is not a disconnect -
    /// it only means that whatever displays the connection's state is stale now.
    void connectionLost();
    /// The run ended without an answer, and whether the server executed the
    /// query is unknowable: it had been delivered in full when the link died.
    /// Emitted just before the Inactive state, so that the widget can say so
    /// instead of reporting the run as completed - "done" would invite a retry,
    /// and a retry may repeat a statement that has already had its effect.
    void outcomeUnknown();

    void fetched(DataTable *table);
    // use QueryState as argument instead of _query_state due to queued connection
    // (slot may have _query_state to be distinct from the state the signal was emitted with)
    void queryStateChanged(QueryState);
    void queryFinished();

protected:
    std::atomic<QueryState> _query_state;
    QElapsedTimer _timer;
    QString _database;
    QString _connection_string;
    mutable QMutex _resultsetsGuard;
    mutable QMutex _connectionGuard;
    QString _dbmsScriptingID;
    void setQueryState(QueryState queryState);

private:
    int _elapsed_ms = 0;
};

Q_DECLARE_METATYPE(DbConnection*)

#endif // DBCONNECTION_H
