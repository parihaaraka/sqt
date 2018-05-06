#include "settings.h"
#include <QSettings>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>

namespace SqtSettings
{

QMutex _m;
QHash<QString, QVariant> _settings;

void load()
{
    QMutexLocker locker(&_m);
    _settings.clear();
    QSettings settings;
    for (const auto &k: settings.allKeys())
        _settings.insert(k, settings.value(k));

    QString appStyle = settings.value("appStyle").toString();
    if (appStyle.isEmpty())
    {
        int fontSize = QGuiApplication::primaryScreen()->physicalDotsPerInch() > 120 ? 10 : 9;
        appStyle = QString(
                    "QApplication { font-size: %1pt; } \n"
                    "QTableView, QHeaderView { font-size: %2pt; }\n"
                    "QTabBar::tab { height: 2em; }\n"
                    "QPlainTextEdit {\n"
                    "   font-family: Consolas, Menlo, 'Liberation Mono', 'Lucida Console', 'DejaVu Sans Mono', 'Courier New', monospace;\n"
                    "   font-size: %3pt;\n"
                    "}").
                arg(fontSize).arg(fontSize - 0.5).arg(fontSize + 1);
        settings.setValue("appStyle", appStyle);
        _settings.insert("appStyle", appStyle);
    }

    bool ok;
    int tabSize = settings.value("tabSize", -1).toInt(&ok);
    if (!ok || tabSize <= 0)
    {
        settings.setValue("tabSize", 3);
        _settings.insert("tabSize", 3);
    }
    locker.unlock();

    QApplication *app = qobject_cast<QApplication*>(QGuiApplication::instance());
    if (app)
        app->setStyleSheet(appStyle);
}

QVariant value(const QString &name, const QVariant &defaultValue)
{
    QMutexLocker locker(&_m);
    const auto it = _settings.find(name);
    if (it == _settings.end())
        return defaultValue;

    return it.value();
}

void setValue(const QString &key, const QVariant &value)
{
    QSettings settings;
    settings.setValue(key, value);

    QMutexLocker locker(&_m);
    _settings.insert(key, value);
}

} // namespace SqtSettings
