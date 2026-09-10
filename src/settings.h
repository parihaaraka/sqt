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

/// The current indentation setting ("tabsIndent"/"indentSize") rendered as
/// literal text - a tab, or \c indentSize spaces (see also
/// CodeEditor::indentSize(), which covers the same setting for code that
/// already has an editor to ask). For code that has no editor at hand and
/// just needs to build an indented line by itself - the routine-signature
/// auto-split and the editor's "split list" command share this rather than
/// each reading the two keys on their own.
QString indentUnit();

QTextCharFormat hlFormat(
        const QJsonValue &settings,
        const QVariant &prop,
        const QColor &defForeground,
        bool bold = false,
        bool italic = false);

#endif // SETTINGS_H
