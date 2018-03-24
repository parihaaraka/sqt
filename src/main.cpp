#include <QtWidgets/QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/sqt.ico"));
    QCoreApplication::setOrganizationName("parihaaraka");
    QCoreApplication::setApplicationName("sqt");
    QCoreApplication::setApplicationVersion("0.2.13");
    if (QApplication::font().pointSize() > 9)
    {
        QFont appFont(QApplication::font());
        appFont.setPointSize(9);
        QApplication::setFont(appFont);
    }

    /// TODO: move to settings
    a.setStyleSheet("QTableView, QHeaderView { font-size: 8.5pt; }\nQTabBar::tab { height: 2em; }");

    MainWindow w;
	w.show();

	return a.exec();
}
