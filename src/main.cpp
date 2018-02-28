#include <QtWidgets/QApplication>
#include <QTranslator>
#include <QLibraryInfo>
#include "mainwindow.h"

#include "odbcconnection.h"
#include <memory>
#include <QObject>
#include <QDebug>
#include "datatable.h"

#include <QJSEngine>
#include <QJSValueList>
#include <QQmlEngine>
#include <QRegularExpression>
#include <iostream>

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/sqt.ico"));
    QCoreApplication::setOrganizationName("parihaaraka");
    QCoreApplication::setApplicationName("sqt");
    QCoreApplication::setApplicationVersion("0.2.12");
    if (QApplication::font().pointSize() > 9)
    {
        QFont appFont(QApplication::font());
        appFont.setPointSize(9);
        QApplication::setFont(appFont);
    }

    /// TODO: move to settings
    a.setStyleSheet("QTableView, QHeaderView { font-size: 8.5pt; }\nQTabBar::tab { height: 2em; }");

    /*
    QTranslator qtTranslator;
    qtTranslator.load("qt_" + QLocale::system().name(),
            QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    a.installTranslator(&qtTranslator);
    */
    MainWindow w;

/*
    std::unique_ptr<OdbcConnection> con(new OdbcConnection());
    con->setConnectionString("Driver=FreeTDS;Server=10.0.1.246;TDS_Version=7.2;Port=1433;Database=Elf3;UID=andrey;PWD=ap502034;ClientCharset=UTF-8;App=sqt;");
    con->setDatabase("Elf3");
    QObject::connect(con.get(), &DbConnection::error, [](QString msg) {qDebug() << msg;});
    if (!con->open())
        return -1;

    QJSEngine e;
    qmlRegisterType<DataTable>();
    QQmlEngine::setObjectOwnership(con.get(), QQmlEngine::CppOwnership);
    QJSValue cn = e.newQObject(con.get());
    e.globalObject().setProperty("connection", cn);

    QQmlEngine::setObjectOwnership(&w, QQmlEngine::CppOwnership);
    QJSValue mw = e.newQObject(&w);
    e.globalObject().setProperty("ui", mw);

    e.installExtensions(QJSEngine::ConsoleExtension);
    QJSValue exec = e.evaluate(
                R"(
                function(query) {
                    return connection.execute(query, Array.prototype.slice.call(arguments, 1));
                }
    )");
    e.globalObject().setProperty("exec", exec);

    QVariantList res;
    e.globalObject().setProperty("resultsets", e.toScriptValue(res));

    QJSValue execRes = e.evaluate(
                R"(
                var res1 = exec("select 1 x, 2 y, 'test1' col3", 'param1', 7);
                var res2 = exec("select 3 x, 4 y, 'test2' col3", ui.current('table', 'id'), 7);
                // ui.selected('column', 'id')
                res1.takeRows(res2);
                ui.pushTable(res1, 'name');
                //ui.pushSql('select ..');
                //ui.pushHtml('<html>...')
                console.debug(res2.rowCount(), res1.value(1, 'col3'));
                script = 'hello from script';
                )");
    if (execRes.isError())
    {
        qDebug() << execRes.property("lineNumber").toInt() << ":" << execRes.toString();
        return -1;
    }

    QString script;
    QJSValue sScript = e.globalObject().property("script");
    if (!sScript.isUndefined())
        script = sScript.toString();
    qDebug() << "script: " << script;

    for (auto r: con->_resultsets)
    {
        for (int c = 0; c < r->columnCount(); ++c)
            qDebug() << r->getColumn(c).name() << ',';
        qDebug() << '\n';
        for (int row = 0; row < r->rowCount(); ++row)
        {
            for (int c = 0; c < r->columnCount(); ++c)
                qDebug() << r->getRow(row)[c].toString() << ',';
            qDebug() << '\n';
        }
    }
    return 0;
*/

	w.show();

	return a.exec();

}

/*
todo in qtscript:

var tmp = execute('select $1', 'test');
tmp.append(execute('select $1, $2', 'x', 5));
results.add('description text: ' + tmp.value(0,1).toString(), 'text');
results.add('<b>test html</b>', 'html');
results.add('create table ...', 'sql');
results.add(tmp);


QJSValue print = fEngine->evaluate("function() { printCallback(Array.prototype.slice.apply(arguments));}");
fEngine->globalObject().setProperty("print", print);
*/
