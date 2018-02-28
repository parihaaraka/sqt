#ifndef SCRIPTS_H
#define SCRIPTS_H

#include <QString>

class DbConnection;

namespace Scripting
{

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
bool execute(DbConnection *connection, Context context, const QString &objectType);

}

#endif // SCRIPTS_H
