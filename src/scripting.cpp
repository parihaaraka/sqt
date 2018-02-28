#include "scripting.h"
#include "dbconnection.h"
#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <QTextStream>
#include <QRegularExpression>
#include "dbconnection.h"
#include "odbcconnection.h"

namespace Scripting
{

// key = dbms info, value = root scripts path
QHash<QString, QString> _dbms_paths;
// key = path/context/, value = { type, script }
QHash<QString, QHash<QString, Script>> _scripts;

QString dbmsScriptPath(DbConnection *con, Context context)
{
    if (!con)
        throw QObject::tr("db connection unavailable");

    // ensure the connection is opened
    con->open();
    QString dbmsID = con->dbmsName() + con->dbmsVersion();
    QString contextFolder;
    switch (context) {
    case Context::Tree:
        contextFolder = "tree/";
        break;
    case Context::Content:
        contextFolder = "content/";
        break;
    case Context::Preview:
        contextFolder = "preview/";
        break;
    default: // root
        contextFolder = "";
    }

    const auto it = _dbms_paths.find(dbmsID);
    if (it != _dbms_paths.end())
        return it.value() + contextFolder;

    QString startPath = QApplication::applicationDirPath() + "/scripts/";
    OdbcConnection *odbcConnection = qobject_cast<OdbcConnection*>(con);
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
        throw QObject::tr("directory %1 not available").arg(startPath + contextFolder);
    _dbms_paths.insert(dbmsID, startPath);
    return startPath + contextFolder;
}

///
/// \brief dbms The function extracts version specific part of script or entire content.
/// \param script Content of script file.
/// \param version Current dbms comparable version (or db-level compartibility level).
/// \return dbms Version specific part of script.
///
/// A script may contain comments corresponding to regexp:
/// (?=\/\*\s*V(\d+)\+\s*\*\/)
/// For example:
/// /* V90000+ */
/// select s.datname, s.pid, s.usename --, ...
/// from pg_stat_activity s
///
/// /* V100000+ */
/// select s.datname, s.pid, s.backend_type, s.usename --, ...
/// from pg_stat_activity
///
/// Such boundaries split a script into parts acording to dbms version.
/// PostgreSQL uses libpq's PQserverVersion(), ODBC data sources must provide
/// version.sql or version.qs to return this value if used within scripts.
/// E.g. scripts/odbc/microsoft sql/version.sql
/// (uses compartibility_level as a comparable version).
///
QString versionSpecificPart(const QString &script, int version)
{
    QRegularExpression re(R"((?=\/\*\s*V(\d+)\+\s*\*\/))");
    QRegularExpressionMatchIterator i = re.globalMatch(script);

    // return whole script body if boundaries not found
    if (!i.hasNext())
        return script;

    QMap<int, QString> parts;
    while (i.hasNext())
    {
        QRegularExpressionMatch match = i.next();
        int key = match.captured(1).toInt();
        QString value;
        if (i.hasNext())
        {
            QRegularExpressionMatch next_match = i.peekNext();
            value = script.mid(match.capturedEnd(), next_match.capturedStart() - match.capturedEnd());
        }
        else
        {
            value = script.mid(match.capturedEnd());
        }
        parts[key] = value;
    }
    // search for the highest available version
    // (QMap is sorted by key)
    QMap<int, QString>::const_iterator mi = parts.constEnd();
    do
    {
        --mi;
        if (mi.key() <= version)
            return mi.value();
    }
    while (mi != parts.constBegin());
    return "";
}

void refresh(DbConnection *connection, Context context)
{
    if (!connection)
        return;

    QString path = dbmsScriptPath(connection, context);
    auto &bunch = _scripts[path];
    bunch.clear();

    QFileInfoList files = QDir(path).entryInfoList({"*.*"}, QDir::Files);
    for (auto &f : files)
    {
        QString suffix = f.suffix().toLower();
        if (suffix != "sql" && suffix != "qs")
            continue;

        QFile scriptFile(f.filePath());
        if (!scriptFile.open(QIODevice::ReadOnly))
            throw QObject::tr("can't open %1").arg(files.at(0).filePath());

        QTextStream stream(&scriptFile);
        stream.setCodec("UTF-8");
        bunch.insert(f.baseName(),
            Scripting::Script {
                versionSpecificPart(
                    stream.readAll(),
                    // prevent infinite loop - do not acquire comparable version on root level
                    context == Context::Root ? -1 : connection->dbmsComparableVersion()),
                suffix == "sql" ? Script::Type::SQL : Script::Type::QS
            }
        );
    }
}

Script* getScript(DbConnection *connection, Context context, const QString &objectType)
{
    auto &bunch = _scripts[dbmsScriptPath(connection, context)];
    if (bunch.isEmpty())
        refresh(connection, context);
    const auto it = bunch.find(objectType);
    return (it == bunch.end() ? nullptr : &it.value());
}

bool execute(DbConnection *connection, Context context, const QString &objectType)
{
    auto script = Scripting::getScript(connection, context, objectType);
    if (!script)
        return false;
    return true;
}


} // namespace Scripting
