#include <QtWidgets/QApplication>
#include <QStyleFactory>
#include <QStyle>
#include "mainwindow.h"
#include "appeventhandler.h"
#include "resourcelocator.h"
#include "settings.h"
#include <cstdio>
#include <cstring>
#include "dbconnectionfactory.h"

int main(int argc, char *argv[])
{
    // Answered before QApplication is built, so that it needs no display: the
    // packaging jobs use it to check that the binary they just assembled runs
    // at all, and a headless machine must not be the reason it does not.
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v"))
        {
            printf("sqt %s\n", SQT_VERSION_STR);
            return 0;
        }
    }


#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    // A non-integer Windows/Linux scale factor (125%, 150%...) can, depending
    // on Qt's default policy for the platform and version in use, be rounded
    // to the nearest whole number for widget geometry - Qt then renders
    // everything at that rounded factor and the platform layer downscales
    // the finished picture to the display's real one. That extra downscale
    // happens after Qt is done painting, so nothing this application does -
    // including comfortableRowHeight() and keepColumnsSnappedToDevicePixels()
    // in styling.cpp, which make every row/column boundary land on a whole
    // *rendered* pixel - can see or correct for it: it resizes every already
    // pixel-perfect grid line by the very same non-integer ratio, and what
    // was "sometimes 1px, sometimes 2px" turns into a uniform "always 2px".
    // PassThrough keeps the real ratio instead of rounding it away, so there
    // is nothing left to downscale.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
                Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
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
    QCoreApplication::setApplicationVersion(SQT_VERSION_STR);
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

    //return a.exec();
    int rc = a.exec();
    DbConnectionFactory::clearConnections();
    return rc;
}
