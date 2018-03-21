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
    virtual bool execute(const QString &query, const QVector<QVariant> *params = nullptr, int limit = -1) = 0;

    void setDatabase(const QString &database);
    void setConnectionString(const QString &connectionString);
    QString connectionString() const;
    QueryState queryState() const;
    QList<DataTable*> _resultsets;

public slots: // to use from QJSEngine
    virtual DataTable* execute(const QString &query, const QVariantList &params);
    void clearResultsets();

signals:
    void message(QString msg) const;
    void error(QString msg) const;
    void fetched(DataTable *table);
    void setContext(QString context) const;
    void queryStateChanged();

protected:
    std::atomic<QueryState> _query_state;
    QString _database, _connection_string;
    QTime _timer;
    QMutex _resultsetsGuard; // TODO needs refactoring
    void setQueryState(QueryState queryState);
    QString elapsed();
};

#endif // DBCONNECTION_H
