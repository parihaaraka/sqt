// SqlLexer::sharedFor()/clearCache(): the only two members that need a
// DbConnection (to find and cache "this connection's dictionary file") rather
// than just a QString to scan. Kept apart from sqllexer.cpp on purpose - that
// file is otherwise pure text scanning with no db/gui dependency at all,
// which is what lets tests/tst_sqllexer.cpp compile it on its own the same
// way tests/tst_filesearch.cpp does filesearch.cpp (see the comment at the
// top of tests/CMakeLists.txt).
#include "sqllexer.h"
#include "dbconnection.h"
#include "misc.h"
#include "scripting.h"

// key = dbms_scripting_id
static QHash<QString, std::shared_ptr<const SqlLexer>> lexerCache;

std::shared_ptr<const SqlLexer> SqlLexer::sharedFor(DbConnection *con)
{
    if (!con)
        return nullptr;

    const auto it = lexerCache.find(con->dbmsScriptingID());
    if (it != lexerCache.end())
        return it.value();

    QJsonDocument settings;
    try
    {
        // Through the locator, not by the relative path alone: dbmsScriptPath()
        // returns a path relative to a *resource root*, so handing it straight
        // to QFile resolved it against the process' working directory - which is
        // a root only when the application happens to be started from its own
        // folder. Everywhere else the file was simply not found and the catch
        // below silently turned that into "no dictionaries", disabling the
        // statement split and the keyword folding.
        const QString file = Scripting::dbmsFile(con, "hl.conf");
        if (file.isEmpty())
            return nullptr;
        settings = readJsonFile(file);
    }
    catch (const QString &)
    {
        // no highlighting settings - no dictionaries to rely on
        return nullptr;
    }

    auto lexer = std::make_shared<const SqlLexer>(settings);
    lexerCache.insert(con->dbmsScriptingID(), lexer);
    return lexer;
}

void SqlLexer::clearCache()
{
    lexerCache.clear();
}
