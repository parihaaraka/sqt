#ifndef SCRIPTCATALOG_H
#define SCRIPTCATALOG_H

#include <QString>
#include <QStringList>

namespace Scripting
{

enum class Context { Root = 0, Tree, Content, Preview, Autocomplete };
QString context2str(Context context);

/// Identifies which script/highlight bundle a dbms uses, without needing a
/// live DbConnection. A connection learns this once, right when it opens
/// (see DbConnection::scriptCatalog()), and the identity stays valid for as
/// long as that server identity does - including after the link itself is
/// gone (see PgConnection::clone()/closeLocked()). Consumers that only
/// resolve file paths (syntax highlighting, "new script" templates) hold a
/// copy of this instead of the connection they got it from, so nothing stops
/// a highlighter from outliving the connection, and nothing about resolving
/// a path ever has to open one.
class ScriptCatalog
{
public:
    ScriptCatalog() = default;
    ScriptCatalog(QString scriptingId, QString dbmsName, bool isOdbc);

    bool isValid() const noexcept { return !_scriptingId.isEmpty(); }
    bool operator==(const ScriptCatalog &other) const noexcept { return _scriptingId == other._scriptingId; }
    bool operator!=(const ScriptCatalog &other) const noexcept { return !(*this == other); }

    /// A stable identity for this dbms bundle, usable as a cache key by
    /// consumers that keep their own per-dbms cache (the syntax highlighter,
    /// the parsed-script cache) - the same role con->dbmsScriptingID() used
    /// to play for them directly.
    const QString& id() const noexcept { return _scriptingId; }

    /// A file of the bundle ("hl.conf"), empty if no root has it or the
    /// catalog is not valid.
    QString file(const QString &name) const;
    /// Every existing folder behind context, the winning one first; empty if
    /// the catalog is not valid.
    QStringList scriptDirs(Context context = Context::Root) const;

private:
    QString _scriptingId;
    QString _dbmsName;
    bool _isOdbc = false;
};

/// Resolves (and caches) the resource-root-relative folder for a dbms
/// identity. Shared by ScriptCatalog and by Scripting::dbmsScriptPath(), so
/// there is a single cache and a single place that walks the resource roots.
QString resolveScriptRoot(const QString &scriptingId, const QString &dbmsName, bool isOdbc);

/// Drops every cached script-root lookup. The dbms folder is searched again:
/// the winning resource root may have changed, and with it the very bundle
/// the scripts come from. Used when the resource roots change and by F5 (see
/// Scripting::clearCache(), which also drops the parsed scripts themselves).
void clearScriptCatalogCache();

} // namespace Scripting

#endif // SCRIPTCATALOG_H
