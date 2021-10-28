#include "settings.h"
#include <QSettings>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QRegularExpression>
#include <QFont>

namespace SqtSettings
{

static QMutex _m;
static QHash<QString, QVariant> _settings;

void load()
{
    QMutexLocker locker(&_m);
    _settings.clear();
    QSettings settings;
    const auto allKeys = settings.allKeys();
    for (const auto &k: allKeys)
        _settings.insert(k, settings.value(k));

    auto setDefault = [&settings](const QString &key, const QVariant &value) {
        settings.setValue(key, value);
        _settings.insert(key, value);
    };

    QString appStyle = settings.value("appStyle").toString();
    if (appStyle.isEmpty())
    {
        int fontSize = QGuiApplication::primaryScreen()->physicalDotsPerInch() > 120 ? 10 : 9;
        appStyle = QString(
                    "QApplication { font-size: %1pt; } \n"
                    "QTableView, QHeaderView { font-size: %2pt; }\n"
                    "QTableView::item { padding: 0.2em; border: 0px; }\n"
                    "QTabBar::tab { height: 2em; }\n"
                    "QPlainTextEdit {\n"
                    "   font-family: Consolas, Menlo, 'Liberation Mono', 'Lucida Console', 'DejaVu Sans Mono', 'Courier New', monospace;\n"
                    "   font-size: %3pt;\n"
                    "}").
                arg(fontSize).arg(fontSize - 0.5).arg(fontSize + 1);
        setDefault("appStyle", appStyle);
    }

    if (settings.value("encodings").toString().isEmpty())
        setDefault("encodings", "UTF-8,Windows-1251,UTF-16LE,cp866");

    if (settings.value("f1url").toString().isEmpty())
        setDefault("f1url", (QLocale::system().language() == QLocale::Russian ?
                                 "https://postgrespro.ru/docs/postgresql/current/sql-commands" :
                                 "https://www.postgresql.org/docs/current/static/sql-commands.html"
                            ));

    if (settings.value("shiftF1url").toString().isEmpty())
        setDefault("shiftF1url", (QLocale::system().language() == QLocale::Russian ?
                                 "https://postgrespro.ru/docs/postgresql/current/functions" :
                                 "https://www.postgresql.org/docs/current/static/functions.html"
                            ));

    bool ok;
    int tabSize = settings.value("tabSize", -1).toInt(&ok);
    if (!ok || tabSize <= 0)
        setDefault("tabSize", 3);

    locker.unlock();
    QApplication *app = qobject_cast<QApplication*>(QGuiApplication::instance());
    if (app)
    {
        QFont f;
        f.setFamily(f.defaultFamily()); // restore system font if font-family is not specified

        // QApplication does not support font styling via setStyleSheet(),
        // so lets do it manually
        QRegularExpression re(R"(QApplication\s*{([^}]+))");
        QRegularExpressionMatch match = re.match(appStyle);
        if (match.hasMatch())
        {
            QString qAppStyle = match.captured(1);
            re.setPattern(R"(([\w-]+)\s*:\s*([^;}]+))");
            QRegularExpressionMatchIterator i = re.globalMatch(qAppStyle);
            while (i.hasNext())
            {
                QRegularExpressionMatch match = i.next();
                if (match.captured(1) == "font-family")
                    f.setFamily(match.captured(2).trimmed());
                else if (match.captured(1) == "font-size")
                {
                    QString val = match.captured(2).trimmed();
                    double size = strtod(val.toStdString().c_str(), nullptr);
                    if (size)
                    {
                        if (val.endsWith("px"))
                            f.setPixelSize(int(size));
                        else
                            f.setPointSizeF(size);
                    }
                }
            }
            app->setFont(f);
        }
        app->setStyleSheet(appStyle);
    }
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
