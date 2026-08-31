#include "rowjson.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <QAbstractItemModel>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QStringList>
#include <algorithm>

namespace
{

/// json string escaping, per RFC 8259. Control characters must be escaped; the
/// short forms are used where they exist so the result stays readable.
QString quote(const QString &text)
{
    QString res;
    res.reserve(text.length() + 2);
    res += '"';
    for (const QChar &c: text)
    {
        switch (c.unicode())
        {
        case '"':  res += "\\\""; break;
        case '\\': res += "\\\\"; break;
        case '\b': res += "\\b"; break;
        case '\f': res += "\\f"; break;
        case '\n': res += "\\n"; break;
        case '\r': res += "\\r"; break;
        case '\t': res += "\\t"; break;
        default:
            if (c.unicode() < 0x20)
                res += QString("\\u%1").arg(uint(c.unicode()), 4, 16, QChar('0'));
            else
                res += c;
        }
    }
    res += '"';
    return res;
}

/// Re-indents a json document's own multi-line output so it can sit as a value
/// inside a bigger document: every line but the first gets \a indent spaces.
QString reindent(const QString &text, int indent)
{
    if (!indent)
        return text;
    const QString pad(indent, ' ');
    QStringList lines = text.split('\n');
    for (int i = 1; i < lines.size(); ++i)
        lines[i].prepend(pad);
    return lines.join('\n');
}

/// The document \a text describes, indented, or a null string when it is not
/// json. Line breaks are kept out of the way first: QJsonDocument::fromJson()
/// accepts formatted json, but a value pulled out of a cell may be wrapped in
/// ways it does not (see the existing viewer's own comment).
QString embeddedDocument(const QString &text, bool anyValue)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return QString();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && !doc.isNull())
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).trimmed();

    if (!anyValue)
        return QString();

    // A json column may legitimately hold a bare scalar (`123`, `"text"`,
    // `true`, `null`), which QJsonDocument refuses to parse on its own - it
    // only accepts an object or an array at the top level. Wrap it to find
    // out, then unwrap the answer.
    QJsonDocument probe = QJsonDocument::fromJson("[" + trimmed.toUtf8() + "]", &err);
    if (err.error == QJsonParseError::NoError && probe.isArray() && probe.array().size() == 1)
        return trimmed;

    return QString();
}

} // namespace

namespace RowJson
{

bool isJsonDocument(const QString &text)
{
    const QString trimmed = text.trimmed();
    // Cheap gate first: only an object or an array counts as "this cell holds
    // json", and both are recognisable from their first character. It also
    // keeps a plain numeric or quoted string from being called json.
    if (!trimmed.startsWith('{') && !trimmed.startsWith('['))
        return false;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &err);
    return (err.error == QJsonParseError::NoError && !doc.isNull());
}

Kind kindForTypeName(const QString &typeName)
{
    QString name = typeName.trimmed().toLower();

    // An array is printed by the dbms in its own syntax, not as json: pg gives
    // `{1,2,3}` and `{}` for an empty one. `{}` happens to be valid json, so
    // letting it be guessed would show an empty array as an empty *object* while
    // a non-empty one stayed text. Always a string, whatever the element type.
    // Checked before the modifier is stripped, since that sits in between:
    // "numeric(10,2)[]".
    if (name.endsWith("[]"))
        return Kind::Text;

    // The name may carry a modifier ("numeric(10,2)"); compare on the bare name.
    const int paren = name.indexOf('(');
    if (paren > 0)
        name.truncate(paren);
    name = name.trimmed();

    if (name == "json" || name == "jsonb")
        return Kind::Json;

    // Arbitrary-precision decimals, both dialects' spellings. float4/float8 are
    // deliberately absent: they are doubles already and print as numbers.
    if (name == "numeric" || name == "decimal" || name == "money")
        return Kind::Numeric;

    return Kind::Auto;
}

QString valueText(const Cell &cell, int indent)
{
    if (!cell.value.isValid() || cell.value.isNull())
        return QStringLiteral("null");

    switch (cell.kind)
    {
    case Kind::Numeric:
        // Exactly as the server printed it, as a string - a json number would
        // go through a double and lose digits.
        return quote(cell.value.toString());

    case Kind::Text:
        // Never looked into: an array's `{}` would otherwise read as an object.
        return quote(cell.value.toString());

    case Kind::Json:
    {
        const QString doc = embeddedDocument(cell.value.toString(), true);
        // A json column holding something unparsable is still worth seeing, so
        // it falls back to the raw text rather than breaking the document.
        return (doc.isNull() ? quote(cell.value.toString()) : reindent(doc, indent));
    }

    case Kind::Auto:
        break;
    }

    switch (cell.value.typeId())
    {
    case QMetaType::Bool:
        return (cell.value.toBool() ? QStringLiteral("true") : QStringLiteral("false"));

    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        // Int64 survives json intact (verified), so these print as numbers.
        return cell.value.toString();

    case QMetaType::Double:
    case QMetaType::Float:
    {
        // NaN and +-Infinity are not json numbers - QJsonDocument itself writes
        // them out as null, which would hide the value. Keep them as strings so
        // what the server returned stays visible.
        const double d = cell.value.toDouble();
        if (qIsNaN(d) || qIsInf(d))
            return quote(cell.value.toString());
        return cell.value.toString();
    }

    // Dates and timestamps: the pg backend keeps the server's own textual
    // representation (it uses the text protocol and stores these as strings, to
    // avoid QDateTime's microsecond and range limits), so those arrive here as
    // strings and pass through untouched. A real QDate/QTime/QDateTime only
    // comes from odbc, which decodes into the Qt types - rendered as ISO-8601,
    // no locale involved.
    case QMetaType::QDate:
        return quote(cell.value.toDate().toString(Qt::ISODate));
    case QMetaType::QTime:
        return quote(cell.value.toTime().toString(Qt::ISODateWithMs));
    case QMetaType::QDateTime:
        return quote(cell.value.toDateTime().toString(Qt::ISODateWithMs));

    default:
        break;
    }

    const QString text = cell.value.toString();
    // Text that is really a json document is expanded into a nested node, so a
    // json value stored in a text column reads like the json it is instead of
    // one long escaped line. Anything else - including text that merely starts
    // with a brace - stays a quoted string, which is why it is parsed first.
    if (isJsonDocument(text))
    {
        const QString doc = embeddedDocument(text, false);
        if (!doc.isNull())
            return reindent(doc, indent);
    }
    return quote(text);
}

QString objectText(const Row &row)
{
    if (row.isEmpty())
        return QStringLiteral("{}");

    QString res = QStringLiteral("{\n");
    for (int i = 0; i < row.size(); ++i)
    {
        // A nested document's continuation lines are indented one level in from
        // the key, not aligned past it: aligning under a long column name pushes
        // the value into a ragged column far to the right, which is exactly the
        // sideways reading this feature exists to avoid.
        res += QStringLiteral("    ") + quote(row[i].name) + QStringLiteral(": ")
                + valueText(row[i], 4);
        if (i != row.size() - 1)
            res += ',';
        res += '\n';
    }
    res += '}';
    return res;
}

QString arrayText(const QVector<Row> &rows)
{
    if (rows.isEmpty())
        return QStringLiteral("[]");

    QString res = QStringLiteral("[\n");
    for (int i = 0; i < rows.size(); ++i)
    {
        res += reindent(QStringLiteral("    ") + objectText(rows[i]), 4);
        if (i != rows.size() - 1)
            res += ',';
        res += '\n';
    }
    res += ']';
    return res;
}

namespace
{

/// One cell of \a model, described for the renderers above.
Cell cellAt(const QAbstractItemModel *model, int row, int column, const TypeNameFn &typeName)
{
    Cell cell;
    cell.name = model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
    // A computed column may come back unnamed; its position still identifies it
    if (cell.name.isEmpty())
        cell.name = QString::number(column + 1);
    // EditRole is the value as the model holds it, before any display
    // formatting - which is what should be inspected here.
    cell.value = model->index(row, column).data(Qt::EditRole);
    cell.kind = (typeName ? kindForTypeName(typeName(column)) : Kind::Auto);
    return cell;
}

} // namespace

QString forSelection(const QAbstractItemModel *model,
                     QModelIndexList indexes,
                     const TypeNameFn &typeName,
                     bool *preformatted)
{
    if (preformatted)
        *preformatted = false;
    if (!model)
        return QString();

    // Sorted so that a row's cells come in the query's own column order, and
    // the rows in the order they are displayed. QModelIndex sorts by row first.
    std::sort(indexes.begin(), indexes.end());

    if (indexes.size() == 1)
    {
        const QModelIndex index = indexes.first();
        if (!index.isValid())
            return QString();

        // A cell holding json is shown on its own: that is the value the user
        // is after, and the surrounding columns would only get in the way.
        const QString cellText = index.data(Qt::EditRole).toString();
        if (isJsonDocument(cellText))
            return cellText; // not preformatted: the viewer may reformat it

        // Anything else: the whole row, which is the readable way to study a
        // wide row without scrolling it sideways.
        Row row;
        const int columns = model->columnCount();
        row.reserve(columns);
        for (int c = 0; c < columns; ++c)
            row.append(cellAt(model, index.row(), c, typeName));

        if (preformatted)
            *preformatted = true;
        return objectText(row);
    }

    // Several cells: only those, grouped by row.
    QVector<Row> rows;
    int currentRow = -1;
    for (const QModelIndex &i: indexes)
    {
        if (!i.isValid())
            continue;
        if (i.row() != currentRow)
        {
            currentRow = i.row();
            rows.append(Row());
        }
        rows.last().append(cellAt(model, i.row(), i.column(), typeName));
    }

    if (rows.isEmpty())
        return QString();

    if (preformatted)
        *preformatted = true;
    // One row's worth of cells reads as an object; several rows as an array of
    // them, one per row.
    return (rows.size() == 1 ? objectText(rows.first()) : arrayText(rows));
}

} // namespace RowJson
