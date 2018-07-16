#ifndef DBCONNECTION_H
#define DBCONNECTION_H

#include <QObject>
#include <QMutex>
#include <QTime>
#include <atomic>
#include <QJSValueList>
#include <QVector>
#include <memory>
#include "datatable.h"

#define FETCH_COUNT_NOTIFY 1000

enum class QueryState : int { Inactive, Running, Cancelling };
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
    virtual QString hostname() const noexcept = 0;
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
    virtual void executeAsync(const QString &query, const QVector<QVariant> *params = nullptr) noexcept = 0;
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

    void setDatabase(const QString &database) noexcept;
    void setConnectionString(const QString &connectionString);
    QString connectionString() const noexcept;
    QueryState queryState() const noexcept;
    QString elapsed() const noexcept;
    QList<DataTable*> _resultsets;

public slots: // to use from QJSEngine
    virtual DataTable* execute(const QString &query, const QVariantList &params);
    void clearResultsets() noexcept;
    virtual bool open() = 0;
    virtual void close() noexcept = 0;

signals:
    void message(const QString &msg) const;
    void error(const QString &msg) const;
    void fetched(DataTable *table);
    // use QueryState as argument instead of _query_state due to queued connection
    // (slot may have _query_state to be distinct from the state the signal was emitted with)
    void queryStateChanged(QueryState);

protected:
    std::atomic<QueryState> _query_state;
    QTime _timer;
    QString _database;
    QString _connection_string;
    QMutex _resultsetsGuard;
    mutable QMutex _connectionGuard;
    QString _dbmsScriptingID;
    void setQueryState(QueryState queryState);

private:
    int _elapsed_ms = 0;
};

Q_DECLARE_METATYPE(DbConnection*)

#endif // DBCONNECTION_H
