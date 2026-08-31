#include "pgconnection.h"
#include "pgtypes.h"
#include "pgtypmod.h"
#include <QApplication>
#include <QVector>
#include <QTextStream>
#include <QSocketNotifier>
#include <QRegularExpression>
#include <QThread>
#include "settings.h"
#include "misc.h"

PgConnection::PgConnection() :
    DbConnection(), _readNotifier(nullptr), _writeNotifier(nullptr), _temp_result(nullptr), _temp_result_rowcount(0)
{
    connect(&_copy_context, &PgCopyContext::error, this, &PgConnection::error);
    connect(&_copy_context, &PgCopyContext::message, this, &PgConnection::message);
}

PgConnection::~PgConnection()
{
    // A query worker captures `this`, so it must be completely stopped before
    // the connection object can disappear. (The socket notifiers belong to this
    // object's own thread - see watchSocket() - and the closing PgConnection::
    // close() below runs in it, so they are destroyed there.)

    if (_queryThread)
    {
        QThread *thread = _queryThread;
        QObject *worker = _queryWorker;
        if (thread->isRunning() && worker && QThread::currentThread() != thread)
        {
            QMetaObject::invokeMethod(worker, [this]() {
                QMutexLocker lk(&_connectionGuard);
                closeLocked();
            }, Qt::BlockingQueuedConnection);
            thread->quit();
            thread->wait();
        }
        else if (thread->isRunning())
        {
            thread->quit();
            thread->wait();
        }
        delete thread;
        _queryThread = nullptr;
        _queryWorker = nullptr;
    }

    // _temp_result is *not* deleted here. It is not an owning pointer: fetch()
    // appends the very same table to _resultsets when it creates it, and the
    // list is what frees it (clearResultsets(), also called by ~DbConnection()).
    // The pointer is only a cursor into that list.
    _temp_result = nullptr;
    PgConnection::close();
}

DbConnection *PgConnection::clone()
{
    PgConnection *res = new PgConnection();
    res->_connection_string = _connection_string;
    res->_database = _database;
    // The clone talks to the same server, so it names the same script bundle -
    // and it has to know that before it is opened, exactly as a connection whose
    // socket has died still knows it (see closeLocked()). Otherwise
    // Scripting::dbmsScriptPath() opens a link just to ask the server who it is,
    // and with the server unreachable it throws instead: no scripts, no
    // highlighting dictionary, an editor tab left with the emergency colouring.
    res->_dbmsScriptingID = _dbmsScriptingID;
    return res;
}

bool PgConnection::open()
{
    //_last_action_moment = chrono::system_clock::now();
    QMutexLocker lk(&_connectionGuard);

    // if current connection is actually broken, the further query will detect it and will try to reconnect
    // (but it may look as ok here)
    if (_conn)
    {
        if (PQstatus(_conn) == CONNECTION_OK)
            return true;
        closeLocked(); // the guard is already held by this very function
    }

    _conn = PQconnectdb(finalConnectionString().c_str());
    if (PQstatus(_conn) != CONNECTION_OK)
    {
        // man: "...a nonempty PQerrorMessage result can consist of multiple lines, and will include a trailing newline.
        // The caller should not free the result directly."
        emit error(PQerrorMessage(_conn));
        PQfinish(_conn);
        _conn = nullptr;
        _opened = false;
        return false;
    }

    _dbmsScriptingID = dbmsName() + dbmsVersionLocked();

    // _database is initially empty within 'connection' node
    // (used to display current context (no need to set it in async method)
    if (_database.isEmpty())
        _database = PQdb(_conn);

    PQsetNoticeReceiver(_conn, noticeReceiver, this);
    PQsetnonblocking(_conn, 1);
    _opened = true;
    watchSocket(SocketWatchMode::Read);
    return true;
}

void PgConnection::openAsync() noexcept
{
    QMutexLocker lk(&_connectionGuard);
    if (_async_stage != async_stage::none || !isIdle())
    {
        const int status = PQtransactionStatus(_conn);
        emit error(tr("unable to open connection (transaction status %1)").arg(status));
        // A run that asked for the reconnect has to end here, or it stays in
        // Reconnecting forever: the tab keeps showing "reconnecting...", the
        // widget's _queryActive flag never goes down, and F5 means "cancel"
        // from then on.
        if (queryState() == QueryState::Reconnecting)
            setQueryState(QueryState::Inactive);
        return;
    }

    // if current connection is actually broken, the further query will detect it and will try to reconnect
    // (but it may looks like ok here)
    if (_conn)
    {
        if (PQstatus(_conn) == CONNECTION_OK) // already connected -
            return;                           // silent exit !!!
        closeLocked(); // the guard is held by this very function
    }

    //time(&_connection_start_moment);
    //_last_try = _connection_start_moment;

    _conn = PQconnectStart(finalConnectionString().c_str());
    if (PQstatus(_conn) == CONNECTION_BAD)
    {
        // connection failed. The reason goes out *before* the Inactive state:
        // that state is what lowers the widget's _queryActive flag, and this is
        // the outcome of the run that asked for the reconnect, so it belongs to
        // the tab's messages pane rather than to the shared log.
        emit error(PQerrorMessage(_conn));
        setQueryState(QueryState::Inactive);
        if (_conn)
        {
            PQfinish(_conn);
            _conn = nullptr;
            _opened = false;
        }
        return;
    }
    _async_stage = async_stage::connecting;
    lk.unlock();
    asyncConnectionProceed();
}

// Releases the link. This is called from the GUI thread (node collapsed,
// Disconnect, a database node left unexpanded) *and* from the query thread
// (a link that dropped mid-query), so it must hold _connectionGuard: two
// unsynchronized calls would both see a non-null _conn and PQfinish() it
// twice, which corrupts the heap and is only noticed much later, usually as
// a wild abort inside malloc during application shutdown.
void PgConnection::close() noexcept
{
    QThread *thread = _queryThread;
    QObject *worker = _queryWorker;
    if (thread && thread->isRunning() && worker && QThread::currentThread() != thread)
    {
        QMetaObject::invokeMethod(worker, [this]() {
            QMutexLocker lk(&_connectionGuard);
            closeLocked();
        }, Qt::BlockingQueuedConnection);
        return;
    }

    QMutexLocker lk(&_connectionGuard);
    closeLocked();
}

void PgConnection::closeLocked() noexcept
{
    // the caller must hold _connectionGuard

    // _dbmsScriptingID is *not* cleared here. It names the script bundle of the
    // server (its name and version), which a closed socket does not change, and
    // an empty one means "unknown" to Scripting: dbmsScriptPath() would open the
    // link again just to ask the server who it is. That is how a database node
    // left collapsed used to get its connection back - the preview pane keeps a
    // shared_ptr to it and asks for hl.conf on every repaint of the content.
    // Only setConnectionString() invalidates the id.

    // The resultsets are *not* dropped here. They belong to whoever asked for
    // them - a tab's models refer to them, and the query thread may still be
    // filling them - while closing the link is a matter of the socket alone
    // and happens behind their back (a dropped connection, a collapsed node).
    // They are released by the next run, which calls clearResultsets() itself.

    watchSocket(SocketWatchMode::None);
    if (!_conn)
    {
        _opened = false;
        return;
    }

    PQfinish(_conn);
    _conn = nullptr;
    _opened = false;
}

bool PgConnection::isOpened() const noexcept
{
    // Whether the link is alive: libpq keeps the handle after it has died, so
    // the pointer alone says nothing (the tree indicator and the connections
    // menu tell "alive" from "registered but broken" by this very function).
    //
    // Deliberately lock-free: this is called from the tree's paint routine, and
    // also from context()/dbmsInfo() below, which already hold the guard. So it
    // reads a flag maintained under the lock instead of dereferencing _conn -
    // touching a PGconn that another thread is inside PQfinish() for would be a
    // use-after-free, and locking here would deadlock.
    return _opened;
}

void PgConnection::cancel() noexcept
{
    QMutexLocker lk(&_connectionGuard);
    if (!_conn || queryState() == QueryState::Inactive)
        return;

    // the reason to call cancel() again is to detect broken connection and close/deallocate it
    // 1) executeAsync()
    // 2) cancel()
    // 3) conection is boken (link disappeared)
    // 4) server unable to notify client of cancellation
    // 5) => query may execute forever
    if (queryState() == QueryState::Cancelling || QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
    {
        emit closeConnectionWanted();
        return;
    }

    // a connection has no cancellation key until it is established, so there is
    // nothing to cancel yet and dropping it is the only way to end such a run
    std::unique_ptr<PGcancel,decltype(&PQfreeCancel)> cancel(PQgetCancel(_conn), PQfreeCancel);
    if (_async_stage == async_stage::connecting || !cancel)
    {
        lk.unlock();
        emit closeConnectionWanted();
        return;
    }

    char errbuf[256];
    int cancelResult = PQcancel(cancel.get(), errbuf, sizeof(errbuf));
    lk.unlock();
    if (cancelResult)
    {
        emit message(tr("cancelling..."));
        setQueryState(QueryState::Cancelling);
    }
    else
    {
        emit error(errbuf);
    }
}

QString PgConnection::context() const noexcept
{
    QMutexLocker lk(&_connectionGuard);
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
    auto _endl =
        #if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
            Qt::endl;
        #else
            endl;
        #endif
    QMutexLocker lk(&_connectionGuard);
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
    out << dbmsName() << " v." << dbmsVersionLocked() << _endl << _endl;
    for (const QString &p: params)
    {
        const char *val = PQparameterStatus(_conn, p.toStdString().c_str());
        if (!val)
            continue;
        out.setFieldAlignment(QTextStream::AlignLeft);
        out.setFieldWidth(27); // max param name length
        out << p;
        out.setFieldWidth(0);
        out << ": " << val << _endl;
    }
    return res;
}

QString PgConnection::dbmsName() const noexcept
{
    // synchronous usage only
    return "PostgreSQL";
}

QString PgConnection::dbmsVersion() const noexcept
{
    QMutexLocker lk(&_connectionGuard);
    if (!isOpened())
        return QString();
    return dbmsVersionLocked();
}

QString PgConnection::dbmsVersionLocked() const noexcept
{
    const char *val = PQparameterStatus(_conn, "server_version");
    return val ? QString::fromUtf8(val) : QString();
}

QString PgConnection::transactionStatus() const noexcept
{
    QMutexLocker lk(&_connectionGuard);
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
    QMutexLocker lk(&_connectionGuard);
    if (!_conn)
        return 0x7fffffff;
    const int res = PQserverVersion(_conn);
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
bool PgConnection::executeAsync(const QString &query, const QVector<QVariant> *params) noexcept
{
    // an empty query is not a new request, but the resend of the current one
    // (see asyncConnectionProceed()), so it must pass the check below
    if (!query.isEmpty())
    {
        // save transaction status to avoid reconnects within transaction
        QMutexLocker lk(&_connectionGuard);
        PGTransactionStatusType initial_state = PQtransactionStatus(_conn);
        // refuse a new query only if the current one is still in flight
        // (an opened transaction does not block anything);
        // the transaction status is unknown while reconnecting, hence the
        // query state check
        if (initial_state == PQTRANS_ACTIVE || queryState() != QueryState::Inactive)
        {
            emit error(tr("another command is already in progress"));
            return false;
        }
    }

    // run_query() is executed in another thread, so the caller's params must
    // not be referenced from there: they may be long gone by then
    const QVector<QVariant> params_copy = (params ? *params : QVector<QVariant>());
    auto run_query = [this, query, params_copy]()
    {
        QMutexLocker lk(&_connectionGuard);
        bool was_in_transaction = (PQtransactionStatus(_conn) == PQTRANS_INTRANS);
        _async_stage = async_stage::sending_query;
        // the query starts here, so the preceding reconnect (if any) is not a
        // part of its execution time
        _timer.start();
        setQueryState(QueryState::Running);

        if (!query.isEmpty())
        {
            _query_tmp = query;
            _params_tmp.clear();
            for (const QVariant &v: params_copy)
                _params_tmp.add(v);
            // a new run, so it has its own right to a single silent resend
            _resent_once = false;
        }
        // Nothing of this attempt has reached the socket yet. A parameterized
        // query counts as delivered from the outset: see _query_flushed - the
        // extended protocol gives no guarantee to lean on.
        _query_flushed = (_params_tmp.count() > 0);

        int async_sent_ok = 0;
        if (_conn)
        {
            // Read whatever the link has to say before the query is handed to
            // libpq. A server that has ended the session (a restart, an
            // administrator command, an idle timeout) has normally sent its
            // FATAL and its FIN long ago, but nothing on an idle link reads
            // them: PQstatus() still says OK, the query goes into the buffer,
            // and the kernel accepts it happily - a half-closed socket is still
            // writable. The loss then surfaces on the *read* that follows, where
            // a query already on the wire is indistinguishable from one the
            // server never saw, and the user is left to check by hand what the
            // run did. Collecting the news first lets the branch below state
            // with certainty that the query was not delivered, and simply send
            // it again on a fresh link.
            //
            // More than one read is needed: the first one returns the bytes that
            // were waiting (the FATAL) and reports success, and only the next
            // one runs into the end of the stream and marks the connection bad.
            // PQconsumeInput() never blocks, so a couple of extra calls on a
            // healthy link cost one syscall each and change nothing.
            for (int i = 0; i < 3 && PQstatus(_conn) == CONNECTION_OK; ++i)
            {
                if (!PQconsumeInput(_conn))
                    break;
            }

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

            // Single row mode prevents resultset from being discarded on error during fetching.
            if (async_sent_ok && SqtSettings::value("pgSingleRowMode", false).toBool())
                PQsetSingleRowMode(_conn);

            //_last_action_moment = chrono::system_clock::now();
        }

        // The link was already dead when the query was handed to libpq, so the
        // server has not seen it and cannot have executed it: reconnect and
        // send it again. This is the state a connection is left in by any
        // earlier loss, which is what makes the retry a single keypress.
        if (PQstatus(_conn) == CONNECTION_BAD)
        {
            // the query is not sent yet, so the messages to follow belong to the
            // connection being restored
            if (!was_in_transaction)
                setQueryState(QueryState::Reconnecting);

            // libpq's own wording ("no connection to the server") reads as a
            // failure of the run, while the query is merely about to be sent
            // again; an open transaction, though, has really been lost.
            // The announcement is made even when the handle is already gone
            // (a loss noticed while idle releases it), or the pane would show
            // "connection established" out of nowhere.
            if (was_in_transaction)
            {
                if (_conn) // to avoid "connection pointer is NULL"
                    emit error(PQerrorMessage(_conn));
            }
            else
                emit message(tr("connection lost, reconnecting..."));

            if (_conn)
            {
                closeLocked(); // the guard is held right here
                emit connectionLost();
            }

            lk.unlock();
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
            if (res >= 0)
            {
                if (!res)
                {
                    // the query has left libpq's buffer whole - from here on it
                    // may have been executed, see _query_flushed
                    _query_flushed = true;
                    _async_stage = async_stage::wait_ready_read;
                    watchSocket(SocketWatchMode::Read);
                }
                else
                {
                    watchSocket(SocketWatchMode::Read | SocketWatchMode::Write);
                }
                return;
            }

            // the write failed, so the query is incomplete and the server
            // cannot have acted on it: this normally reconnects and resends
            const QString err = PQerrorMessage(_conn);
            const bool resend = linkLostMidQuery(err);
            lk.unlock();
            if (resend)
                openAsync();
            return;
        }
        // libpq refused the query itself (out of memory, wrong state) on a link
        // that is still alive: that refusal is the result of the run, hence
        // reported before the Inactive state that ends it
        emit error(PQerrorMessage(_conn));
        setQueryState(QueryState::Inactive);
    };

    if (query.isEmpty())
    {
        run_query();
        return true;
    }

    clearResultsets();
    // A half-built resultset left by a link that died mid-fetch is gone with
    // the list above, and the cursor into it must not survive: fetch() takes a
    // non-null _temp_result for "keep appending to this table" and would write
    // into freed memory.
    _temp_result = nullptr;
    _temp_result_rowcount = 0;
    // delete listeners before switch to another thread
    watchSocket(SocketWatchMode::None);

    // Massively data fetching query freezes ui, so we want to run it in
    // separate thread. Asynchronous libpq API is used for the sake of
    // opportunities it provides.
    QThread* thread = new QThread();
    QObject *worker = new QObject();
    worker->moveToThread(thread);
    _queryThread = thread;
    _queryWorker = worker;

    connect(thread, &QThread::started, worker, [this, run_query, thread, worker]() {
        connect(this, &PgConnection::closeConnectionWanted, worker, [this]() {
            QMutexLocker lk(&_connectionGuard);
            if (!_conn)
                return;
            closeLocked();
            emit error(tr("connection closed"));
            setQueryState(QueryState::Inactive);
        }, Qt::QueuedConnection);

        connect(this, &PgConnection::queryStateChanged, worker, [this, thread](QueryState state) {
            if (state != QueryState::Inactive)
                return;
            {
                QMutexLocker lkres(&_resultsetsGuard);
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
                for (auto res: qAsConst(_resultsets))
#else
                for (auto res: std::as_const(_resultsets))
#endif
                    clarifyTableStructure(*res);
            }
            QMutexLocker lk(&_connectionGuard);
            watchSocket(SocketWatchMode::None);
            thread->quit();
        }, Qt::QueuedConnection);

        run_query();
        if (_query_state == QueryState::Inactive)
            thread->exit();
    });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (_queryThread != thread)
            return;
        _queryThread = nullptr;
        _queryWorker = nullptr;
        QMutexLocker lk(&_connectionGuard);
        if (_conn && PQtransactionStatus(_conn) == PQTRANS_INERROR)
            PQclear(PQexec(_conn, "rollback"));
        watchSocket(SocketWatchMode::Read);
        emit queryFinished();
        thread->deleteLater();
    });
    thread->start();
    return true;
}

bool PgConnection::execute(const QString &query, const QVector<QVariant> *params)
{
    // a lost link is worth one silent retry, no more (see below)
    bool reconnected = false;
    // save transaction status to avoid reconnects within transaction
    QMutexLocker connection_lk(&_connectionGuard);
    PGTransactionStatusType initial_state = PQtransactionStatus(_conn);
    connection_lk.unlock();
    if (initial_state == PQTRANS_ACTIVE)
    {
        emit message(tr("another command is already in progress\n"));
        return false;
    }

    bool was_in_transaction = (initial_state == PQTRANS_INTRANS);
    clearResultsets();
    // the cursor points into the list that has just been freed (it is left
    // behind by a link that died mid-resultset), so it has to go with it
    _temp_result = nullptr;
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

        // The query never reached the server, so nothing was executed and one
        // more attempt on a fresh link is all this takes. The caller here is
        // the object tree, F4 or the preview pane, none of which has a
        // messages pane of its own, so a reconnect stays silent: only a
        // genuine failure (reported by open()) reaches the log.
        if (PQstatus(_conn) == CONNECTION_BAD)
        {
            // ... except when an open transaction has been lost with the link:
            // that changes what the following statements would mean
            if (was_in_transaction && _conn)
                emit error(PQerrorMessage(_conn));
            if (_conn)
            {
                close();
                emit connectionLost();
            }
            if (was_in_transaction || reconnected || !open())
                return false;
            reconnected = true;
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
    QMutexLocker lk(&_connectionGuard);
    if (!_conn)
        return identifier; // same fallback as the DbConnection base default

    QByteArray tmp = identifier.toUtf8();
    std::unique_ptr<char, void(*)(void*)> res(
                PQescapeIdentifier(_conn, tmp.data(), tmp.size()),
                PQfreemem);
    if (!res)
        return QString();
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
        auto ti = typeInfo(c.sqlType());

        // Only a name starting with '_' means an array: point, int2vector and
        // the like have a typelem of their own without being arrays, and their
        // modifier belongs to the type itself, not to the element type.
        const bool isArray = (ti.first.startsWith('_') && ti.second > 0);
        // an array's modifier describes its elements
        const int sqlType = (isArray ? ti.second : c.sqlType());

        // decode first - the description below is built out of the result, and
        // the column itself still holds the -1 defaults at this point
        const PgTypmod tm = pgDecodeTypmod(sqlType, c.modifier());

        QString typeDescr = (isArray ? ti.first.mid(1) : ti.first);
        if (typeDescr == "char" && tm.suffix.isEmpty())
            typeDescr = "\"char\"";
        typeDescr += tm.suffix;
        if (isArray)
            typeDescr += "[]";

        c.clarifyType(typeDescr, tm.length, tm.scale, ti.second);
    }
}

bool PgConnection::isIdle() const noexcept
{
    // the caller must lock _connectionGuard when needed
    int status = PQtransactionStatus(_conn);
    return !_conn || status == PQTRANS_IDLE || status == PQTRANS_UNKNOWN;
}

bool PgConnection::hasLink() const noexcept
{
    // Whether there is a handle at all - unlike isOpened(), which tells whether
    // the link behind the handle is alive. A dead but still held handle is worth
    // reading from (that is how a query gets its error), a released one is not.
    QMutexLocker lk(&_connectionGuard);
    return _conn != nullptr;
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
        t->addColumn(new DataColumn(hint, "", QMetaType::QString, TEXTOID, -1, -1, 1, Qt::AlignLeft));
        t->addRow()[0] = QString(PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY));
        // libpq calls this back from whichever thread is talking to the server,
        // and that is the query thread for an asynchronous run, so the list has
        // to be guarded here as everywhere else
        QMutexLocker lk(&cn->_resultsetsGuard);
        cn->_resultsets.push_back(t);
    }
    else
        emit cn->message(PQresultErrorMessage(res));
}

void PgConnection::fetchNotifications()
{
    // the caller must lock _connectionGuard when needed
    PGnotify *notify;
    while ((notify = PQnotifies(_conn)))
    {
        std::unique_ptr<PGnotify, void(*)(void*)> n_guard(notify, PQfreemem);
        emit message(tr("* notification received:\n  server process id: %1\n  channel: %2\n  payload: %3").
                     arg(n_guard->be_pid).arg(n_guard->relname, n_guard->extra));
    }
}

bool PgConnection::linkLostMidQuery(const QString &libpqError)
{
    // the caller holds _connectionGuard

    // A query that never left libpq's buffer in full cannot have been executed
    // (see _query_flushed), so the link is worth restoring and the query worth
    // sending again - exactly what the user would do by hand, minus the
    // guesswork. A COPY is excluded: it has a local file half-written or
    // half-read behind it, and an open transaction is lost with the link, which
    // changes what the query would mean on a fresh session.
    const bool resendable = (!_query_flushed && !_copy_context && !_resent_once &&
                             PQtransactionStatus(_conn) != PQTRANS_INTRANS);

    _copy_context.clear();
    // the notifiers would keep firing on a dead descriptor
    watchSocket(SocketWatchMode::None);
    _async_stage = async_stage::none;
    // the link is gone whether or not the handle is kept below, and isOpened()
    // (the tree indicator) reads this flag
    _opened = false;

    if (resendable)
    {
        _resent_once = true;
        // the run continues, so it stays away from Inactive: the query is about
        // to be sent again on a fresh link
        setQueryState(QueryState::Reconnecting);
        emit message(tr("connection lost before the query was sent, reconnecting..."));
        closeLocked(); // the guard is held by the caller
        emit connectionLost();
        return true;
    }

    // The query had already been delivered, so the server may well have executed
    // it - possibly committed it - and sqt has no way to tell. Resending is out
    // of the question here: this is the result of the run, and it belongs to the
    // tab's messages pane, hence emitted *before* the Inactive state, which is
    // what lowers the widget's _queryActive flag. The dead handle is kept (its
    // CONNECTION_BAD status is what paints the tree indicator red, and the
    // pending resultsets stay alive until the next run replaces them); the next
    // query reopens the link.
    if (!libpqError.isEmpty())
        emit error(libpqError);
    emit error(tr("The query had been sent when the connection dropped, so it is\n"
                  "unknown whether the server executed it. If it modifies data,\n"
                  "check the effect of this run before repeating it."));
    // tells the widget to report the run as interrupted rather than done
    emit outcomeUnknown();
    setQueryState(QueryState::Inactive);
    emit connectionLost();
    return false;
}

void PgConnection::fetch() noexcept
{
    _connectionGuard.lock();

    // Nobody waits for a result on an idle link: whatever arrives on it is a
    // notification, and whatever happens to it is not the output of a query.
    // The transaction status alone cannot tell the two apart - libpq reports
    // it as "unknown" once the link is broken, which looks exactly like idle -
    // hence the query state as well.
    bool is_notification = (isIdle() && queryState() == QueryState::Inactive);
    _connectionGuard.unlock();
    do
    {
        QMutexLocker lk(&_connectionGuard);
        //_last_action_moment = chrono::system_clock::now();
        if (!PQconsumeInput(_conn))
        {
            // disconnection detects here
            if (PQstatus(_conn) == CONNECTION_BAD)
            {
                QString err = PQerrorMessage(_conn);

                // An idle link that drops costs the user nothing and needs no
                // announcement: release the handle so that the tree indicator
                // turns red, and let the next query restore the connection.
                if (is_notification)
                {
                    _copy_context.clear();
                    // the notifiers would keep firing on a dead descriptor
                    watchSocket(SocketWatchMode::None);
                    _async_stage = async_stage::none;
                    _opened = false;
                    setQueryState(QueryState::Inactive);
                    lk.unlock();
                    close();
                    emit connectionLost();
                    return;
                }

                // a query of ours was in flight: either it is provably safe to
                // send again, or the user has to be told what is unknown
                const bool resend = linkLostMidQuery(err);
                lk.unlock();
                if (resend)
                    openAsync(); // sends the query again once connected
                return;
            }

            emit error(PQerrorMessage(_conn));
            break;  // incorrect processing?
        }

        if (_async_stage == async_stage::copy_out)
        {
            lk.unlock();
            getCopyData();
            return;
        }

        if (PQisBusy(_conn) || is_notification)
        {
            fetchNotifications();
            break;
        }

        std::unique_ptr<PGresult,decltype(&PQclear)> tmp_res(PQgetResult(_conn), PQclear);
        fetchNotifications();
        lk.unlock();

        if (!tmp_res)   // query processing finished
        {
            _copy_context.clear();
            _async_stage = async_stage::none;
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
            QMutexLocker lkres(&_resultsetsGuard);
            _resultsets.append(_temp_result);
            lkres.unlock();
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
                emit error(tr("SQLSTATE: %1\n%2")
                           .arg(PQresultErrorField(tmp_res.get(), PG_DIAG_SQLSTATE))
                           .arg(PQresultErrorMessage(tmp_res.get())));
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
    QMutexLocker lk(&_connectionGuard);
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
        _opened = false;
        watchSocket(SocketWatchMode::None);
        emit error(PQerrorMessage(_conn));
        setQueryState(QueryState::Inactive);
        // do not release _conn here to avoid error "connection pointer is NULL"
        break;
    default:    // PGRES_POLLING_OK
        // successful connection
        _async_stage = async_stage::none;

        // set notice and warning messages handler
        PQsetNoticeReceiver(_conn, noticeReceiver, this);
        // prevent PQsendQuery to block execution
        PQsetnonblocking(_conn, 1);

        // the link is alive from here on - see isOpened()
        _opened = true;

        emit message(tr("connection established\n"));

        // connection restored during query execution
        if (queryState() == QueryState::Reconnecting)
        {
            lk.unlock();
            executeAsync("");
        }
        else
            watchSocket(SocketWatchMode::None);
    }
}

void PgConnection::getCopyData()
{
    do
    {
        char *buf;
        QMutexLocker lk(&_connectionGuard);
        int len = PQgetCopyData(_conn, &buf, true);
        lk.unlock();
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
            lk.relock();
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

        QMutexLocker lk(&_connectionGuard);
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

        int res = PQputCopyData(_conn, _copy_in_buf.data(), int(_copy_in_buf.size()));
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
    // The link may be gone already: it is released by whichever thread notices
    // the loss (the query worker, more often than not), while the notifiers are
    // switched off by a queued call to this thread - see watchSocket() - so one
    // last activation can still arrive here, on a descriptor that is no longer
    // ours. Nothing is to be read from it, and libpq would merely answer
    // "connection pointer is NULL", which is not a message about the query.
    if (!hasLink())
        return;

    switch (_async_stage)
    {
    case async_stage::connecting:
        asyncConnectionProceed();
        break;
    case async_stage::flush:  // sending query to a server
    {
        QMutexLocker lk(&_connectionGuard);
        if (PQconsumeInput(_conn))
        {
            lk.unlock();
            readyWriteSocket();
            return;
        }

        const QString err = PQerrorMessage(_conn);
        if (PQstatus(_conn) == CONNECTION_BAD)
        {
            // the query was still on its way out, so it is normally resent
            const bool resend = linkLostMidQuery(err);
            lk.unlock();
            if (resend)
                openAsync();
            return;
        }

        // the link is alive, so this is the outcome of the run and belongs to
        // the pane: reported before the Inactive state that ends it
        emit error(err);
        setQueryState(QueryState::Inactive);
        _async_stage = async_stage::none;
        watchSocket(SocketWatchMode::Read);
        break;
    }

    default:
        fetch();
        break;
    }
}

void PgConnection::readyWriteSocket()
{
    // the link may already be released - see readyReadSocket()
    if (!hasLink())
        return;

    if (_async_stage == async_stage::connecting)
    {
        asyncConnectionProceed();
        return;
    }

    QMutexLocker lk(&_connectionGuard);
    if (_async_stage == async_stage::copy_in)
    {
        watchSocket(SocketWatchMode::Read); // may we get something here?
        lk.unlock();
        putCopyData();
        return;
    }

    if (_async_stage == async_stage::flush || _async_stage == async_stage::flush_copy)
    {
        const bool query_flush = (_async_stage == async_stage::flush);
        int res = PQflush(_conn);
        if (res >= 0)
        {
            if (!res)
            {
                // the query is out of libpq's buffer whole - past this point it
                // may have been executed, see _query_flushed
                if (query_flush)
                    _query_flushed = true;
                _async_stage = async_stage::wait_ready_read;
                watchSocket(SocketWatchMode::Read);
            }
            // current mode is rw
            return;
        }

        // The write failed, so the run is over either way. Sending the query
        // again is normally safe here - its last bytes never made it out - and
        // the helper ends the run when it is not.
        const QString err = PQerrorMessage(_conn);
        const bool resend = linkLostMidQuery(err);
        lk.unlock();
        if (resend)
            openAsync();
        return;
    }

    watchSocket(SocketWatchMode::Read);
}

void PgConnection::watchSocket(int mode)
{
    // A QSocketNotifier is registered with the event dispatcher of the thread
    // it was created in, and only that thread may enable, disable or delete it.
    // Doing it from another thread corrupts the dispatcher's own bookkeeping,
    // and the damage is not noticed where it is done: it surfaces much later as
    // a wild abort inside some unrelated malloc/free - typically while a static
    // container is being destroyed after main() has returned. Qt says as much
    // beforehand ("Socket notifiers cannot be enabled or disabled from another
    // thread"), and the "Invalid socket N and type 'Read', disabling..." storm
    // that follows is the same wound: a notifier left watching a descriptor
    // that another thread has already closed.
    //
    // The handlers are readyReadSocket()/readyWriteSocket(), slots of this
    // object, so they run in the thread this object belongs to no matter which
    // thread the notifier fired in. That thread is therefore the one that owns
    // the notifiers, and a call from the query worker is forwarded to it.
    //
    // The forwarding must not block: the worker calls watchSocket() while
    // holding _connectionGuard (see run_query()), and the owner thread may be
    // waiting for that very mutex, so a blocking call would deadlock the pair.
    // It is queued instead, and the queued call takes the guard itself - the
    // same-thread path keeps the old contract, where the caller holds it.
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(this, [this, mode]() {
            QMutexLocker lk(&_connectionGuard);
            watchSocket(mode);
        }, Qt::QueuedConnection);
        return;
    }

    // the caller must lock _connectionGuard when needed
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
                sn = new QSocketNotifier(socket_handle, type);
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
                    // Not atof()/strtod(): those follow the current locale, and
                    // QApplication sets it from the system - so where LC_NUMERIC
                    // uses a comma, "1.5" off the wire silently became 1. The
                    // server always prints '.' (and "NaN"/"Infinity"), so the
                    // parsing must not depend on a locale at all. See misc.h.
                    (*row)[i] = parseDouble(val);
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
