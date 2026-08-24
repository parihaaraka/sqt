#include <QApplication>
#include "odbcconnection.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <QDateTime>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtCore/QTextCodec>
#else
#include <QStringConverter>
#endif
#include <QStringList>
#include <QRegularExpression>
#include "datatable.h"
#include <memory>
#include "scripting.h"

template <typename Fn> struct arg7_ptr_type;

template <typename Ret, typename A1, typename A2, typename A3,
          typename A4, typename A5, typename A6, typename A7,
          typename A8, typename A9>
struct arg7_ptr_type<Ret (*)(A1, A2, A3, A4, A5, A6, A7, A8, A9)> {
    using type = std::remove_pointer_t<A7>;
};
// to deal with odbc headers mess (mingw vs  msvc)
using ColSizeT = arg7_ptr_type<decltype(&SQLDescribeColA)>::type;

OdbcConnection::OdbcConnection() :
    DbConnection()
{
    RETCODE retcode;
    _query_state = QueryState::Inactive;

    retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &_henv);
    if (check(retcode, _henv, SQL_HANDLE_ENV))
    {
        retcode = SQLSetEnvAttr(_henv, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(std::intptr_t(SQL_OV_ODBC3)), 0);
        if (check(retcode, _henv, SQL_HANDLE_ENV))
        {
            retcode = SQLAllocHandle(SQL_HANDLE_DBC, _henv, &_hdbc);
            if (check(retcode, _hdbc, SQL_HANDLE_DBC))
                return;
            else if (_hdbc)
                SQLFreeHandle(SQL_HANDLE_DBC, _hdbc);
        }
        SQLFreeHandle(SQL_HANDLE_ENV, _henv);
    }
    _hdbc = nullptr;
    _henv = nullptr;
}

OdbcConnection::~OdbcConnection()
{
    // A query worker captures `this` through execute(), so it must be
    // completely stopped before _hdbc/_henv disappear underneath it - same
    // reasoning as PgConnection::~PgConnection(). Unlike there, no
    // invokeMethod dance into the worker thread is needed to ask it to stop:
    // execute() spends its whole life inside a single blocking ODBC call and
    // never pumps its own event loop, so a queued call to it would just sit
    // there until the call returns on its own. SQLCancel(), on the other
    // hand, is explicitly meant to be invoked from a thread other than the
    // one blocked inside the driver call, so it is called directly here.
    if (_queryThread)
    {
        QThread *thread = _queryThread;
        if (thread->isRunning() && QThread::currentThread() != thread)
        {
            cancel();
            thread->wait();
        }
        else if (thread->isRunning())
        {
            // destructor running on the worker thread itself - nothing to
            // cancel it from; let it unwind (should not normally happen)
            thread->quit();
            thread->wait();
        }
        delete thread;
        _queryThread = nullptr;
        _queryWorker = nullptr;
    }

    OdbcConnection::close();
    if (_hdbc)
        SQLFreeHandle(SQL_HANDLE_DBC, _hdbc);
    if (_henv)
        SQLFreeHandle(SQL_HANDLE_ENV, _henv);
}

DbConnection *OdbcConnection::clone()
{
    OdbcConnection *res = new OdbcConnection();
    res->_connection_string = _connection_string;
    res->_database = _database;
    // Same data source, same script bundle - and known before the clone is
    // opened, just as a connection whose link has died keeps it (see
    // closeLocked()). Without it Scripting has to connect merely to ask the
    // driver for the dbms name, and an unreachable server then costs the clone
    // its scripts and its highlighting dictionary.
    res->_dbmsScriptingID = _dbmsScriptingID;
    return res;
}

bool OdbcConnection::checkStmt(RETCODE retcode, SQLHSTMT handle)
{
    if (retcode == SQL_SUCCESS)
        return true;

    SQLINTEGER NativeError;
    char SqlState[6];
    SQLCHAR BufErrMsg[SQL_MAX_MESSAGE_LENGTH];
    SQLSMALLINT MsgLen;
    RETCODE rc;

    SQLSMALLINT i = 0;
    do
    {
        MsgLen = 0; NativeError = 0;
        rc = SQLGetDiagRecA(SQL_HANDLE_STMT,
                            handle,
                            ++i,
                            reinterpret_cast<SQLCHAR*>(SqlState),
                            &NativeError,
                            BufErrMsg,
                            sizeof(BufErrMsg),
                            &MsgLen);
        // SQLGetDiagRec's *TextLengthPtr reports the *full* length that would
        // be needed, not the truncated length actually copied into
        // BufErrMsg, whenever the message does not fit the buffer (it still
        // returns SQL_SUCCESS_WITH_INFO in that case). Trusting it as an
        // index unconditionally is a one-byte stack buffer overflow the
        // moment a driver returns a diagnostic message longer than
        // SQL_MAX_MESSAGE_LENGTH.
        if (MsgLen < 0)
            MsgLen = 0;
        else if (MsgLen >= SQLSMALLINT(sizeof(BufErrMsg)))
            MsgLen = SQLSMALLINT(sizeof(BufErrMsg) - 1);
        BufErrMsg[MsgLen] = 0;
        if (rc == SQL_NO_DATA)
            break;

        // Statement(s) could not be prepared
        if (NativeError == 8180 && strcmp(SqlState, "42000") == 0)
            continue;

        if (MsgLen > 0 || NativeError)
        {
            bool is_warn = (strcmp(SqlState, "01000") == 0 || strcmp(SqlState, "00000") == 0);
            QString msg = QString::fromLocal8Bit(reinterpret_cast<char*>(BufErrMsg), SQL_NTS);
            if (!is_warn || NativeError)
            {
                msg = tr("%1 %2, state %3: %4").
                        arg(is_warn ? tr("warinig") : tr("error")).
                        arg(NativeError).
                        arg(SqlState, msg);
                if (is_warn)
                    emit message(msg);
                else
                    emit error(msg);
            }
            else
                emit message(msg);

            if (strcmp(SqlState, "08S01") == 0)   // connection broken (first time detected on query execution (SQL_HANDLE_STMT))
            {
                // Reentrant on purpose: this runs synchronously on whichever
                // thread is inside execute() right now (the worker thread for
                // an async query), and close()/open() detect that and skip
                // the cross-thread stop-and-wait dance - see close().
                close();
                if (open())
                    emit message("connection restored\n");
            }
        }
    }
    while (rc == SQL_SUCCESS);

    return (retcode == SQL_SUCCESS_WITH_INFO);
}

bool OdbcConnection::check(RETCODE retcode, SQLHANDLE handle, SQLSMALLINT handle_type) const
{
    if (retcode == SQL_SUCCESS)
        return true;

    SQLINTEGER NativeError;
    char SqlState[6];
    SQLCHAR BufErrMsg[SQL_MAX_MESSAGE_LENGTH];
    SQLSMALLINT MsgLen;
    RETCODE rc;

    SQLSMALLINT i = 0;
    do
    {
        MsgLen = 0; NativeError = 0;
        rc = SQLGetDiagRecA(handle_type,
                            handle,
                            ++i,
                            reinterpret_cast<SQLCHAR*>(SqlState),
                            &NativeError,
                            BufErrMsg,
                            sizeof(BufErrMsg),
                            &MsgLen);
        // see the identical comment in checkStmt()
        if (MsgLen < 0)
            MsgLen = 0;
        else if (MsgLen >= SQLSMALLINT(sizeof(BufErrMsg)))
            MsgLen = SQLSMALLINT(sizeof(BufErrMsg) - 1);
        BufErrMsg[MsgLen] = 0;
        if (rc == SQL_NO_DATA)
            break;

        // Statement(s) could not be prepared
        if (NativeError == 8180 && strcmp(SqlState, "42000") == 0)
            continue;

        if (MsgLen > 0 || NativeError)
        {
            bool is_warn = (strcmp(SqlState, "01000") == 0 || strcmp(SqlState, "00000") == 0);
            QString msg = QString::fromLocal8Bit(reinterpret_cast<char*>(BufErrMsg), SQL_NTS).append("\n");
            if (!is_warn || NativeError)
            {
                msg = tr("%1 %2, state %3: %4").
                        arg(is_warn ? tr("warinig") : tr("error")).
                        arg(NativeError).
                        arg(SqlState, msg);
                if (is_warn)
                    emit message(msg);
                else
                    emit error(msg);
            }
            else
                emit error(msg);
        }
    }
    while (rc == SQL_SUCCESS);
    return (retcode == SQL_SUCCESS_WITH_INFO);
}

QMetaType::Type OdbcConnection::sqlTypeToVariant(int sqlType) const noexcept
{
    QMetaType::Type var_type;
    switch (sqlType)
    {
    case SQL_SMALLINT:
    case SQL_INTEGER:
        var_type = QMetaType::Int;
        break;
    case SQL_BIGINT:
        var_type = QMetaType::LongLong;
        break;
    case SQL_REAL:
    case SQL_FLOAT:
    case SQL_DOUBLE:
        var_type = QMetaType::Double;
        break;
    case SQL_BIT:
        var_type = QMetaType::Bool;
        break;
    case SQL_TINYINT:
        var_type = QMetaType::UChar;
        break;
    case SQL_TYPE_DATE:
        var_type = QMetaType::QDate;
        break;
    case SQL_SS_TIME2:
    case SQL_TYPE_TIME:
        var_type = QMetaType::QTime;
        break;
    case SQL_TYPE_TIMESTAMP:
        var_type = QMetaType::QDateTime;
        break;
    default:
        var_type = QMetaType::QString;
    }
    return var_type;
}

bool OdbcConnection::execute(const QString &query, const QVector<QVariant> *params)
{
    // TODO implement params to use in js-scripts
    Q_UNUSED(params)

    clearResultsets();
    if (!open())
        return false;

    SQLLEN cb;
    static auto rqsplit = QRegularExpression("^go\\s*$",
                                             QRegularExpression::CaseInsensitiveOption |
                                             QRegularExpression::MultilineOption);
    QStringList queries = query.split(rqsplit,
                                  #if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
                                      Qt::SkipEmptyParts
                                  #else
                                      QString::SkipEmptyParts
                                  #endif
                                      );
    SQLHSTMT hstmt_local;
    RETCODE retcode;
    bool alloc_ok;
    {
        // Allocating a statement handle - and, on failure, reading the DBC's
        // own diagnostics for it - touches _hdbc directly, so both have to be
        // serialized against close()/open() the same way PgConnection
        // serializes against _conn. The blocking SQLExecDirect()/SQLFetch()
        // calls below run *without* this lock held, unlike PgConnection's
        // per-call locking around libpq - ODBC gives no way to split a single
        // SQLExecDirect() into short non-blocking steps, so holding
        // _connectionGuard for the whole query would make context()/
        // dbmsInfo() (which also lock it) stall for the query's entire
        // duration instead of a moment.
        QMutexLocker lk(&_connectionGuard);
        retcode = SQLAllocHandle(SQL_HANDLE_STMT, _hdbc, &hstmt_local);
        alloc_ok = check(retcode, _hdbc, SQL_HANDLE_DBC);
    }
    if (!alloc_ok)
        return false;

    {
        QMutexLocker lk(&_hstmtGuard);
        _hstmt = hstmt_local;
    }

    std::unique_ptr<SQLHSTMT, std::function<void(SQLHSTMT*)>> hstmt_guard(&hstmt_local, [this](SQLHSTMT *hstmt)
    {
        // _hstmtGuard stays held across the free itself: cancel() holds the
        // same lock for the duration of its SQLCancel() call, so this either
        // waits for a SQLCancel() already in flight to finish before freeing
        // the handle out from under it, or - if we get here first - makes
        // cancel() see a cleared _hstmt and skip the call entirely. Either
        // way SQLCancel() never touches a freed handle.
        QMutexLocker lk(&_hstmtGuard);
        _hstmt = nullptr;
        SQLFreeHandle(SQL_HANDLE_STMT, *hstmt);
        setQueryState(QueryState::Inactive);
    });

    setQueryState(QueryState::Running);

    _timer.start();
    for (QString &q: queries)
    {
        // ms sql server wants \r\n line ends:
        // TODO check dbms vendor (or something else) to support \n only...
        static auto rle = QRegularExpression("(?<!\r)\n");
        q.replace(rle, "\r\n");

        /*
        1) in case of SQL_CURSOR_STATIC mode SQLRowCount always returns -1 (FreeTDS), and SQLFetch acts very slow
        2) prepared statement incompatible with several features (including showplan)
        */
        retcode = SQLExecDirectA(hstmt_local, reinterpret_cast<SQLCHAR*>(q.toLocal8Bit().data()), SQL_NTS);
        if (retcode == SQL_NO_DATA)
            continue;

        while (checkStmt(retcode, hstmt_local))
        {
            SQLSMALLINT col_count;
            retcode = SQLNumResultCols(hstmt_local, &col_count);
            int rowcount = 0;
            if (checkStmt(retcode, hstmt_local) && col_count)
            {
                DataTable *table = new DataTable();
                QMutexLocker lk(&_resultsetsGuard);
                _resultsets.append(table);
                lk.unlock();

                ColSizeT col_size;
                SQLCHAR buf[512];
                SQLSMALLINT buf_res_length, data_type, dec_digits, nullable_desc;
                for (SQLUSMALLINT i = 0; i < col_count; ++i)
                {
                    SQLColAttributeA(hstmt_local, i + 1, SQL_DESC_TYPE_NAME, buf, sizeof(buf), &buf_res_length, nullptr);
                    QString typeName = QString::fromLocal8Bit(reinterpret_cast<char*>(buf));
                    SQLDescribeColA(hstmt_local, i + 1, buf, sizeof(buf), &buf_res_length, &data_type, &col_size, &dec_digits, &nullable_desc);
                    switch (data_type)
                    {
                    case SQL_DECIMAL:
                    case SQL_NUMERIC:
                        typeName += '(' + QString::number(col_size) +
                                (dec_digits > 0 ? ',' + QString::number(dec_digits) : "") +
                                ')';
                        break;
                    case SQL_FLOAT:
                    case SQL_REAL:
                    case SQL_DOUBLE:
                        if (col_size == 24 || col_size == 53)
                        {
                            typeName = (col_size == 24 ? "real" : "double precision");
                            break;
                        }
                    [[fallthrough]];
                    case SQL_CHAR:
                    case SQL_VARCHAR:
                    case SQL_WCHAR:
                    case SQL_WVARCHAR:
                    case SQL_WLONGVARCHAR:
                    case SQL_BINARY:
                    case SQL_VARBINARY:
                        if (col_size > 0)
                            typeName += '(' +
                                    (col_size == 536870911 || col_size == 1073741823 ?
                                         "max" : QString::number(col_size)) +
                                    ')';
                        break;
                    }
                    table->addColumn(new DataColumn(
                                         QString::fromLocal8Bit(reinterpret_cast<char*>(buf)),
                                         typeName,
                                         sqlTypeToVariant(data_type),
                                         data_type,
                                         col_size,
                                         dec_digits,
                                         int8_t(nullable_desc),
                                         isNumericType(data_type) ?
                                         Qt::AlignRight : Qt::AlignLeft)
                                     );
                }

                while (/*(limit == -1 || rowcount < limit) &&*/ (retcode = SQLFetch(hstmt_local)) != SQL_NO_DATA)
                {
                    if (!checkStmt(retcode, hstmt_local))
                        break;
                    std::unique_ptr<DataRow> row(new DataRow(table));
                    for (SQLUSMALLINT i = 0; i < col_count; ++i)
                    {
                        cb = SQL_NULL_DATA;
                        int type = table->getColumn(i).sqlType();
                        switch (type)
                        {
                        case SQL_SMALLINT:
                        {
                            short num = 0;
                            retcode = SQLGetData(hstmt_local, i + 1, SQL_C_SSHORT, &num, 0, &cb);
                            if (!checkStmt(retcode, hstmt_local) || cb == SQL_NULL_DATA)
                                break;
                            (*row)[i] = num;
                            break;
                        }
                        case SQL_BIGINT:
                        {
                            qint64 num = 0;
                            retcode = SQLGetData(hstmt_local, i + 1, SQL_C_SBIGINT, &num, 0, &cb);
                            if (!checkStmt(retcode, hstmt_local) || cb == SQL_NULL_DATA)
                                break;
                            (*row)[i] = num;
                            break;
                        }
                        case SQL_INTEGER:
                        {
                            qint32 num = 0;
                            retcode = SQLGetData(hstmt_local, i + 1, SQL_C_SLONG, &num, 0, &cb);
                            if (!checkStmt(retcode, hstmt_local) || cb == SQL_NULL_DATA)
                                break;
                            (*row)[i] = num;
                            break;
                        }
                        case SQL_REAL:
                        {
                            float num = 0;
                            retcode = SQLGetData(hstmt_local, i + 1, SQL_C_FLOAT, &num, 0, &cb);
                            if (!checkStmt(retcode, hstmt_local) || cb == SQL_NULL_DATA)
                                break;
                            (*row)[i] = num;
                            break;
                        }
                        case SQL_FLOAT:
                        case SQL_DOUBLE:
                        {
                            double num = 0;
                            retcode = SQLGetData(hstmt_local, i + 1, SQL_C_DOUBLE, &num, 0, &cb);
                            if (!checkStmt(retcode, hstmt_local) || cb == SQL_NULL_DATA)
                                break;
                            (*row)[i] = num;
                            break;
                        }
                        case SQL_BIT:
                        {
                            unsigned char bit = 0;
                            retcode = SQLGetData(hstmt_local, i + 1, SQL_C_BIT, &bit, 0, &cb);
                            if (!checkStmt(retcode, hstmt_local) || cb == SQL_NULL_DATA)
                                break;
                            (*row)[i] = (bit ? true : false);
                            break;
                        }
                        case SQL_TINYINT:
                        {
                            unsigned char bit = 0;
                            retcode = SQLGetData(hstmt_local, i + 1, SQL_C_UTINYINT, &bit, 0, &cb);
                            if (!checkStmt(retcode, hstmt_local) || cb == SQL_NULL_DATA)
                                break;
                            (*row)[i] = bit;
                            break;
                        }
                        case SQL_TYPE_DATE:
                        {
                            DATE_STRUCT date;
                            retcode = SQLGetData(hstmt_local, i + 1, SQL_C_TYPE_DATE, &date, sizeof(DATE_STRUCT), &cb);
                            if (!checkStmt(retcode, hstmt_local) || cb == SQL_NULL_DATA)
                                break;
                            (*row)[i] = QDate(date.year, date.month, date.day);
                            break;
                        }
                        case SQL_SS_TIME2:
                        case SQL_TYPE_TIME:
                        {
                            /*
                            TIME_STRUCT time;
                            retcode = SQLGetData(hstmt, i + 1, SQL_C_TYPE_TIME, &time, sizeof(TIME_STRUCT), &cb);
                            if (!check(retcode, hstmt, SQL_HANDLE_STMT) || cb == SQL_NULL_DATA)
                                break;
                            (*row)[i] = QTime(time.hour, time.minute, time.second);
                            */
                            TIMESTAMP_STRUCT dt;
                            retcode = SQLGetData(hstmt_local, i + 1, SQL_C_TYPE_TIMESTAMP, &dt, sizeof(TIMESTAMP_STRUCT), &cb);
                            if (!checkStmt(retcode, hstmt_local) || cb == SQL_NULL_DATA)
                                break;
                            (*row)[i] = QTime(dt.hour, dt.minute, dt.second, dt.fraction / 1000000);
                            break;
                        }
                        case SQL_TYPE_TIMESTAMP:
                        {
                            TIMESTAMP_STRUCT dt;
                            retcode = SQLGetData(hstmt_local, i + 1, SQL_C_TYPE_TIMESTAMP, &dt, sizeof(TIMESTAMP_STRUCT), &cb);
                            if (!checkStmt(retcode, hstmt_local) || cb == SQL_NULL_DATA)
                                break;
                            (*row)[i] = QDateTime(QDate(dt.year, dt.month, dt.day), QTime(dt.hour, dt.minute, dt.second, dt.fraction / 1000000));
                            break;
                        }
                        case SQL_WCHAR:
                        case SQL_WVARCHAR:
                        case SQL_WLONGVARCHAR:
                        {
                            size_t buf_size = 1024;
                            std::vector<char> buf_storage(buf_size);
                            char *buf_data = buf_storage.data();
                            char *ptr = buf_data;
                            size_t res_len = 0;
                            do
                            {
                                retcode = SQLGetData(hstmt_local, i + 1, SQL_C_WCHAR, ptr, SQLLEN(buf_size - size_t(ptr - buf_data)), &cb);
                                if (!SQL_SUCCEEDED(retcode) || cb == SQL_NULL_DATA)
                                    break;
                                if (retcode == SQL_SUCCESS_WITH_INFO)
                                {
                                    res_len = buf_size - sizeof(SQLWCHAR); // every pass null-terminated
                                    buf_storage.resize(buf_size + size_t(cb));
                                    buf_data = buf_storage.data();
                                    ptr = buf_data + res_len;
                                    buf_size = buf_size + size_t(cb);
                                }
                                else
                                    res_len += size_t(cb);
                            }
                            while (retcode == SQL_SUCCESS_WITH_INFO);
                            if (retcode == SQL_ERROR)
                                break;
                            if (cb != SQL_NULL_DATA)
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                                (*row)[i] = QString::fromUtf16(reinterpret_cast<ushort*>(buf), int(res_len / sizeof(SQLWCHAR)));
#else
                                (*row)[i] = QString::fromUtf16(reinterpret_cast<char16_t*>(buf_data), int(res_len / sizeof(SQLWCHAR)));
#endif
                            //(*row)[i] = QTextCodec::codecForMib(1015)->toUnicode(val); // 1015 is UTF-16, 1014 UTF-16LE, 1013 UTF-16LE
                            break;
                        }
                        default:
                        {
                            size_t buf_size = 1024;
                            std::vector<char> buf_storage(buf_size);
                            char *buf = buf_storage.data();
                            char *ptr = buf;
                            size_t res_len = 0;
                            do
                            {
                                SQLLEN arg_len = static_cast<SQLLEN>(buf_size - size_t(ptr - buf));
                                retcode = SQLGetData(hstmt_local, i + 1, SQL_C_CHAR, ptr, arg_len, &cb);
                                if (!SQL_SUCCEEDED(retcode) || cb == SQL_NULL_DATA)
                                    break;
                                if (retcode == SQL_SUCCESS_WITH_INFO && cb > arg_len) // workaround for sql_variant (always SQL_SUCCESS_WITH_INFO)
                                {
                                    res_len = buf_size - sizeof(SQLCHAR); // every pass null-terminated
                                    buf_storage.resize(buf_size * 2);
                                    buf = buf_storage.data();
                                    ptr = buf + res_len;
                                    buf_size = buf_size * 2;
                                }
                                else
                                    break;
                            }
                            while (retcode == SQL_SUCCESS_WITH_INFO);
                            if (retcode == SQL_ERROR)
                                break;
                            if (cb != SQL_NULL_DATA)
                                (*row)[i] = QString::fromLocal8Bit(buf);
                        }
                        }  // end of switch

                        if (retcode == SQL_ERROR)
                            return false;
                    }

                    QMutexLocker lkt(&table->mutex);
                    table->addRow(row.release());
                    lkt.unlock();

                    ++rowcount;
                    if (rowcount % FETCH_COUNT_NOTIFY == 0)
                        emit fetched(table);
                }
                if (rowcount == 0 || rowcount % FETCH_COUNT_NOTIFY != 0)
                    emit fetched(table);
            }

            if (col_count)
                emit message(tr("%1 rows fetched").arg(rowcount));
            else
            {
                retcode = SQLRowCount(hstmt_local, &cb);
                bool rowcount_ok;
                {
                    // only the diagnostics fetch (on failure) touches _hdbc
                    QMutexLocker lk(&_connectionGuard);
                    rowcount_ok = check(retcode, _hdbc, SQL_HANDLE_DBC);
                }
                if (rowcount_ok && cb != -1)
                    emit message(tr("%1 rows affected").arg(cb));
            }

            //if (rowcount == limit)
            //    SQLCloseCursor(hstmt_local);
            retcode = SQLMoreResults(hstmt_local);
        }
    }

    return true;
}

void OdbcConnection::clarifyTableStructure(DataTable &)
{
    // TODO
}

bool OdbcConnection::executeAsync(const QString &query, const QVector<QVariant> *params) noexcept
{
    // Postgres refuses a query while another one is running, and odbc has to do
    // the same: a second thread would overwrite _hstmt, leaving cancellation to
    // target the wrong statement and the first handle to leak.
    if (queryState() != QueryState::Inactive)
    {
        emit error(tr("another command is already in progress"));
        return false;
    }

    // the lambda outlives this call and runs in another thread, hence the copy:
    // the caller's params may be gone by the time the query starts
    const QVector<QVariant> params_copy = (params ? *params : QVector<QVariant>());

    // Tracked in _queryThread/_queryWorker - unlike the previous, untracked
    // QThread, this lets close() and ~OdbcConnection() find out a query is
    // still running before they touch _hdbc/_henv, exactly as with
    // PgConnection.
    QThread *thread = new QThread();
    QObject *worker = new QObject();
    worker->moveToThread(thread);
    _queryThread = thread;
    _queryWorker = worker;

    connect(thread, &QThread::started, worker, [this, query, params_copy, thread]() {
        execute(query, params_copy.isEmpty() ? nullptr : &params_copy);
        thread->quit();
    });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        // A newer executeAsync() call may already have replaced _queryThread
        // by the time this queued slot runs; only clear our own bookkeeping.
        if (_queryThread == thread)
        {
            _queryThread = nullptr;
            _queryWorker = nullptr;
        }
        emit queryFinished();
        thread->deleteLater();
    });
    thread->start();
    return true;
}

bool OdbcConnection::open()
{
    // Still a fully synchronous, blocking call, same as PQconnectdb() in
    // PgConnection::open() - ODBC has no portable non-blocking connect API to
    // build an openAsync() on top of the way libpq's PQconnectStart() allows.
    // A slow or unreachable server will still stall whichever thread calls
    // this (typically the GUI thread, for the first connect - see the wider
    // review). That is a pre-existing limitation this change does not fix.
    QMutexLocker lk(&_connectionGuard);
    if (!_hdbc)
        return false;
    if (_opened)
        return true;

    SQLCHAR szConnStrOut[1024];
    SQLSMALLINT swStrLen;
    RETCODE retcode;
    SQLPOINTER timeout = reinterpret_cast<SQLPOINTER>(std::intptr_t(10));
    std::string cs = finalConnectionString();

    SQLSetConnectAttrA(_hdbc, SQL_ATTR_CONNECTION_TIMEOUT, timeout, SQL_IS_UINTEGER);
    SQLSetConnectAttrA(_hdbc, SQL_ATTR_LOGIN_TIMEOUT, timeout, SQL_IS_UINTEGER);
    retcode = SQLDriverConnectA(_hdbc, nullptr, const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(cs.c_str())),
                                SQL_NTS, szConnStrOut,
                                1024, &swStrLen, SQL_DRIVER_NOPROMPT);
    if (check(retcode, _hdbc, SQL_HANDLE_DBC))
    {
        _opened = true;
        // dbmsNameLocked()/dbmsVersionLocked(), not the public getters: we
        // already hold _connectionGuard here.
        _dbmsScriptingID = dbmsNameLocked() + dbmsVersionLocked() + "_odbc";
        return true;
    }
    _opened = false;
    return false;
}

QString OdbcConnection::dbmsInfo() const noexcept
{
    // A single lock for both calls - see the comment on execute()'s
    // SQLAllocHandle() for why this can stall for as long as a concurrently
    // running query, not just briefly.
    QMutexLocker lk(&_connectionGuard);
    return dbmsNameLocked() + " v." + dbmsVersionLocked();
}

QString OdbcConnection::dbmsName() const noexcept
{
    QMutexLocker lk(&_connectionGuard);
    return dbmsNameLocked();
}

QString OdbcConnection::dbmsNameLocked() const noexcept
{
    // caller must hold _connectionGuard
    QString info;
    if (!_hdbc)
        return info;
    const SQLSMALLINT buf_size = 256;
    SQLSMALLINT res_size;
    char info_buf[buf_size];
    RETCODE retcode = SQLGetInfoA(_hdbc, SQL_DBMS_NAME, info_buf, buf_size, &res_size);
    if (check(retcode, _hdbc, SQL_HANDLE_DBC))
    //if (retcode == SQL_SUCCESS)
        info = QString::fromLocal8Bit(info_buf, SQL_NTS);
    return info;
}

QString OdbcConnection::dbmsVersion() const noexcept
{
    QMutexLocker lk(&_connectionGuard);
    return dbmsVersionLocked();
}

QString OdbcConnection::dbmsVersionLocked() const noexcept
{
    // caller must hold _connectionGuard
    QString info;
    if (!_hdbc)
        return info;
    const SQLSMALLINT buf_size = 256;
    SQLSMALLINT res_size;
    char info_buf[buf_size];
    RETCODE retcode = SQLGetInfoA(_hdbc, SQL_DBMS_VER, info_buf, buf_size, &res_size);
    if (check(retcode, _hdbc, SQL_HANDLE_DBC))
        info = QString::fromLocal8Bit(info_buf, SQL_NTS);
    return info;
}

int OdbcConnection::dbmsComparableVersion()
{
    if (auto s = Scripting::execute(this, Scripting::Context::Root, "version", nullptr))
    {
        if (!s->resultsets.empty() &&
                s->resultsets.last()->rowCount() == 1 &&
                s->resultsets.last()->columnCount() == 1)
        {
            bool ok;
            int version = s->resultsets.last()->value(0, 0).toInt(&ok);
            if (ok)
                return version;
        }
    }
    return 0x7fffffff;
}

std::string OdbcConnection::finalConnectionString() const noexcept
{
    return ("APP=sqt;" + _connection_string +
            (_database.isEmpty() ?
                 "" : ";Database={" + _database + "};")).toStdString();
}

bool OdbcConnection::isUnquotedType(int sqlType) const noexcept
{
    return (sqlType == SQL_BIT || isNumericType(sqlType));
}

bool OdbcConnection::isNumericType(int sqlType) const noexcept
{
    switch (sqlType)
    {
    case SQL_DECIMAL:
    case SQL_NUMERIC:
    case SQL_TINYINT:
    case SQL_SMALLINT:
    case SQL_INTEGER:
    case SQL_BIGINT:
    case SQL_FLOAT:
    case SQL_DOUBLE:
    case SQL_REAL:
        return true;
    }
    return false;
}

QString OdbcConnection::context() const noexcept
{
    QMutexLocker lk(&_connectionGuard);
    if (!_hdbc)
        return "";

    RETCODE retcode;
    const SQLSMALLINT buf_size = 256;
    SQLSMALLINT res_size;
    char info_buf[buf_size];
    QString user, tmp, context;

    retcode = SQLGetInfoA(_hdbc, SQL_SERVER_NAME, info_buf, buf_size, &res_size);
    if (retcode != SQL_SUCCESS)
        return "";
    //if (check(retcode, _hdbc, SQL_HANDLE_DBC) && res_size)
    context = QString::fromLocal8Bit(info_buf, SQL_NTS);

    retcode = SQLGetInfoA(_hdbc, SQL_USER_NAME, info_buf, buf_size, &res_size);
    if (retcode == SQL_SUCCESS)
        user = QString::fromLocal8Bit(info_buf, SQL_NTS);

    retcode = SQLGetInfoA(_hdbc, SQL_DATABASE_NAME, info_buf, buf_size, &res_size);
    if (retcode == SQL_SUCCESS)
        tmp = QString::fromLocal8Bit(info_buf, SQL_NTS);

    if (context.isEmpty())
        context = tmp;
    else if (!tmp.isEmpty())
        context += '/' + tmp;

    return (user.isEmpty() ? "" : user + "@") + context;
}

QString OdbcConnection::database() const noexcept
{
    return _database;
}

void OdbcConnection::close() noexcept
{
    // SQLDisconnect() must not run concurrently with an in-flight
    // SQLExecDirect()/SQLFetch() on the same _hdbc. Unlike PgConnection's
    // worker, execute() has no event loop of its own to forward a request
    // into while it is blocked inside a driver call - see the destructor -
    // so the only safe option here is to ask it to stop and actually wait for
    // it, rather than trying to run the disconnect "inside" that thread.
    //
    // Called reentrantly, from the worker thread itself (checkStmt()'s 08S01
    // reconnect - see there), there is nothing to stop: this thread *is* the
    // one currently between driver calls, so it goes straight to closeLocked().
    QThread *thread = _queryThread;
    if (thread && thread->isRunning() && QThread::currentThread() != thread)
    {
        cancel();
        thread->wait();
    }

    QMutexLocker lk(&_connectionGuard);
    closeLocked();
}

void OdbcConnection::closeLocked() noexcept
{
    // the caller must hold _connectionGuard

    // _dbmsScriptingID survives on purpose - see PgConnection::closeLocked()
    clearResultsets();
    if (!_hdbc || !_opened)
    {
        _opened = false;
        return;
    }
    SQLDisconnect(_hdbc);
    _opened = false;
}

bool OdbcConnection::isOpened() const noexcept
{
    // Lock-free on purpose - see the comment on _opened in the header, and
    // PgConnection::isOpened() for the underlying reasoning: this is called
    // from the tree's paint routine and the status bar timer, and must never
    // block on _connectionGuard (which a running query can hold for its
    // entire duration) nor make a fresh ODBC call of its own.
    return _opened;
}

void OdbcConnection::cancel() noexcept
{
    if (_query_state != QueryState::Running)
        return;

    setQueryState(QueryState::Cancelling);
    emit message(tr("cancelling..."));

    // SQLCancel() is explicitly meant to be called, without any locking of
    // its own, from a thread other than the one blocked inside the driver
    // call it targets - no separate QThread is needed just to make this call,
    // unlike PgConnection::cancel() reaching into libpq's own cancel API
    // through a temporary handle. _hstmtGuard here is not about serializing
    // with the running query - it is about _hstmt's *lifetime*: it stops
    // execute() from freeing the handle while SQLCancel() is still using it
    // (see the hstmt_guard lambda in execute()), and makes this a safe no-op
    // if the query has already finished and freed it.
    //
    // check(), not checkStmt(): checkStmt()'s 08S01 branch calls close(),
    // which - seeing this same thread's SQLCancel() in flight from a
    // *different* thread than the worker - would call cancel() again to stop
    // it, and that recursive call would deadlock trying to re-lock
    // _hstmtGuard against itself. Nothing here needs the auto-reconnect
    // check() skips, either.
    QMutexLocker lk(&_hstmtGuard);
    if (_hstmt)
        check(SQLCancel(_hstmt), _hstmt, SQL_HANDLE_STMT);
}

/*
void OdbcConnection::gui_execute(const QString &query, const QVector<QVariant> *params, int limit)
{
    execute(query, params, limit);
    emit message(tr("done (%1)").arg(QDateTime::fromMSecsSinceEpoch(_timer.elapsed()).toString("mm:ss.zzz")));
    refreshContext();
}
*/

// get numerics: http://support.microsoft.com/kb/222831
// C data types: http://msdn.microsoft.com/ru-ru/library/ms714556(en-us,VS.85).aspx
