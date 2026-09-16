#ifndef SCRIPTS_H
#define SCRIPTS_H

#include <QString>
#include <optional>
#include <functional>
#include <QVariant>
#include <memory>
#include "scriptcatalog.h"

class DbConnection;
class DataTable;

namespace Scripting
{

/*!
 * \brief Class emits api to js code.
 *
 * Technically, sql or js script may return a bunch of data (resultsets, scripts)
 * to be used to display db object content (tab pages with grids, scripts and so on).
 * CppConductor accumulates all the returned data, that's why there are lists of
 * resultsets, scripts and html content. This multipart result may be used in the future,
 * but for now we use simple gui and display only one item at a time. To show additional
 * data on the db object's dependent part of gui the one should add additional subitems
 * within db tree node and display every part of context data separately.
 */
class CppConductor : public QObject
{
    Q_OBJECT
public:
    /// \a catalog is whatever the connection execute() ran against already
    /// knew (see DbConnection::scriptCatalog()) - the result carries it along
    /// so a caller that only has the CppConductor, not the connection, can
    /// still pick a highlighter for what it shows (see MainWindow::showContent()).
    CppConductor(Scripting::ScriptCatalog catalog, std::function<QVariant(QString)> cb) :
        _catalog(std::move(catalog)), _cb(cb) {}
    CppConductor(const CppConductor&) = delete;
    ~CppConductor();

private:
    Scripting::ScriptCatalog _catalog;
    std::function<QVariant(QString)> _cb;

public:
    QList<DataTable*> resultsets;
    QList<QString> scripts;
    QList<QString> htmls;
    QList<QString> texts;
    const Scripting::ScriptCatalog& scriptCatalog() const { return _catalog; }

public slots:
    QVariant value(QString type);
    void appendTable(DataTable *table);
    void appendScript(QString script);
    void appendHtml(QString html);
    void appendText(QString text);
    void clear();
};

struct Script
{
    enum class Type { SQL, QS };
    Script(QString body, Type type) : body(body), type(type) {}
    QString body;
    Type type = Type::SQL;
};

/// The scripts folder of the dbms, relative to a resource root
/// ("scripts/postgres/content/"). May open() \a con if its script catalog
/// identity is not known yet.
QString dbmsScriptPath(DbConnection *con, Context context = Context::Root);
/// Every existing folder behind that path, the winning one first.
QStringList dbmsScriptDirs(DbConnection *con, Context context = Context::Root);
/// A file of the dbms bundle ("hl.conf"), empty if no root has it.
QString dbmsFile(DbConnection *con, const QString &name);
void refresh(DbConnection *connection, Context context);
/// Drops every cached script and path. The cache refills lazily on the next
/// getScript()/dbmsScriptPath() call, so nothing is re-read eagerly. Used when
/// the resource roots change (the assets directory is a setting now) and by
/// F5, where it replaces the four refresh() calls.
void clearCache();

/// Breaks a freshly generated function/procedure DDL's parameter list across
/// several lines when it has more than a few of them - pg_get_functiondef()
/// (and whatever the odbc content scripts use) hands back a single long
/// line, however many arguments it declares. Mutates content->scripts.last()
/// in place; a no-op if that is not actually a routine (see \a type) or is
/// short enough to leave alone.
///
/// The tree has no notion of "this kind of node is a routine" beyond the type
/// name a scripts/<dbms>/tree script happens to be registered under - a
/// user's own tree may register arbitrary type names for arbitrary objects,
/// and there is no way to know what any of them mean. So this is hardcoded to
/// the two type names the bundled postgres and odbc scripts themselves use,
/// rather than attempting to infer "routine-ness" some other way; a script
/// registered under any other name is simply left alone.
void autoSplitRoutineSignature(const QString &type, CppConductor *content, DbConnection *con);

/// Returns a copy: a pointer into the storage would be invalidated by the very
/// next refresh() of the same context.
std::optional<Script> getScript(DbConnection *connection, Context context, const QString &objectType);
// unable to make QObject movable, but we can't allow CppConductor to be copied => unique_ptr
std::unique_ptr<CppConductor> execute(
        DbConnection *connection,
        Context context,
        const QString &objectType,
        std::function<QVariant(QString)> envCallback);
}

#endif // SCRIPTS_H
