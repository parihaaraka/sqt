QT  += widgets qml

TARGET = sqt
TEMPLATE = app
CONFIG += c++11

# prevent making debug and release subfolders in target dir on windows
CONFIG -= debug_and_release debug_and_release_target

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
    appeventhandler.cpp \
    copycontext.cpp \
    codeeditor.cpp \
    settings.cpp \
    settingsdialog.cpp \
    jsonsyntaxhighlighter.cpp \
    sqlparser.cpp

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
    appeventhandler.h \
    copycontext.h \
    codeeditor.h \
    settings.h \
    settingsdialog.h \
    jsonsyntaxhighlighter.h \
    sqlparser.h

FORMS    += mainwindow.ui \
    logindialog.ui \
    findandreplacepanel.ui \
    connectiondialog.ui \
    settingsdialog.ui

RESOURCES += \
    sqt.qrc

#https://wiki.qt.io/Install_Qt_5_on_Ubuntu
unix {
    INCLUDEPATH += /usr/include/postgresql
    LIBS += -lodbc -lpq -lssl
    QMAKE_POST_LINK += $$quote($$QMAKE_SYMBOLIC_LINK $$system_quote($${_PRO_FILE_PWD_}/../decor) $$system_quote($$OUT_PWD))$$escape_expand(\n\t)
    QMAKE_POST_LINK += $$quote($$QMAKE_SYMBOLIC_LINK $$system_quote($${_PRO_FILE_PWD_}/../scripts) $$system_quote($$OUT_PWD))$$escape_expand(\n\t)
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

    PG_BIN_TREE = "C:/Dev/pgsql-9.6.6"
    LIBS += "$${PG_BIN_TREE}/lib/libpq.lib"
    INCLUDEPATH += "$${PG_BIN_TREE}/include" "$${PG_BIN_TREE}/include/libpq"

    QMAKE_EXTRA_TARGETS += makedirs
    makedirs.target = sqt_subdirs
    makedirs.commands = $$sprintf($$QMAKE_MKDIR_CMD, $$OUT_PWD/decor) $$escape_expand(\n\t) $$sprintf($$QMAKE_MKDIR_CMD, $$OUT_PWD/scripts)

    QMAKE_POST_LINK += $$QMAKE_COPY_DIR $$system_quote($$shell_path($${_PRO_FILE_PWD_}/../decor)) $$system_quote($$shell_path($$OUT_PWD/decor))$$escape_expand(\n\t)
    QMAKE_POST_LINK += $$QMAKE_COPY_DIR $$system_quote($$shell_path($${_PRO_FILE_PWD_}/../scripts)) $$system_quote($$shell_path($$OUT_PWD/scripts))$$escape_expand(\n\t)
}
