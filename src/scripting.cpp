#include "scripting.h"
#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <QTextStream>
#include <QRegularExpression>
#include "dbconnection.h"
#include "odbcconnection.h"
#include "datatable.h"
#include <QJSEngine>
#include <QJSValueList>
#include <QQmlEngine>

namespace Scripting
{

// key = dbms_scripting_id, value = root scripts path
static QHash<QString, QString> _dbms_paths;
// key = dbms_scripting_id/context/, value = { type, script }
static QHash<QString, QHash<QString, Script>> _scripts;

QString context2str(Context context)
{
    switch (context) {
    case Context::Tree:
        return "tree";
    case Context::Content:
        return "content";
    case Context::Preview:
        return "preview";
    case Context::Autocomplete:
        return "autocomplete";
    default: // root
        return "";
    }
}

QString dbmsScriptPath(DbConnection *con, Context context)
{
    if (!con || (con->dbmsScriptingID().isEmpty() && !con->open()))
        throw QObject::tr("db connection unavailable");
    OdbcConnection *odbcConnection = qobject_cast<OdbcConnection*>(con);

    QString contextFolder = context2str(context);
    if (!contextFolder.isEmpty())
        contextFolder += '/';

    const auto it = _dbms_paths.find(con->dbmsScriptingID());
    if (it != _dbms_paths.end())
        return it.value() + contextFolder;

    QString startPath = QApplication::applicationDirPath() + "/scripts/";
    if (odbcConnection)
        startPath += "odbc/";

    QDir dir(startPath);
    if (!dir.exists())
        throw QObject::tr("directory %1 does not exist").arg(startPath);

    QStringList subdirs = dir.entryList(QStringList(), QDir::AllDirs | QDir::NoDotAndDotDot);
    QString dbmsName = con->dbmsName();
    if (dbmsName.isEmpty())
        throw QObject::tr("unable to get dbms name");

    QString endPath;
    // search for the folder with a name containing dbms name
    for (const QString &d : subdirs)
    {
        if (dbmsName.contains(d, Qt::CaseInsensitive))
        {
            endPath = d + "/";
            break;
        }
    }

    // if specific folder was not found for odbc driver
    if (endPath.isEmpty() && odbcConnection)
        startPath += "default/";
    else
        startPath += endPath;

    if (!QFile::exists(startPath + contextFolder))
        throw QObject::tr("directory %1 is not available").arg(startPath + contextFolder);
    _dbms_paths.insert(con->dbmsScriptingID(), startPath);
    return startPath + contextFolder;
}

///
/// \brief dbms The function extracts version specific part of script or entire content.
/// \param script Content of script file.
/// \param version Current dbms comparable version (or db-level compartibility level).
/// \return dbms Version specific part of script.
///
/// A script may contain comments corresponding to regexp:
/// \/\*\s*(if|elif|else|endif)\s+version\s*(\d+)?\s*\*\/
/// For example:
/// /* if version 100000 */
/// select s.datname, s.pid, s.backend_type, s.usename --, ...
/// from pg_stat_activity
///
/// /* elif version 90600 */
/// select s.datname, s.pid, s.usename --, ...
/// from pg_stat_activity s
/// /* endif version */
///
/// Such boundaries split a script into parts acording to dbms minimal version.
/// Nesting is ok, order matters.
/// PostgreSQL uses libpq's PQserverVersion(), ODBC data sources must provide
/// version.sql or version.qs to return this value if used within scripts.
/// E.g. scripts/odbc/microsoft sql/version.sql
/// (uses compartibility_level as a comparable version).
///
QString versionSpecificPart(const QString &script, int version)
{

/* if version 90000 */
/* elif version 80000 */
/* else version */
/* endif version */

    QRegularExpression re(R"(\/\*\s*(if|elif|else|endif)\s+version\s*(\d+)?\s*\*\/)");

    //QRegularExpression re(R"((?=\/\*\s*V(\d+)\+\s*\*\/))");
    QRegularExpressionMatchIterator i = re.globalMatch(script);

    // return whole script body if boundaries are not found
    if (!i.hasNext())
        return script;

    QString res;
    res.reserve(script.size());
	int pos = 0;
	int level = 0;
    auto decrement_level = [&level]()
    {
        --level;
        if (level < 0)
            throw QObject::tr("invalid dbms version boundaries");
    };

    std::function<void()> fill = [&]()
    {
        while (i.hasNext())
        {
            int run_level = level;
            QRegularExpressionMatch match = i.next();
            QString cond = match.captured(1);
            res.append(script.midRef(pos, match.capturedStart() - pos));
            if (cond == "if")
            {
                ++level;
                int block_ver = match.captured(2).toInt();
                if (version >= block_ver)
                {
                    // ok
                    pos = match.capturedEnd();
                    fill(); // to process nested ifs
                }
                else
                {
                    // search for suitable sibling
                    while (i.hasNext())
                    {
                        QRegularExpressionMatch match = i.next();
                        QString cond = match.captured(1);
                        if (cond == "if")
                            ++level;
                        else if (cond == "endif")
                        {
                            decrement_level();
                            pos = match.capturedEnd();
                            if (level < run_level) // suitable sibling is not found
                                return;
                        }
                        else if (level == run_level)
                        {
                            if (cond == "else" || version >= match.captured(2).toInt())
                            {
                                // ok
                                pos = match.capturedEnd();
                                fill(); // to process nested ifs
                                break;
                            }
                        }
                    }
                }
            }
            else if (cond == "endif")
            {
                decrement_level();
                pos = match.capturedEnd();
                return;
            }
            else
            {
                // block filled - need to skip the rest
                while (i.hasNext())
                {
                    QRegularExpressionMatch match = i.next();
                    QString cond = match.captured(1);
                    if (cond == "if")
                        ++level;
                    else if (cond == "endif")
                        decrement_level();

                    if (level < run_level)
                    {
                        pos = match.capturedEnd();
                        return;
                    }
                }
            }
        }
        res.append(script.midRef(pos));
	};
	fill();
    if (level)
        throw QObject::tr("invalid dbms version boundaries");
    return res;
}

void refresh(DbConnection *connection, Context context)
{
    if (!connection)
        return;

    QString path = dbmsScriptPath(connection, context);
    auto &bunch = _scripts[connection->dbmsScriptingID() + context2str(context)];
    bunch.clear();

    QFileInfoList files = QDir(path).entryInfoList({"*.*"}, QDir::Files);
    for (const auto &f : files)
    {
        QString suffix = f.suffix().toLower();
        if (suffix != "sql" && suffix != "qs")
            continue;

        QFile scriptFile(f.filePath());
        if (!scriptFile.open(QIODevice::ReadOnly))
            throw QObject::tr("can't open %1").arg(files.at(0).filePath());

        QTextStream stream(&scriptFile);
        stream.setCodec("UTF-8");

        bunch.insert(
            f.baseName(),
            Scripting::Script {
                versionSpecificPart(
                    stream.readAll(),
                    // prevent infinite loop - do not acquire comparable version on root level
                    context == Context::Root ? -1 : connection->dbmsComparableVersion()),
                suffix == "sql" ? Script::Type::SQL : Script::Type::QS
            });
    }
}

Script* getScript(DbConnection *connection, Context context, const QString &objectType)
{
    auto &bunch = _scripts[connection->dbmsScriptingID() + context2str(context)];
    if (bunch.isEmpty())
    {
        refresh(connection, context);
        bunch = _scripts[connection->dbmsScriptingID() + context2str(context)];
    }
    const auto it = bunch.find(objectType);
    return (it == bunch.end() ? nullptr : &it.value());
}

void execute(
        CppConductor *env,
        DbConnection *connection,
        Script *s)
{
    QString query = s->body;

    // replace macroses with corresponding values in both sql and qs scripts
    QRegularExpression expr("\\$(\\w+\\.\\w+)\\$");
    QRegularExpressionMatchIterator i = expr.globalMatch(query);
    QStringList macros;
    // search for macroses within query text
    while (i.hasNext())
    {
        QRegularExpressionMatch match = i.next();
        if (!macros.contains(match.captured(1)))
            macros << match.captured(1);
    }
    // replace macroses with values
    for (const QString &macro: qAsConst(macros))
    {
        QString value = (macro == "dbms.version" ?
                             QString::number(connection->dbmsComparableVersion()) :
                             env->value(macro).toString());
        query = query.replace("$" + macro + "$", value.isEmpty() ? "NULL" : value);
    }

    if (s->type == Scripting::Script::Type::SQL)
    {
        connection->execute(query);
        for (int i = connection->_resultsets.size() - 1; i >= 0; --i)
        {
            DataTable *t = connection->_resultsets.at(i);
            connection->_resultsets.removeAt(i);
            if (t->rowCount() == 1 && t->columnCount() == 1)
            {
                QString cn = t->getColumn(0).name();
                if (cn == "script")
                    env->appendScript(t->value(0, 0).toString());
                else if (cn == "html")
                    env->appendHtml(t->value(0, 0).toString());
                else
                {
                    env->appendTable(t);
                    t = nullptr;
                }
            }
            else
            {
                env->appendTable(t);
                t = nullptr;
            }

            if (t)
                delete t;
        }
    }
    else if (s->type == Scripting::Script::Type::QS)
    {
        QJSEngine e;
        qmlRegisterAnonymousType<DataTable>("dummy", 1);
        QQmlEngine::setObjectOwnership(connection, QQmlEngine::CppOwnership);
        QJSValue cn = e.newQObject(connection);
        e.globalObject().setProperty("__connection", cn);

        // environment access in a functional style
        QQmlEngine::setObjectOwnership(env, QQmlEngine::CppOwnership);
        QJSValue cppEnv = e.newQObject(env);
        e.globalObject().setProperty("__env", cppEnv);
        QJSValue env_fn = e.evaluate(R"(
                                     function(objectType) {
                                        return __env.value(objectType);
                                     })");
        e.globalObject().setProperty("env", env_fn);

        QJSValue execFn = e.evaluate(R"(
                                     function(query) {
                                        return __connection.execute(query, Array.prototype.slice.call(arguments, 1));
                                     })");
        e.globalObject().setProperty("exec", execFn);

        QJSValue returnTableFn = e.evaluate(R"(
                                        function(resultset) {
                                            __env.appendTable(resultset);
                                        })");
        e.globalObject().setProperty("returnTable", returnTableFn);
        QJSValue returnScriptFn = e.evaluate(R"(
                                        function(script) {
                                            __env.appendScript(script);
                                        })");
        e.globalObject().setProperty("returnScript", returnScriptFn);

        QJSValue returnTextFn = e.evaluate(R"(
                                        function(text) {
                                            __env.appendText(text);
                                        })");
        e.globalObject().setProperty("returnText", returnTextFn);

        QJSValue execRes = e.evaluate(query);
        if (execRes.isError())
            throw QObject::tr("error at line %1: %2").arg(execRes.property("lineNumber").toInt()).arg(execRes.toString());
    }
}

std::unique_ptr<CppConductor> execute(
        DbConnection *connection,
        Context context,
        const QString &objectType,
        std::function<QVariant (QString)> envCallback)
{
    std::unique_ptr<CppConductor> env { new CppConductor(nullptr, envCallback) };
    auto s = Scripting::getScript(connection, context, objectType);
    if (!s)
        return nullptr;
    execute(env.get(), connection, s);
    return env;
}

std::unique_ptr<CppConductor> execute(
        std::shared_ptr<DbConnection> connection,
        Context context,
        const QString &objectType,
        std::function<QVariant(QString)> envCallback)
{
    std::unique_ptr<CppConductor> env { new CppConductor(connection, envCallback) };
    auto s = Scripting::getScript(connection.get(), context, objectType);
    if (!s)
        return nullptr;
    execute(env.get(), connection.get(), s);
    return env;
}

CppConductor::~CppConductor()
{
    clear();
}

QVariant CppConductor::value(QString type)
{
    if (_cb)
        return _cb(type);
    return QVariant();
}

void CppConductor::appendTable(DataTable *table)
{
    resultsets.append(table);
    QQmlEngine::setObjectOwnership(table, QQmlEngine::CppOwnership);
}

void CppConductor::appendScript(QString script)
{
    scripts.append(script);
}

void CppConductor::appendHtml(QString html)
{
    htmls.append(html);
}

void CppConductor::appendText(QString text)
{
    texts.append(text);
}

void CppConductor::clear()
{
    qDeleteAll(resultsets);
    resultsets.clear();
    scripts.clear();
    htmls.clear();
    texts.clear();
}


} // namespace Scripting
