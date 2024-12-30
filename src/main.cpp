#include <QtWidgets/QApplication>
#include "mainwindow.h"
#include "appeventhandler.h"

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/sqt.ico"));
    QCoreApplication::setOrganizationName("parihaaraka");
    QCoreApplication::setApplicationName("sqt");
    QCoreApplication::setApplicationVersion("0.4.6");
    setlocale(LC_NUMERIC, "C");

    AppEventHandler appEventHandler;
    a.installEventFilter(&appEventHandler);

    MainWindow w;
	w.show();

	return a.exec();
}
