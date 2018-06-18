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
    virtual void executeAsync(const QString &query, const QVector<QVariant> *params = nullptr) noexcept override;
    virtual bool execute(const QString &query, const QVector<QVariant> *params = nullptr) override;

private:
    SQLHENV _henv;
    SQLHDBC _hdbc;
    std::atomic<SQLHSTMT> _hstmt; // to cancel query from another thread
    bool checkStmt(RETCODE retcode, SQLHSTMT handle);
    bool check(RETCODE retcode, SQLHANDLE handle, SQLSMALLINT handle_type) const;
    std::string finalConnectionString() const noexcept;
};

#endif // ODBCCONNECTION_H
