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

    virtual bool open() = 0;
    virtual void close() noexcept = 0;
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

    void setDatabase(const QString &database) noexcept;
    void setConnectionString(const QString &connectionString);
    QString connectionString() const noexcept;
    QueryState queryState() const noexcept;
    QString elapsed() const noexcept;
    QList<DataTable*> _resultsets;

public slots: // to use from QJSEngine
    virtual DataTable* execute(const QString &query, const QVariantList &params);
    void clearResultsets();

signals:
    void message(const QString &msg) const;
    void error(const QString &msg) const;
    void fetched(DataTable *table);
    void setContext(const QString &context);
    void queryStateChanged();

protected:
    std::atomic<QueryState> _query_state;
    QString _database, _connection_string;
    QTime _timer;
    QMutex _resultsetsGuard; // TODO needs refactoring
    QString _dbmsScriptingID;
    void setQueryState(QueryState queryState);

private:
    int _elapsed_ms = 0;
};

#endif // DBCONNECTION_H
