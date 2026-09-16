#include "scripting.h"
#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <QTextStream>
#include <QRegularExpression>
#include "dbconnection.h"
#include "datatable.h"
#include "settings.h"
#include "sqllexer.h"
#include "scriptversionfilter.h"
#include "resourcelocator.h"
#include <QJSEngine>
#include <QJSValueList>
#include <QQmlEngine>

namespace Scripting
{

// key = dbms_scripting_id/context/, value = { type, script }
static QHash<QString, QHash<QString, Script>> _scripts;

QString dbmsScriptPath(DbConnection *con, Context context)
{
    if (!con || (!con->scriptCatalog().isValid() && !con->open()))
        throw QObject::tr("db connection unavailable");

    QString contextFolder = context2str(context);
    if (!contextFolder.isEmpty())
        contextFolder += '/';
    // Shared with ScriptCatalog::file()/scriptDirs() - same cache, same
    // resource-root walk, so a connection and a bare catalog copied from it
    // always agree on where the bundle lives.
    return resolveScriptRoot(con->dbmsScriptingID(), con->dbmsName(), con->isOdbcConnection()) + contextFolder;
}

QStringList dbmsScriptDirs(DbConnection *con, Context context)
{
    return appResources().dirs(dbmsScriptPath(con, context));
}

QString dbmsFile(DbConnection *con, const QString &name)
{
    return appResources().file(dbmsScriptPath(con, Context::Root) + name);
}

void refresh(DbConnection *connection, Context context)
{
    if (!connection)
        return;

    const QStringList dirs = dbmsScriptDirs(connection, context);
    // The version is asked once per bundle, not once per file: it is the same
    // number for all of them, and for an odbc data source every call is a query
    // (the version comes from the root level version.sql/qs script) - which
    // re-enters this very function, hence the -1 for that script itself.
    const int version =
            (context == Context::Root && connection->isOdbcConnection() ?
                 -1 : connection->dbmsComparableVersion());
    // The bunch is built aside and published in one step at the end. A
    // reference into _scripts must not be held across anything that may touch
    // the cache again: dbmsComparableVersion() above does exactly that for odbc,
    // and QHash moves its nodes on insert, so such a reference would be left
    // dangling and the next insert through it would write into freed memory.
    // Publishing at the end also keeps a throw in the middle (an unreadable
    // file, malformed version boundaries) from leaving a half-filled entry
    // behind - getScript() would consider it complete and use it forever.
    QHash<QString, Script> bunch;

    // The roots are merged file by file, not folder by folder: replacing a
    // single script must not hide the rest of the bundle. The folders come in
    // priority order, so the first script of a name is the one that counts.
    QFileInfoList files;
    for (const QString &dir: dirs)
        files += QDir(dir).entryInfoList({"*.*"}, QDir::Files);

    for (const auto &f: std::as_const(files))
    {
        QString suffix = f.suffix().toLower();
        if (suffix != "sql" && suffix != "qs")
            continue;

        if (bunch.contains(f.baseName()))
            continue;

        QFile scriptFile(f.filePath());
        if (!scriptFile.open(QIODevice::ReadOnly))
            throw QObject::tr("can't open %1").arg(f.filePath());

        QTextStream stream(&scriptFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        stream.setCodec("UTF-8");
#else
        stream.setEncoding(QStringConverter::Utf8);
#endif

        bunch.insert(
            f.baseName(),
            Scripting::Script {
                versionSpecificPart(stream.readAll(), version),
                suffix == "sql" ? Script::Type::SQL : Script::Type::QS
            });
    }
    // done reading - now the cache may be touched
    _scripts.insert(connection->dbmsScriptingID() + context2str(context),
                    bunch);
}

void clearCache()
{
    // The dbms folder is searched again as well: the winning root may have
    // changed, and with it the very bundle the scripts come from.
    clearScriptCatalogCache();
    _scripts.clear();
}

void autoSplitRoutineSignature(const QString &type, CppConductor *content, DbConnection *con)
{
    // A parameter list worth breaking up starts at four items - three or
    // fewer usually still reads fine on one line, and pulling those apart too
    // would just add noise to the overwhelming majority of routines that
    // take few arguments. listBounds() reports commas, not item count, hence
    // the -1.
    constexpr int kMinCommasToSplit = 3;
    // ...except when the line itself is unreasonably long regardless of item
    // count: a long schema-qualified name can by itself push even a
    // two-or-three-parameter signature off the visible part of the pane, at
    // which point splitting is worth it however few parameters there are -
    // just not down to a single one (bounds.separators.isEmpty() below bails
    // out before this is even reached: there is nothing to *list* one
    // parameter across several lines).
    constexpr int kLineLengthToSplit = 100;

    if (!content || content->scripts.isEmpty() || !con ||
        (type != "function" && type != "procedure"))
        return;

    auto lexer = SqlLexer::sharedFor(con);
    if (!lexer)
        return;

    // pg_get_functiondef() (and whatever the odbc content scripts use) hands
    // back the whole `CREATE [OR REPLACE] FUNCTION|PROCEDURE name(...)  ...`
    // text as one piece; a negative position asks listBounds() for the first
    // top-level bracket in it, which - see listBounds()'s own docs - is
    // exactly this parameter list, comments (the commented-out `DROP
    // FUNCTION` some content scripts prepend included) and everything else
    // notwithstanding.
    QString &script = content->scripts.last();
    const SqlListBounds bounds = lexer->listBounds(script, -1);
    if (bounds.close < 0 || bounds.separators.isEmpty())
        return;

    if (bounds.separators.size() < kMinCommasToSplit)
    {
        // Length of the line the parameter list's own '(' sits on.
        const int lineStart = script.lastIndexOf('\n', bounds.open) + 1;
        const int lineEnd = script.indexOf('\n', lineStart);
        const int lineLength = (lineEnd < 0 ? script.length() : lineEnd) - lineStart;

        if (lineLength <= kLineLengthToSplit)
            return;
    }

    const SqlListReflow reflow = SqlLexer::reflowList(script, bounds, indentUnit());
    script.replace(reflow.start, reflow.end - reflow.start, reflow.replacement);
}

std::optional<Script> getScript(DbConnection *connection, Context context, const QString &objectType)
{
    const QString key = connection->dbmsScriptingID() + context2str(context);

    // constFind() keeps the lookup from creating an empty entry for every dbms
    // ever asked about
    auto bunch = _scripts.constFind(key);
    if (bunch == _scripts.constEnd() || bunch->isEmpty())
    {
        refresh(connection, context);
        // the iterator is taken after the refresh: the one above would already
        // be pointing into a rehashed storage
        bunch = _scripts.constFind(key);
        if (bunch == _scripts.constEnd())
            return std::nullopt;
    }
    const auto it = bunch->find(objectType);
    return (it == bunch->end() ? std::nullopt : std::optional<Script>(*it));
}

void execute(
        CppConductor *env,
        DbConnection *connection,
        Script *s)
{
    QString query = s->body;

    // replace macroses with corresponding values in both sql and qs scripts
    static QRegularExpression expr("\\$(\\w+\\.\\w+)\\$");
    QStringList macros;

    {
        QRegularExpressionMatchIterator i = expr.globalMatch(query);
        // search for macroses within query text
        while (i.hasNext())
        {
            QRegularExpressionMatch match = i.next();
            if (!macros.contains(match.captured(1)))
                macros << match.captured(1);
        }
    }

    // replace macroses with values
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
    for (const QString &macro: qAsConst(macros))
#else
    for (const QString &macro: std::as_const(macros))
#endif
    {
        QString value = (macro == "dbms.version" ?
                             QString::number(connection->dbmsComparableVersion()) :
                             env->value(macro).toString());
        query = query.replace("$" + macro + "$", value.isEmpty() ? "NULL" : value);
    }

    if (s->type == Scripting::Script::Type::SQL)
    {
        connection->execute(query);
        // The tables are taken out in one guarded step. Walking the connection's
        // own list here would race with the query thread and with libpq's notice
        // callback, both of which append to it - and a QList mutated from two
        // threads at once corrupts the heap, which then surfaces at some
        // unrelated free() much later.
        QList<DataTable*> tables = connection->takeResultsets();
        for (int i = tables.size() - 1; i >= 0; --i)
        {
            DataTable *t = tables.at(i);
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

    // Some of the ddl gets generated by the dbms itself (pg_get_functiondef()
    // and friends), which is uppercase-only, so the case of the keywords is
    // unified here instead of maintaining a second copy of the scripts.
    if (!env->scripts.isEmpty() &&
        SqtSettings::value("lowercaseKeywords", false).toBool())
    {
        const auto lexer = SqlLexer::sharedFor(connection);
        if (lexer)
        {
            for (QString &script: env->scripts)
                script = lexer->foldKeywords(script);
        }
    }
}

std::unique_ptr<CppConductor> execute(
        DbConnection *connection,
        Context context,
        const QString &objectType,
        std::function<QVariant (QString)> envCallback)
{
    std::unique_ptr<CppConductor> env { new CppConductor(connection ? connection->scriptCatalog() : Scripting::ScriptCatalog(), envCallback) };
    auto s = Scripting::getScript(connection, context, objectType);
    if (!s)
        return nullptr;
    execute(env.get(), connection, &s.value());
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
