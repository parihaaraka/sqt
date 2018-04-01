#include <QtWidgets/QApplication>
#include "mainwindow.h"
#include "appeventhandler.h"
#include <QScreen>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/sqt.ico"));
    QCoreApplication::setOrganizationName("parihaaraka");
    QCoreApplication::setApplicationName("sqt");
    QCoreApplication::setApplicationVersion("0.2.15");
    setlocale(LC_NUMERIC, "C");

    // TODO first start eveluation only
    int fontSize = QGuiApplication::primaryScreen()->physicalDotsPerInch() > 120 ? 10 : 9;

    // TODO move to settings
    a.setStyleSheet(QString(R"(
QApplication { font-size: %1pt; }
QTableView, QHeaderView { font-size: %2pt; }
QTabBar::tab { height: 2em; }
QPlainTextEdit { font-family: Consolas, Menlo, 'Liberation Mono', 'Lucida Console', 'DejaVu Sans Mono', 'Courier New', monospace }
                            )").
                            arg(fontSize).
                            arg(fontSize - 0.5));

    AppEventHandler appEventHandler;
    a.installEventFilter(&appEventHandler);

    MainWindow w;
	w.show();

	return a.exec();
}
