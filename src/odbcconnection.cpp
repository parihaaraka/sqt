#include <QApplication>
#include "odbcconnection.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <QDateTime>
#include <QTextCodec>
#include <QStringList>
#include <QRegularExpression>
#include "datatable.h"
#include <memory>
#include "scripting.h"

OdbcConnection::OdbcConnection() :
    DbConnection()
{
    RETCODE retcode;
    _henv = nullptr;
    _hstmt = nullptr;
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
    close();
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
                        arg(SqlState).
                        arg(msg);
                if (is_warn)
                    emit message(msg);
                else
                    emit error(msg);
            }
            else
                emit message(msg);

            if (strcmp(SqlState, "08S01") == 0)   // connection broken (first time detected on query execution (SQL_HANDLE_STMT))
            {
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
                        arg(SqlState).
                        arg(msg);
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
    QStringList queries = query.split(QRegularExpression("^go\\s*$",
                                                         QRegularExpression::CaseInsensitiveOption |
                                                         QRegularExpression::MultilineOption),
                                      QString::SkipEmptyParts);
    SQLHSTMT hstmt_local;
    RETCODE retcode = SQLAllocHandle(SQL_HANDLE_STMT, _hdbc, &hstmt_local);
    if (!check(retcode, _hdbc, SQL_HANDLE_DBC))
        return false;
    _hstmt = hstmt_local;

    std::unique_ptr<SQLHSTMT, std::function<void(SQLHSTMT*)>> hstmt_guard(&hstmt_local, [this](SQLHSTMT *hstmt)
    {
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
        q.replace(QRegularExpression("(?<!\r)\n"), "\r\n");

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

                SQLULEN col_size;
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
                    //[[fallthrough]];
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
                            char *buf = buf_storage.data();
                            char *ptr = buf;
                            size_t res_len = 0;
                            do
                            {
                                retcode = SQLGetData(hstmt_local, i + 1, SQL_C_WCHAR, ptr, SQLLEN(buf_size - size_t(ptr - buf)), &cb);
                                if (!SQL_SUCCEEDED(retcode) || cb == SQL_NULL_DATA)
                                    break;
                                if (retcode == SQL_SUCCESS_WITH_INFO)
                                {
                                    res_len = buf_size - sizeof(SQLWCHAR); // every pass null-terminated
                                    buf_storage.resize(buf_size + size_t(cb));
                                    buf = buf_storage.data();
                                    ptr = buf + res_len;
                                    buf_size = buf_size + size_t(cb);
                                }
                                else
                                    res_len += size_t(cb);
                            }
                            while (retcode == SQL_SUCCESS_WITH_INFO);
                            if (retcode == SQL_ERROR)
                                break;
                            if (cb != SQL_NULL_DATA)
                                (*row)[i] = QString::fromUtf16(reinterpret_cast<ushort*>(buf), int(res_len / sizeof(SQLWCHAR)));
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

                    QMutexLocker lk(&table->mutex);
                    table->addRow(row.release());
                    lk.unlock();

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
                if (check(retcode, _hdbc, SQL_HANDLE_DBC) && cb != -1)
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

void OdbcConnection::executeAsync(const QString &query, const QVector<QVariant> *params) noexcept
{
    QThread* thread = new QThread();
    connect(thread, &QThread::started, thread, [this, query, thread, params]() {
        execute(query, params);
        thread->quit();
    });
    connect(thread, &QThread::finished, [this, thread]() {
        thread->deleteLater();
        emit queryFinished();
    });
    thread->start();
}

bool OdbcConnection::open()
{
    if (!_hdbc)
        return false;
    if (isOpened())
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
        _dbmsScriptingID = dbmsName() + dbmsVersion() + "_odbc";
        return true;
    }
    return false;
}

QString OdbcConnection::dbmsInfo() const noexcept
{
    return dbmsName() + " v." + dbmsVersion();
}

QString OdbcConnection::dbmsName() const noexcept
{
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
    _dbmsScriptingID.clear();
    clearResultsets();
    if (!isOpened())
        return;
    SQLDisconnect(_hdbc);
}

bool OdbcConnection::isOpened() const noexcept
{
    if (!_hdbc)
        return false;
    SQLINTEGER isConnectionDead;
    RETCODE retcode = SQLGetConnectAttr(_hdbc,
                                        SQL_ATTR_CONNECTION_DEAD,
                                        &isConnectionDead,
                                        sizeof(isConnectionDead),
                                        nullptr);
    if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
        return !isConnectionDead;
    return false;
}

void OdbcConnection::cancel() noexcept
{
    SQLHSTMT hstmt_local = _hstmt;
    if (_query_state == QueryState::Running)
    {
        setQueryState(QueryState::Cancelling);
        emit message(tr("cancelling..."));

        QThread* thread = new QThread;
        connect(thread, &QThread::started, [this, hstmt_local]() {
            checkStmt(SQLCancel(hstmt_local), hstmt_local);
        });
        connect(thread, &QThread::finished, [thread]() {
            thread->deleteLater();
        });
        thread->start();
    }
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
