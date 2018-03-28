QT  += widgets qml

TARGET = sqt
TEMPLATE = app
CONFIG += c++11

SOURCES += main.cpp\
        mainwindow.cpp \
    dbobjectsmodel.cpp \
    dbobject.cpp \
    logindialog.cpp \
    dbosortfilterproxymodel.cpp \
    odbcconnection.cpp \
    querywidget.cpp \
    tablemodel.cpp \
    extfiledialog.cpp \
    dbtreeitemdelegate.cpp \
    findandreplacepanel.cpp \
    connectiondialog.cpp \
    dbconnection.cpp \
    datatable.cpp \
    dbconnectionfactory.cpp \
    pgconnection.cpp \
    pgparams.cpp \
    sqlsyntaxhighlighter.cpp \
    scripting.cpp \
    appeventhandler.cpp

HEADERS  += mainwindow.h \
    dbobjectsmodel.h \
    dbobject.h \
    logindialog.h \
    dbosortfilterproxymodel.h \
    odbcconnection.h \
    querywidget.h \
    tablemodel.h \
    extfiledialog.h \
    dbtreeitemdelegate.h \
    findandreplacepanel.h \
    connectiondialog.h \
    dbconnection.h \
    datatable.h \
    dbconnectionfactory.h \
    pgconnection.h \
    pgtypes.h \
    pgparams.h \
    sqlsyntaxhighlighter.h \
    scripting.h \
    appeventhandler.h

FORMS    += mainwindow.ui \
    logindialog.ui \
    findandreplacepanel.ui \
    connectiondialog.ui

RESOURCES += \
    sqt.qrc

#https://wiki.qt.io/Install_Qt_5_on_Ubuntu
unix {
    INCLUDEPATH += /usr/include/postgresql
    LIBS += -lodbc -lpq -lssl
}

win32 {
#win32-g++:contains(QMAKE_HOST.arch, x86_64):{
win32-msvc*:contains(QMAKE_HOST.arch, x86_64):{
    LIBS += "-LC:/Program Files (x86)/Microsoft SDKs/Windows/v7.1A/Lib/x64" \
        -lodbc32 \
        -lodbccp32
} else {
    LIBS += "-LC:/Program Files (x86)/Microsoft SDKs/Windows/v7.1A/Lib" \
        -lodbc32 \
        -lodbccp32
}
    RC_FILE = sqt.rc
}

# TODO
#QMAKE_EXTRA_TARGETS
#ln -s $$_PRO_FILE_PWD_/../decor $$OUT_PWD && ln -s $$_PRO_FILE_PWD_/../scripts $$OUT_PWD
