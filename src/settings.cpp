#include "settings.h"
#include "qfileinfo.h"
#include "qjsondocument.h"
#include "qjsonobject.h"
#include <QSettings>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QRegularExpression>
#include <QFont>
#include "styling.h"
#include "misc.h"

namespace SqtSettings
{

static QMutex _m;
static QHash<QString, QVariant> _settings;

void load()
{
    qRegisterMetaType<QList<RecentFile>>();
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

    // The single-byte ones come from textcodec.cpp rather than from Qt, so this
    // list no longer depends on the Qt version.
    const QString encodings = settings.value("encodings").toString();
    if (encodings.isEmpty() ||
        // Qt 6 builds before the codec tables existed could not offer these and
        // saved a poorer list of their own; replace exactly that one, so that a
        // list the user has edited is left alone.
        encodings == "UTF-8,UTF-16,ISO-8859-1")
        setDefault("encodings", "UTF-8,windows-1251,UTF-16LE,cp866");


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
    int indentSize = settings.value("indentSize", -1).toInt(&ok);
    if (!ok || indentSize <= 0)
        setDefault("indentSize", 3);

    locker.unlock();
    QApplication *app = qobject_cast<QApplication*>(QGuiApplication::instance());
    if (app)
    {
        QFont f;
        f.setFamily(f.defaultFamily()); // restore system font if font-family is not specified

        // QApplication does not support font styling via setStyleSheet(),
        // so lets do it manually
        static QRegularExpression re(R"(QApplication\s*{([^}]+))");
        QRegularExpressionMatch match = re.match(appStyle);
        if (match.hasMatch())
        {
            QString qAppStyle = match.captured(1);
            re.setPattern(R"(([\w-]+)\s*:\s*([^;}]+))");
            QRegularExpressionMatchIterator i = re.globalMatch(qAppStyle);
            while (i.hasNext())
            {
                QRegularExpressionMatch match2 = i.next();
                if (match2.captured(1) == "font-family")
                    f.setFamily(match2.captured(2).trimmed());
                else if (match2.captured(1) == "font-size")
                {
                    QString val = match2.captured(2).trimmed();
                    // Locale-independent: strtod() here followed the system
                    // locale, so a "font-size: 9.5pt" was read as 9 wherever
                    // LC_NUMERIC uses a comma. A stylesheet always writes '.'.
                    const std::string valUtf8 = val.toStdString();
                    double size = parseDouble(valUtf8.c_str());
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

QTextCharFormat hlFormat(const QJsonValue &node, const QVariant &prop, const QColor &defForeground, bool bold, bool italic)
{
    QTextCharFormat format;
    format.setProperty(QTextFormat::UserProperty, prop);
    format.setForeground(defForeground);
    format.setFontItalic(italic);
    format.setFontWeight(bold ? QFont::Bold : QFont::Normal);
    if (node.isObject())
    {
        auto obj = node.toObject();
        QJsonValue fg = obj[isDarkMode() ? "foreground_dark" : "foreground_light"];
        if (fg.type() != QJsonValue::String)
            fg = obj["foreground"];
        if (fg.type() == QJsonValue::String)
        {
            QColor c(fg.toString());
            if (c.isValid())
                format.setForeground(c);
        }
        format.setFontItalic(obj["italic"].toBool(italic));
        format.setFontWeight(obj["bold"].toBool(bold) ?
                    QFont::Bold : QFont::Normal);
    }
    return format;
}
