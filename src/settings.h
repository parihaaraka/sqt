#ifndef SETTINGS_H
#define SETTINGS_H

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

#endif // SETTINGS_H
