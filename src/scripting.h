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

class CppConductor : public QObject
{
    Q_OBJECT
public:
    CppConductor(std::function<QVariant(QString)> cb) : cb(cb) {}
    CppConductor(const CppConductor&) = delete;
    ~CppConductor();

private:
    std::function<QVariant(QString)> cb;

public:
    QList<DataTable*> _resultsets;
    QList<QString> _scripts;
    QList<QString> _html;

public slots:
    QVariant value(QString type);
    void appendTable(DataTable *table);
    void appendScript(QString script);
    void appendHtml(QString html);
    void clear();
};

enum class Context { Root = 0, Tree, Content, Preview };
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
std::unique_ptr<CppConductor> execute(DbConnection *connection, Context context, const QString &objectType, std::function<QVariant(QString)> envCallback);

}

#endif // SCRIPTS_H
