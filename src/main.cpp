#include <QtWidgets/QApplication>
#include <QStyleFactory>
#include <QStyle>
#include "mainwindow.h"
#include "appeventhandler.h"
#include "resourcelocator.h"
#include "settings.h"

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication a(argc, argv);
    // windows11 style is broken (QTBUG-124235, QTBUG-124150)
    // (QTableView cell selection in ligth theme)
    if (a.style() && a.style()->name() == "windows11")
    {
        if (QStyle *style = QStyleFactory::create("fusion"))
            a.setStyle(style);
    }
    a.setWindowIcon(QIcon(":/sqt.ico"));
    QCoreApplication::setOrganizationName("parihaaraka");
    QCoreApplication::setApplicationName("sqt");
    QCoreApplication::setApplicationVersion("0.5.0");
    setlocale(LC_NUMERIC, "C");

    // The scripts and the icons are looked up through the locator, so the folder
    // of the user's own copies has to be known before anything asks for a file.
    // The name of the application is set above - the settings depend on it.
    SqtSettings::load();
    setAppResourcesUserDir(SqtSettings::value("assetsDir").toString());

    AppEventHandler appEventHandler;
    a.installEventFilter(&appEventHandler);

    MainWindow w;
    w.show();

    return a.exec();
}
