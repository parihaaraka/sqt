#include "scriptcatalog.h"
#include "resourcelocator.h"
#include <QDir>
#include <QCoreApplication>
#include <QHash>

namespace Scripting
{

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

namespace
{
// key = dbms_scripting_id, value = scripts path relative to a resource root
QHash<QString, QString> &scriptRootCache()
{
    static QHash<QString, QString> cache;
    return cache;
}
} // namespace

QString resolveScriptRoot(const QString &scriptingId, const QString &dbmsName, bool isOdbc)
{
    if (scriptingId.isEmpty())
        throw QCoreApplication::translate("Scripting", "db connection unavailable");

    const auto it = scriptRootCache().constFind(scriptingId);
    if (it != scriptRootCache().constEnd())
        return it.value();

    // Paths are kept relative to a resource root: the same bundle may be laid
    // out next to the binary, under the user's home and in the system-wide
    // folder at once, and the choice between them belongs to the locator.
    QString startPath = QString("scripts/") + (isOdbc ? "odbc/" : "");
    const QStringList dirs = appResources().dirs(startPath);
    if (dirs.isEmpty())
        throw QCoreApplication::translate("Scripting", "directory %1 is not found in %2")
                .arg(startPath, appResources().roots().join(", "));

    if (dbmsName.isEmpty())
        throw QCoreApplication::translate("Scripting", "unable to get dbms name");

    // search for the folder with a name containing the dbms name; it may live
    // in any of the roots, so all of them are asked in turn
    QString endPath;
    for (const QString &dir: dirs)
    {
        const QStringList subdirs = QDir(dir).entryList(QStringList(), QDir::AllDirs | QDir::NoDotAndDotDot);
        for (const QString &d: subdirs)
        {
            if (dbmsName.contains(d, Qt::CaseInsensitive))
            {
                endPath = d + "/";
                break;
            }
        }
        if (!endPath.isEmpty())
            break;
    }

    // if a specific folder was not found for an odbc driver
    if (endPath.isEmpty() && isOdbc)
        startPath += "default/";
    else
        startPath += endPath;

    if (appResources().dirs(startPath).isEmpty())
        throw QCoreApplication::translate("Scripting", "directory %1 is not available").arg(startPath);
    scriptRootCache().insert(scriptingId, startPath);
    return startPath;
}

void clearScriptCatalogCache()
{
    scriptRootCache().clear();
}

ScriptCatalog::ScriptCatalog(QString scriptingId, QString dbmsName, bool isOdbc) :
    _scriptingId(std::move(scriptingId)), _dbmsName(std::move(dbmsName)), _isOdbc(isOdbc)
{
}

QStringList ScriptCatalog::scriptDirs(Context context) const
{
    // An invalid catalog just means "nothing to show yet" (no selection, no
    // connection) rather than a misconfiguration, so it is reported quietly.
    // A genuinely missing resource folder, on the other hand, is still worth
    // surfacing and is left to propagate as before.
    if (!isValid())
        return {};
    QString contextFolder = context2str(context);
    if (!contextFolder.isEmpty())
        contextFolder += '/';
    return appResources().dirs(resolveScriptRoot(_scriptingId, _dbmsName, _isOdbc) + contextFolder);
}

QString ScriptCatalog::file(const QString &name) const
{
    if (!isValid())
        return QString();
    return appResources().file(resolveScriptRoot(_scriptingId, _dbmsName, _isOdbc) + name);
}

} // namespace Scripting
