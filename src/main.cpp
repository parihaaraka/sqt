#include <QtWidgets/QApplication>
#include "mainwindow.h"
#include "appeventhandler.h"
#include "settings.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/sqt.ico"));
    QCoreApplication::setOrganizationName("parihaaraka");
    QCoreApplication::setApplicationName("sqt");
    QCoreApplication::setApplicationVersion("0.3.4");
    setlocale(LC_NUMERIC, "C");

    AppEventHandler appEventHandler;
    a.installEventFilter(&appEventHandler);

    MainWindow w;
	w.show();

	return a.exec();
}
