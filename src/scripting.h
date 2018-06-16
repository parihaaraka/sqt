#ifndef SCRIPTS_H
#define SCRIPTS_H

#include <QString>
#include <functional>
#include <QVariant>
#include <memory>

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
    CppConductor(std::shared_ptr<DbConnection> cn, std::function<QVariant(QString)> cb) : _cn(cn), _cb(cb) {}
    CppConductor(const CppConductor&) = delete;
    ~CppConductor();

private:
    std::shared_ptr<DbConnection> _cn;
    std::function<QVariant(QString)> _cb;

public:
    QList<DataTable*> resultsets;
    QList<QString> scripts;
    QList<QString> htmls;
    QList<QString> texts;
    std::shared_ptr<DbConnection> connection() const { return _cn; }

public slots:
    QVariant value(QString type);
    void appendTable(DataTable *table);
    void appendScript(QString script);
    void appendHtml(QString html);
    void appendText(QString text);
    void clear();
};

enum class Context { Root = 0, Tree, Content, Preview, Autocomplete };
struct Script
{
    enum class Type { SQL, QS };
    Script(QString body, Type type) : body(body), type(type) {}
    QString body;
    Type type = Type::SQL;
};

QString dbmsScriptPath(DbConnection *con, Context context = Context::Root);
void refresh(DbConnection *connection, Context context);
Script* getScript(DbConnection *connection, Context context, const QString &objectType);
// unable to make QObject movable, but we can't allow CppConductor to be copied => unique_ptr
std::unique_ptr<CppConductor> execute(
        std::shared_ptr<DbConnection> connection,
        Context context,
        const QString &objectType,
        std::function<QVariant(QString)> envCallback);
std::unique_ptr<CppConductor> execute(
        DbConnection *connection,
        Context context,
        const QString &objectType,
        std::function<QVariant(QString)> envCallback);
}

#endif // SCRIPTS_H
