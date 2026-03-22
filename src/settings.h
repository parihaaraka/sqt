#ifndef SETTINGS_H
#define SETTINGS_H

#include "qjsonvalue.h"
#include "qtextformat.h"
#include <QVariant>

struct RecentFile
{
    QString fileName;
    QString encoding;
};
Q_DECLARE_METATYPE(QList<RecentFile>)

namespace SqtSettings
{

void load();
QVariant value(const QString &name, const QVariant &defaultValue = QVariant());
void setValue(const QString &key, const QVariant &value);

} // namespace SqtSettings

QTextCharFormat hlFormat(
        const QJsonValue &settings,
        const QVariant &prop,
        const QColor &defForeground,
        bool bold = false,
        bool italic = false);

#endif // SETTINGS_H
