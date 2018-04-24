#include "pgconnection.h"
#include "pgtypes.h"
#include <QApplication>
#include <QVector>
#include <QTextStream>
#include <QSocketNotifier>
#include <QRegularExpression>
#include <QThread>

PgConnection::PgConnection() :
    DbConnection(), _readNotifier(nullptr), _writeNotifier(nullptr), _temp_result(nullptr), _temp_result_rowcount(0)
{
    connect(&_copy_context, &PgCopyContext::error, this, &PgConnection::error);
    connect(&_copy_context, &PgCopyContext::message, this, &PgConnection::message);
}

PgConnection::~PgConnection()
{
    if (_temp_result)
    {
        delete _temp_result;
        _temp_result = nullptr;
    }
    close();
}

DbConnection *PgConnection::clone()
{
    PgConnection *res = new PgConnection();
    res->_connection_string = _connection_string;
    res->_database = _database;
    return res;
}

bool PgConnection::open()
{
    //_last_action_moment = chrono::system_clock::now();

    // if current connection is actually broken, the further query will detect it and will try to reconnect
    // (but it may looks like ok here)
    if (_conn)
    {
        if (PQstatus(_conn) == CONNECTION_OK)
            return true;
        close();
    }

    _conn = PQconnectdb(finalConnectionString().c_str());
    if (PQstatus(_conn) != CONNECTION_OK)
    {
        // man: "...a nonempty PQerrorMessage result can consist of multiple lines, and will include a trailing newline.
        // The caller should not free the result directly."
        emit error(PQerrorMessage(_conn));
        PQfinish(_conn);
        _conn = nullptr;
        return false;
    }

    _dbmsScriptingID = dbmsName() + dbmsVersion();

    // _database is initially empty within 'connection' node
    // (used to display current context (no need to set it in async method)
    if (_database.isEmpty())
        _database = PQdb(_conn);

    // set notice and warning messages handler
    PQsetNoticeReceiver(_conn, noticeReceiver, this);
    PQsetnonblocking(_conn, 1);

    // watch socket to receive notifications
    watchSocket(SocketWatchMode::Read);

    return true;
}

void PgConnection::openAsync() noexcept
{
    if (_async_stage != async_stage::none || !isIdle())
    {
        int status = PQtransactionStatus(_conn);
        emit error(tr("unable to open connection (transaction status %1)").arg(status));
        return;
    }

    // if current connection is actually broken, the further query will detect it and will try to reconnect
    // (but it may looks like ok here)
    if (_conn)
    {
        if (PQstatus(_conn) == CONNECTION_OK) // already connected -
            return;                           // silent exit !!!
        close();
    }

    //time(&_connection_start_moment);
    //_last_try = _connection_start_moment;

    _conn = PQconnectStart(finalConnectionString().c_str());
    if (PQstatus(_conn) == CONNECTION_BAD)
    {
        // connection failed
        setQueryState(QueryState::Inactive);
        emit error(PQerrorMessage(_conn));
        if (_conn)
        {
            PQfinish(_conn);
            _conn = nullptr;
        }
        return;
    }
    _async_stage = async_stage::connecting;
    asyncConnectionProceed();
}

void PgConnection::close() noexcept
{
    _dbmsScriptingID.clear();
    clearResultsets();
    if (!_conn)
        return;

    // stop socket watcher
    watchSocket(SocketWatchMode::None);

    PQfinish(_conn);
    _conn = nullptr;
}

bool PgConnection::isOpened() const noexcept
{
    return _conn;
}

void PgConnection::cancel() noexcept
{
    if (!_conn)
        return;

    setQueryState(QueryState::Cancelling);
    emit message(tr("cancelling..."));

    char errbuf[256];
    PGcancel *cancel = PQgetCancel(_conn);
    if (!PQcancel(cancel, errbuf, sizeof(errbuf)))
        emit message(errbuf);
    PQfreeCancel(cancel);
}

QString PgConnection::context() const noexcept
{
    if (!isOpened())
        return QString();
    QString user = PQuser(_conn);
    QString host = PQhost(_conn);
    QString port = PQport(_conn);
    return (user.isEmpty() ? "" : user + "@") +
            host +
            (port.isEmpty() ? "" : ":" + port + "/" + database());
}

QString PgConnection::database() const noexcept
{
    return _database;
}

QString PgConnection::dbmsInfo() const noexcept
{
    QString res;
    if (!isOpened())
        return res;
    QVector<QString> params {
        //"server_version",
        "server_encoding", "client_encoding",
        "application_name", "is_superuser", "session_authorization",
        "DateStyle", "IntervalStyle", "TimeZone",
        "integer_datetimes", "standard_conforming_strings"};
    QTextStream out(&res);
    out << dbmsName() << " v." << dbmsVersion() << endl << endl;
    for (const QString &p: params)
    {
        const char *val = PQparameterStatus(_conn, p.toStdString().c_str());
        if (!val)
            continue;
        out.setFieldAlignment(QTextStream::AlignLeft);
        out.setFieldWidth(27); // max param name length
        out << p;
        out.setFieldWidth(0);
        out << ": " << val << endl;
    }
    return res;
}

QString PgConnection::dbmsName() const noexcept
{
    return "PostgreSQL";
}

QString PgConnection::dbmsVersion() const noexcept
{
    QString res;
    if (!isOpened())
        return res;
    const char *val = PQparameterStatus(_conn, "server_version");
    return (val ? val : res);
}

QString PgConnection::transactionStatus() const noexcept
{
    switch (PQtransactionStatus(_conn))
    {
    case PQTRANS_ACTIVE:
        return "active";
    case PQTRANS_INTRANS:
        return "intrans";
    case PQTRANS_INERROR:
        return "inerror";
    default:
        return "";
    }
}

int PgConnection::dbmsComparableVersion()
{
    int res = PQserverVersion(_conn);
    // use queries for the the most recent server version in case of error
    return res ? res : 0x7fffffff;
}

std::string PgConnection::finalConnectionString() const noexcept
{
    QString res = "application_name=sqt " + _connection_string;
    if (!_database.isEmpty())
    {
        QString db = _database;
        db.replace(QRegularExpression{"(['\\\\])"}, R"(\\1)");
        res += " dbname='" + db + "'";
    }
    return res.toStdString();
}

bool PgConnection::isUnquotedType(int sqlType) const noexcept
{
    return (sqlType == BOOLOID || isNumericType(sqlType));
}

bool PgConnection::isNumericType(int sqlType) const noexcept
{
    switch (sqlType)
    {
    case INT2OID:
    case INT4OID:
    case INT8OID:
    case OIDOID:
    case TIDOID:
    case XIDOID:
    case CIDOID:
    case FLOAT4OID:
    case FLOAT8OID:
    case NUMERICOID:
        return true;
    }
    return false;
}

QMetaType::Type PgConnection::sqlTypeToVariant(int sqlType) const noexcept
{
    QMetaType::Type var_type;
    switch (sqlType)
    {
    case INT2OID:
    case INT4OID:
        var_type = QMetaType::Int;
        break;
    case OIDOID:
    case REGPROCOID:
    case XIDOID:
    case CIDOID:
        var_type = QMetaType::UInt;
        break;
    case ABSTIMEOID: // absolute, limited-range date and time (Unix system time)
    case INT8OID:
        var_type = QMetaType::LongLong;
        break;
    case FLOAT4OID:
    case FLOAT8OID:
        var_type = QMetaType::Double;
        break;
    case BOOLOID:
        var_type = QMetaType::Bool;
        break;
    case CHAROID:
        var_type = QMetaType::Char;
        break;
    case DATEOID:
        var_type = QMetaType::QDate;
        break;
    case TIMEOID:
        var_type = QMetaType::QTime;
        break;
    case TIMESTAMPOID:
    case TIMESTAMPTZOID:
        var_type = QMetaType::QDateTime;
        break;
    default:
        var_type = QMetaType::QString;
    }
    return var_type;
}

void PgConnection::executeAsync(const QString &query, const QVector<QVariant> *params) noexcept
{
    // save transaction status to avoid reconnects within transaction
    PGTransactionStatusType initial_state = PQtransactionStatus(_conn);
    if (initial_state == PQTRANS_ACTIVE)
    {
        emit error(tr("another command is already in progress"));
        return;
    }

    auto run_query = [this, query, params]()
    {
        bool was_in_transaction = (PQtransactionStatus(_conn) == PQTRANS_INTRANS);
        _async_stage = async_stage::sending_query;
        setQueryState(QueryState::Running);

        if (!query.isEmpty())
        {
            _query_tmp = query;
            _params_tmp.clear();
            if (params)
            {
                for (const QVariant &v: *params)
                    _params_tmp.add(v);
            }
        }

        int async_sent_ok = 0;
        if (_conn)
        {
            async_sent_ok = _params_tmp.count() ?
                        PQsendQueryParams(_conn,
                                          _query_tmp.toStdString().c_str(),
                                          static_cast<int>(_params_tmp.count()),
                                          nullptr,
                                          _params_tmp.values(),
                                          _params_tmp.lengths(),
                                          nullptr,
                                          0) :
                        PQsendQuery(_conn, _query_tmp.toStdString().c_str());

            // TODO make gui option to enable single row mode
            // (slow fetch of large rowset, but prevent partial result
            //  from being discarded on error during fetching)
            if (async_sent_ok)
            {
                //PQsetSingleRowMode(_conn);
            }

            //_last_action_moment = chrono::system_clock::now();
        }

        // disconnected or connection broken => reconnect and try again
        if (PQstatus(_conn) == CONNECTION_BAD)
        {
            emit error(PQerrorMessage(_conn));
            close();
            _async_stage = async_stage::none;
            // do not try to excute the query again if there was an opened transaction
            if (!was_in_transaction)
                openAsync();
            else
                setQueryState(QueryState::Inactive);
            return;
        }

        if (async_sent_ok)
        {
            _async_stage = async_stage::flush;
            int res = PQflush(_conn);
            if (res < 0)    // error
            {
                _async_stage = async_stage::none;
                watchSocket(SocketWatchMode::Read);
            }
            else
            {
                if (!res)
                {
                    _async_stage = async_stage::wait_ready_read;
                    watchSocket(SocketWatchMode::Read);
                }
                else
                {
                    watchSocket(SocketWatchMode::Read | SocketWatchMode::Write);
                }
                return;
            }
        }
        setQueryState(QueryState::Inactive);
        emit error(PQerrorMessage(_conn));
    };

    if (query.isEmpty())
    {
        run_query();
        return;
    }

    QMutexLocker lk(&_resultsetsGuard);
    clearResultsets();
    lk.unlock();

    // Massively data fetching query freezes ui, so we want to run it in
    // separate thread. Asynchronous libpq API is used for the sake of
    // opportunities it provides.
    QThread* thread = new QThread();
    auto state_handler_connection = std::make_shared<QMetaObject::Connection>();
    connect(thread, &QThread::started, [this, run_query, thread, state_handler_connection]() {
        // stop event loop on inactive query state
        *state_handler_connection = connect(this, &PgConnection::queryStateChanged, [this, thread]() {
            if (_query_state == QueryState::Inactive)
            {
                for (auto res: _resultsets)
                    clarifyTableStructure(*res);
                thread->quit();
            }
        });

        run_query();

        if (_query_state == QueryState::Inactive)
            // man: If the eventloop in QThread::exec() is not running then
            // the next call to QThread::exec() will also return immediately.
            // (next to this handler Qt will call exec())
            thread->exit();
    });
    connect(thread, &QThread::finished, [this, thread, state_handler_connection]() {
        thread->deleteLater();
        // disconnect queryStateChanged handler
        disconnect(*state_handler_connection);

        // autorollback
        if (PQtransactionStatus(_conn) == PQTRANS_INERROR)
            PQclear(PQexec(_conn, "rollback"));
    });
    _timer.start();
    thread->start();
}

bool PgConnection::execute(const QString &query, const QVector<QVariant> *params)
{
    // save transaction status to avoid reconnects within transaction
    PGTransactionStatusType initial_state = PQtransactionStatus(_conn);
    if (initial_state == PQTRANS_ACTIVE)
    {
        emit message(tr("another command is already in progress\n"));
        return false;
    }

    bool was_in_transaction = (initial_state == PQTRANS_INTRANS);
    QMutexLocker lk(&_resultsetsGuard);
    clearResultsets();
    lk.unlock();
    _temp_result_rowcount = 0;
    // suspend external socket watcher
    watchSocket(SocketWatchMode::None);

    _params_tmp.clear();
    if (params)
    {
        for (const QVariant &v: *params)
            _params_tmp.add(v);
    }

    _timer.start();
    do
    {
        PGresult *raw_tmp_res = nullptr;
        if (_conn)
        {
            raw_tmp_res = _params_tmp.count() ?
                        PQexecParams(_conn,
                                     query.toStdString().c_str(),
                                     static_cast<int>(_params_tmp.count()),
                                     nullptr,
                                     _params_tmp.values(),
                                     _params_tmp.lengths(),
                                     nullptr,
                                     0) :
                        PQexec(_conn, query.toStdString().c_str());
            //_last_action_moment = chrono::system_clock::now();
        }
        std::unique_ptr<PGresult,decltype(&PQclear)> tmp_res(raw_tmp_res, PQclear);

        // disconnected or connection broken => reconnect and try again
        if (PQstatus(_conn) == CONNECTION_BAD)
        {
            emit error(PQerrorMessage(_conn));
            close();
            if (was_in_transaction || !open())
                return false;
            continue;
        }

        fetchNotifications();

        ExecStatusType status = PQresultStatus(raw_tmp_res);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK)
        {
            // restore watching socket to receive notifications
            watchSocket(SocketWatchMode::Read);
            emit error(PQresultErrorMessage(raw_tmp_res));
            return false;
        }

        // resultset fetched
        if (status == PGRES_TUPLES_OK)
        {
            DataTable *table = new DataTable();
            QMutexLocker lk(&_resultsetsGuard);
            _resultsets.append(table);
            lk.unlock();
            int rows_fetched = appendRawDataToTable(*table, raw_tmp_res);
            if (!rows_fetched || rows_fetched % FETCH_COUNT_NOTIFY != 0)
                emit fetched(table);
        }

        // restore watching socket to receive notifications
        watchSocket(SocketWatchMode::Read);
        break;
    }
    while (true);

    return true;
}

QString PgConnection::escapeIdentifier(const QString &identifier)
{
    QByteArray tmp = identifier.toUtf8();
    std::unique_ptr<char, void(*)(void*)> res(
                PQescapeIdentifier(_conn, tmp.data(), tmp.size()),
                PQfreemem);
    return QString::fromUtf8(res.get());
}

QPair<QString,int> PgConnection::typeInfo(int sqlType)
{
    auto it = _data_types.constFind(sqlType);
    if (it != _data_types.constEnd())
        return it.value();

    QPair<QString,int> tInfo("unknown", -1);
    std::unique_ptr<DbConnection> cn{clone()};
    QVariantList params;
    QString query =
            "select t.oid, t.typname, el.oid "
            "from pg_type t "
            "   left join pg_type el on t.typelem = el.oid ";
    if (!_data_types.empty())
    {
        query += "where t.oid = $1::oid";
        params.append(QVariant(sqlType));
    }

    if (DataTable *res = cn->execute(query, params))
    {
        for (int i = 0; i < res->rowCount(); ++i)
        {
            auto r = res->getRow(i);
            _data_types[r[0].toInt()] = {
                    r[1].toString(),
                    r[2].isValid() ? r[2].toInt() : -1
                };
            if (sqlType == r[0].toInt())
                tInfo = _data_types[sqlType];
        }
    }
    return tInfo;
}

void PgConnection::clarifyTableStructure(DataTable &table)
{
    for (int i = 0; i < table.columnCount(); ++i)
    {
        DataColumn &c = table.getColumn(i);
        int16_t dec_digits = -1;
        int len = -1;
        int fmod = c.modifier();

        auto ti = typeInfo(c.sqlType());
        int sqlType = (ti.second > 0 ? ti.second : c.sqlType());

        if (fmod >= 0)
        {
            switch (sqlType) {
            case NUMERICOID:
                len = (fmod >> 16);
                dec_digits = ((fmod - VARHDRSZ) & 0xffff);
                break;
            case BITOID:
            case VARBITOID:
                len = fmod;
                fmod = -1;
                break;
            default:
                if (fmod >= VARHDRSZ) { // if not?
                    len = fmod - VARHDRSZ;
                    fmod = -1;
                }
            }
        }

        QString typeDescr = ti.first.startsWith('_') ? ti.first.mid(1) : ti.first;
        if (c.modifier() >= 0)
            typeDescr += '(' +
                    QString::number(c.length()) +
                    (c.scale() > 0 ? ',' + QString::number(c.scale()) : "") +
                    ')';
        else if (typeDescr == "char")
            typeDescr = "\"char\"";

        if (ti.second > 0 && ti.first[0] == '_')
            typeDescr += "[]";

        c.clarifyType(typeDescr, len, dec_digits, ti.second);
    }
}

bool PgConnection::isIdle() const noexcept
{
    int status = PQtransactionStatus(_conn);
    return !_conn || status == PQTRANS_IDLE || status == PQTRANS_UNKNOWN;
}

void PgConnection::noticeReceiver(void *arg, const PGresult *res)
{
    PgConnection *cn = static_cast<PgConnection*>(arg);
    QString hint = PQresultErrorField(res, PG_DIAG_MESSAGE_HINT);
    // Postgresql doesn't support procedures, but anonimous code blocks may raise
    // textual notice. This is the way to return plain text result like
    // script or html content from within plpgsql code blocks instead of js.
    if (hint == "script" || hint == "html")
    {
        DataTable *t = new DataTable();
        t->addColumn(hint, QMetaType::QString, TEXTOID, -1, -1, 1, Qt::AlignLeft);
        t->addRow()[0] = QString(PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));
        // synchronous usage only - no need to use _resultsetsGuard
        cn->_resultsets.push_back(t);
    }
    else
        cn->message(PQresultErrorMessage(res));
}

void PgConnection::fetchNotifications()
{
    PGnotify *notify;
    while ((notify = PQnotifies(_conn)) != nullptr)
    {
        std::unique_ptr<PGnotify, void(*)(void*)> n_guard(notify, PQfreemem);
        emit message(tr("* notification received:\n  server process id: %1\n  channel: %2\n  payload: %3").
                     arg(n_guard->be_pid).arg(n_guard->relname).arg(n_guard->extra));
    }
}

void PgConnection::fetch() noexcept
{
    bool is_notification = isIdle();
    do
    {
        //_last_action_moment = chrono::system_clock::now();
        if (!PQconsumeInput(_conn))
        {
            // disconnection detects here
            if (PQstatus(_conn) == CONNECTION_BAD)
            {
                _copy_context.clear();
                watchSocket(SocketWatchMode::None);
                _async_stage = async_stage::none;
                setQueryState(QueryState::Inactive);
            }
            emit error(PQerrorMessage(_conn));
            break;  // incorrect processing?
        }

        if (_async_stage == async_stage::copy_out)
        {
            getCopyData();
            return;
        }

        fetchNotifications();
        if (PQisBusy(_conn) || is_notification)
            break;

        std::unique_ptr<PGresult,decltype(&PQclear)> tmp_res(PQgetResult(_conn), PQclear);

        if (!tmp_res)   // query processing finished
        {
            _copy_context.clear();
            _async_stage = async_stage::none;
            // restore watching socket to receive notifications
            watchSocket(SocketWatchMode::Read);

            setQueryState(QueryState::Inactive);
            break;
        }

        ExecStatusType status = PQresultStatus(tmp_res.get());
        if (status == PGRES_COMMAND_OK)
        {
            char *tuplesAffected = PQcmdTuples(tmp_res.get());
            emit message(*tuplesAffected ?
                             tr("%1 rows affected").arg(tuplesAffected) :
                             tr("statement executed successfully"));
            _temp_result = nullptr;
            continue;
        }

        if (status == PGRES_EMPTY_QUERY)
        {
            emit message(tr("empty query"));
            continue;
        }

        if (status == PGRES_COPY_OUT)
        {
            if (!_copy_context)
                _copy_context.init(_query_tmp);
            if (!_copy_context.nextDestination())
                cancel();
            _async_stage = async_stage::copy_out;
            getCopyData();
            return;
        }

        if (status == PGRES_COPY_IN)
        {
            if (!_copy_context)
                _copy_context.init(_query_tmp);
            if (!_copy_context.nextSource())
                cancel();
            watchSocket(SocketWatchMode::Write);
            _async_stage = async_stage::copy_in;
            _copy_in_buf.resize(0);
            putCopyData();
            return;
        }

        // in case of error the result contains its details,
        // so we want to save it too

        if (!_temp_result)
        {
            // initialize new resultset
            _temp_result = new DataTable();
            QMutexLocker lk(&_resultsetsGuard);
            _resultsets.append(_temp_result);
            lk.unlock();
            _temp_result_rowcount = 0;
            appendRawDataToTable(*_temp_result, tmp_res.get());
        }
        else if (status != PGRES_FATAL_ERROR && PQnfields(tmp_res.get()))
        {
            // append rows to resultset
            appendRawDataToTable(*_temp_result, tmp_res.get());
        }

        // resultset completely fetched
        if (status == PGRES_FATAL_ERROR || status == PGRES_TUPLES_OK)
        {
            // final message if not sent within appendRawDataToTable()
            if ((!_temp_result_rowcount && PQnfields(tmp_res.get())) ||
                    _temp_result_rowcount % FETCH_COUNT_NOTIFY != 0)
                emit fetched(_temp_result);

            if (status == PGRES_FATAL_ERROR) // erroneous resultset
                emit error(PQresultErrorMessage(tmp_res.get()));
            else if (_temp_result->columnCount())
                emit message(tr("%1 rows fetched").arg(_temp_result_rowcount));

            // invalidate intermediate resultset pointer
            if (_temp_result)
            {
                // do not delete - it is in _resultsets already
                _temp_result = nullptr;
            }
        }
    }
    while (true);
}

void PgConnection::asyncConnectionProceed()
{
    PostgresPollingStatusType state = PQconnectPoll(_conn);
    switch (state)
    {
    case PGRES_POLLING_READING:
        watchSocket(SocketWatchMode::Read);
        break;
    case PGRES_POLLING_WRITING:
        watchSocket(SocketWatchMode::Write);
        break;
    case PGRES_POLLING_FAILED:
        // connection failed
        _async_stage = async_stage::none;
        watchSocket(SocketWatchMode::None);
        emit error(PQerrorMessage(_conn));
        // do not release _conn here to avoid error "connection pointer is NULL"
        break;
    default:    // PGRES_POLLING_OK
        // successful connection
        _async_stage = async_stage::none;

        // set notice and warning messages handler
        PQsetNoticeReceiver(_conn, noticeReceiver, this);
        // prevent PQsendQuery to block execution
        PQsetnonblocking(_conn, 1);

        emit message(tr("connection established\n"));

        // connection restored during query execution
        if (queryState() == QueryState::Running)
            executeAsync("");
        else
            watchSocket(SocketWatchMode::None);
    }
}

void PgConnection::getCopyData()
{
    do
    {
        char *buf;
        int len = PQgetCopyData(_conn, &buf, true);
        std::unique_ptr<char, void(*)(void*)> buf_guard(buf, PQfreemem);

        if (len > 0) // row fetched
        {
            if (_query_state != QueryState::Cancelling)
            {
                if (!_copy_context.write(buf, len))
                    cancel();
            }
            continue;
        }

        if (len == -1) // done
        {
            _async_stage = async_stage::wait_ready_read;
            fetch();
        }
        else if (len == -2) // error
        {
            emit error(PQerrorMessage(_conn));
        }

        break;
    }
    while (true);
}

void PgConnection::putCopyData()
{
    do
    {
        // read data if buffer is empty
        // (buffer may stay non-empty if last write opertion failed because of overflowed internal buffer)
        if (    _query_state != QueryState::Cancelling &&
                !_copy_in_buf.size() &&
                !_copy_context.read(_copy_in_buf, 1024 * 512))
        {
            cancel();
            continue;
        }

        if (!_copy_in_buf.size()) // eof
        {
            int end_res = PQputCopyEnd(_conn, nullptr);
            if (end_res >= 0) // error
            {
                if (end_res > 0) // data sent
                    _async_stage = async_stage::flush_copy;
                watchSocket(SocketWatchMode::Write);
            }
            else
            {
                _async_stage = async_stage::wait_ready_read;
                watchSocket(SocketWatchMode::Read);
                emit error(PQerrorMessage(_conn));
            }
            break;
        }

        int res = PQputCopyData(_conn, _copy_in_buf.data(), _copy_in_buf.size());
        if (res > 0)
        {
            _copy_in_buf.resize(0);
            continue;
        }

        if (!res)
            watchSocket(SocketWatchMode::Write);
        else if (res < 0) // error
        {
            _async_stage = async_stage::wait_ready_read;
            watchSocket(SocketWatchMode::Read);
            emit error(PQerrorMessage(_conn));
        }

        break;
    }
    while (true);
}

void PgConnection::readyReadSocket()
{
    switch (_async_stage)
    {
    case async_stage::wait_ready_read:
    case async_stage::copy_out:
    case async_stage::copy_in: // is it possible?
        fetch();
        break;
    case async_stage::connecting:
        asyncConnectionProceed();
        break;
    case async_stage::flush:  // sending query to a server
        if (PQconsumeInput(_conn))
        {
            readyWriteSocket();
            return;
        }
        emit error(PQerrorMessage(_conn));
        _async_stage = async_stage::none;
        watchSocket(SocketWatchMode::Read);
        break;
    default:
        if (isIdle())
            fetch();
    }
}

void PgConnection::readyWriteSocket()
{
    if (_async_stage == async_stage::connecting)
    {
        asyncConnectionProceed();
        return;
    }

    if (_async_stage == async_stage::copy_in)
    {
        watchSocket(SocketWatchMode::Read); // may we get something here?
        putCopyData();
        return;
    }

    if (_async_stage == async_stage::flush || _async_stage == async_stage::flush_copy)
    {
        int res = PQflush(_conn);
        if (res < 0)    // error
            _async_stage = async_stage::none;
        else
        {
            if (!res)
            {
                _async_stage = async_stage::wait_ready_read;
                watchSocket(SocketWatchMode::Read);
            }
            // current mode is rw
            return;
        }
        emit error(PQerrorMessage(_conn));
        _async_stage = async_stage::none;
    }
    watchSocket(SocketWatchMode::Read);
}

void PgConnection::watchSocket(int mode)
{
    int socket_handle = (_conn ? PQsocket(_conn) : -1);
    // force disabling socket watcher in case of incorrect handle
    if (socket_handle == -1)
        mode = SocketWatchMode::None;

    // PQconnectStart may reuse connection freed by previous PQfinish() call (considering object address),
    // and moreover this connection may have the same socket handle, but this is logically another socket
    // (just re-enabling QSocketNotifier doesn't work).
    // So we free listeners to grant the line "sn && sn->socket() != socket_handle" works as expected.
    if (mode == SocketWatchMode::None)
    {
        if (_readNotifier)
            delete _readNotifier;
        _readNotifier = nullptr;

        if (_writeNotifier)
            delete _writeNotifier;
        _writeNotifier = nullptr;
        return;
    }

    auto adjustNotifier = [this, mode, socket_handle](QSocketNotifier::Type type) {
        QSocketNotifier* &sn = (type == QSocketNotifier::Read ?
                                    _readNotifier :
                                    _writeNotifier);
        SocketWatchMode watchMode = (type == QSocketNotifier::Read ?
                                         SocketWatchMode::Read :
                                         SocketWatchMode::Write);
        if (mode & watchMode)
        {
            if (sn && sn->socket() != socket_handle)
            {
                delete sn;
                sn = nullptr;
            }

            if (!sn)
            {
                sn = new QSocketNotifier(socket_handle, type, this);
                connect(sn, &QSocketNotifier::activated, this,
                        type == QSocketNotifier::Read ?
                            &PgConnection::readyReadSocket :
                            &PgConnection::readyWriteSocket);
            }

            sn->setEnabled(true);
        }
        else if (sn)
        {
            sn->setEnabled(false);
        }
    };
    adjustNotifier(QSocketNotifier::Read);
    adjustNotifier(QSocketNotifier::Write);
}

int PgConnection::appendRawDataToTable(DataTable &dst, PGresult *src) noexcept
{
    int dst_columns_count = dst.columnCount();
    int src_columns_count = PQnfields(src);
    int rows_count = PQntuples(src);

    if (!dst_columns_count)
    {
        for (int i = 0; i < src_columns_count; ++i)
        {
            int data_type = PQftype(src, i);
            int fmod = PQfmod(src, i);
            DataColumn *c = new DataColumn(QString::fromUtf8(PQfname(src, i)),
                                           sqlTypeToVariant(data_type),
                                           data_type,
                                           fmod,
                                           1, //nullable, no way to get column-level info
                                           isNumericType(data_type) ?
                                               Qt::AlignRight :
                                               Qt::AlignLeft);
            dst.addColumn(c);
        }
        dst_columns_count = src_columns_count;
    }

    if (dst_columns_count != src_columns_count)
        emit error(tr("source and destiation resultsets do not match"));
    else if (rows_count)
    {
        for (int r = 0; r < rows_count; ++r)
        {
            std::unique_ptr<DataRow> row(new DataRow(&dst));
            for (int i = 0; i < src_columns_count; ++i)
            {
                if (PQgetisnull(src, r, i))
                    continue;
                const char *val = PQgetvalue(src, r, i);
                int type = dst.getColumn(i).sqlType();
                switch (type)
                {
                case INT2OID:
                case INT4OID:
                    (*row)[i] = std::atoi(val);
                    break;
                case INT8OID:
                    (*row)[i] = std::atoll(val);
                    break;
                case FLOAT4OID:
                case FLOAT8OID:
                    (*row)[i] = std::atof(val);
                    break;
                case BOOLOID:
                    (*row)[i] = (val[0] == 't');
                    break;
                case CHAROID:
                    if (!val[0])
                        (*row)[i] = QChar(0);
                    else
                        (*row)[i] = QString::fromStdString(val).at(0);
                    break;

                // QDate is lack of special values support, lack of precision to keep huge dates
                /*
                case DATEOID:
                    (*row)[i] = QDate::fromString(val, Qt::ISODate);
                    break;
                */

                // QTime/QDateTime aren't support microseconds, so qt object as a storage will loose
                // precision. As far as sqt does not interpret values returned from data source,
                // we would prefer to keep their original textual representation.

                /*
                case TIMEOID:
                    (*row)[i] = QTime::fromString(val, Qt::ISODateWithMs);
                    break;
                case TIMESTAMPOID:
                    (*row)[i] = QDateTime::fromString(val, Qt::ISODateWithMs);
                    break;
                */
                // TIMESTAMPTZOID, TIMETZOID goes here untill timezone printing out implemented
                default:
                    (*row)[i] = QString::fromStdString(val);
                }  // end of switch
            }

            QMutexLocker lk(&dst.mutex);
            dst.addRow(row.release());
            lk.unlock();

            ++_temp_result_rowcount;
            if (_temp_result_rowcount % FETCH_COUNT_NOTIFY == 0)
                emit fetched(&dst);
        }
    }
    return rows_count;
}
