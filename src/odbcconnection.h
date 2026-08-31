#ifndef ODBCCONNECTION_H
#define ODBCCONNECTION_H

#define SQL_VARIANT (-150)  // windows only?
#define SQL_SS_TIME2 (-154) // windows only?

#include <QtGlobal>

#ifdef Q_OS_WIN32
#include <qt_windows.h>
#else
#include <sqltypes.h>
#endif

#include <sql.h>
#include <sqlext.h>
#include <QString>
#include <QThread>
#include <QMutex>
#include <atomic>
#include "dbconnection.h"

class QueryCanceller;

class OdbcConnection : public DbConnection
{
    Q_OBJECT
public:
    OdbcConnection();
    virtual ~OdbcConnection() override;
    virtual DbConnection* clone() override;

    virtual bool open() override;
    virtual void close() noexcept override;
    virtual bool isOpened() const noexcept override;
    virtual void cancel() noexcept override;
    virtual QString context() const noexcept override;
    virtual QString database() const noexcept override;
    virtual QString dbmsInfo() const noexcept override;
    virtual QString dbmsName() const noexcept override;
    virtual QString dbmsVersion() const noexcept override;
    virtual int dbmsComparableVersion() override;
    virtual bool isUnquotedType(int sqlType) const noexcept override;
    virtual bool isNumericType(int sqlType) const noexcept override;
    virtual QMetaType::Type sqlTypeToVariant(int sqlType) const noexcept override;
    virtual bool executeAsync(const QString &query, const QVector<QVariant> *params = nullptr) noexcept override;
    virtual bool execute(const QString &query, const QVector<QVariant> *params = nullptr) override;
    virtual void clarifyTableStructure(DataTable &table) override;

private:
    SQLHENV _henv = nullptr;
    SQLHDBC _hdbc = nullptr;

    /// Whether the link is alive. Maintained under _connectionGuard at every
    /// connect/disconnect/detected loss and read lock-free - see
    /// PgConnection::isOpened() for the reasoning. The tree's paint routine
    /// and the status bar poll this on every repaint, and must never block on
    /// _connectionGuard (which execute() may hold, or effectively occupy for
    /// the whole query - see below) nor make a fresh ODBC call while a query
    /// is running on this connection in another thread.
    ///
    /// This trades the old behaviour - polling SQL_ATTR_CONNECTION_DEAD live
    /// on every repaint, which could notice a server-side drop of an
    /// otherwise idle link immediately - for the same trade-off PgConnection
    /// already makes: an idle link that dies is only noticed the next time
    /// something actually uses it.
    std::atomic_bool _opened {false};

    /// Guards _hstmt's lifetime only (set right after SQLAllocHandle in
    /// execute(), cleared right before SQLFreeHandle when it finishes).
    /// Deliberately a *separate* lock from _connectionGuard: cancel() must be
    /// able to reach SQLCancel() while execute() is blocked inside a long
    /// SQLExecDirect()/SQLFetch() call - i.e. while _connectionGuard could be
    /// held for the whole query, unlike PgConnection where each libpq call is
    /// individually non-blocking - so _hstmtGuard is never held for longer
    /// than a handle get/set/free.
    mutable QMutex _hstmtGuard;
    SQLHSTMT _hstmt = nullptr;

    /// The thread executeAsync() runs execute() in, and the worker object
    /// living in it - tracked exactly like PgConnection's _queryThread /
    /// _queryWorker, so the destructor (and close()) can find out whether a
    /// query is still running on this connection before touching _hdbc/_henv
    /// from another thread.
    QThread *_queryThread = nullptr;
    QObject *_queryWorker = nullptr;

    bool checkStmt(RETCODE retcode, SQLHSTMT handle);
    bool check(RETCODE retcode, SQLHANDLE handle, SQLSMALLINT handle_type) const;
    std::string finalConnectionString() const noexcept;

    /// dbmsName()/dbmsVersion() themselves lock _connectionGuard - callers
    /// that already hold it (open(), dbmsInfo()) must use these instead, or
    /// they would deadlock on the non-recursive mutex. Mirrors
    /// PgConnection::dbmsVersionLocked().
    QString dbmsNameLocked() const noexcept;
    QString dbmsVersionLocked() const noexcept;

    /// close() itself; the caller must hold _connectionGuard.
    void closeLocked() noexcept;
    /// Stops the query worker and waits for it, re-issuing the cancel on every
    /// round and reporting when the driver will not let go. The wait cannot be
    /// abandoned - the caller is about to touch _hdbc/_henv.
    void waitForQueryThread(QThread *thread) noexcept;
};

#endif // ODBCCONNECTION_H
