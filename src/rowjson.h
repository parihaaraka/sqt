#ifndef ROWJSON_H
#define ROWJSON_H

#include <QString>
#include <QVariant>
#include <QVector>
#include <QModelIndexList>
#include <functional>

class QAbstractItemModel;

/// Resultset rows as json text - what Ctrl+J shows for a grid selection.
///
/// Written as text instead of being built through QJsonObject on purpose:
/// QJsonObject keeps its keys sorted alphabetically, and the whole point here is
/// to read a row the way the query selected it, column order included.
///
/// No gui and no dbms dependency (QAbstractItemModel is QtCore), so the rules
/// below are unit-testable (tests/tst_rowjson.cpp).
namespace RowJson
{

/// How a value is to be rendered.
enum class Kind
{
    /// Decide from the variant: text stays text (a numeric-looking string is
    /// never turned into a json number), a number stays a number. Text that
    /// looks like a json object or array - it starts with '{' or '[' - is
    /// embedded as a nested node if it really parses, and left as a plain
    /// string if it does not.
    Auto,
    /// The column's dbms type is json/jsonb, so the value is embedded whatever
    /// its shape - a bare `123` or `"text"` is a json value too. Still only
    /// when it parses: a column can be json and hold something broken.
    Json,
    /// Arbitrary-precision numeric: always a json string. A double would
    /// silently round it, and this is a tool for people who care.
    Numeric,
    /// Always a quoted string, never looked into. This is what an array column
    /// gets: postgres prints an array as `{1,2,3}` (and an empty one as `{}`,
    /// which *is* valid json), so guessing would turn some arrays into objects
    /// and leave others as text - the same value looking different from one row
    /// to the next.
    Text,
};

struct Cell
{
    QString name;
    QVariant value;
    Kind kind = Kind::Auto;
};

using Row = QVector<Cell>;

/// One row as a json object - one key per cell, column order preserved.
QString objectText(const Row &row);

/// Several rows as a json array of objects.
QString arrayText(const QVector<Row> &rows);

/// A single cell's value as json text, without its key. Multi-line output (an
/// embedded object) is indented relative to \a indent, so that it lines up
/// under the key it belongs to.
QString valueText(const Cell &cell, int indent = 0);

/// Whether \a text is a complete json *document* - an object or an array.
/// This is what decides that a cell holds json and is worth showing on its own.
bool isJsonDocument(const QString &text);

/// The kind implied by a column's dbms type name ("jsonb", "numeric(10,2)").
/// A name-based hint on purpose: the type name is the one description both
/// backends fill in, while the numeric ids behind it are dbms-specific.
Kind kindForTypeName(const QString &typeName);

/// The dbms type name of a column, asked of the caller - the model itself does
/// not carry it (see AppEventHandler, which reads it off the DataTable).
using TypeNameFn = std::function<QString(int column)>;

/// What Ctrl+J shows for \a indexes, the grid's current selection:
///
/// - a single cell holding json: that json, on its own;
/// - a single cell that does not: the whole row as an object, so a wide row can
///   be read without scrolling sideways;
/// - several cells of one row: an object of just those cells;
/// - several cells across rows: an array of one object per row.
///
/// \a indexes need not be sorted. Returns a null string when there is nothing
/// to show. \a preformatted tells the caller the result is already json text
/// and must not be reformatted (which would sort the keys).
QString forSelection(const QAbstractItemModel *model,
                     QModelIndexList indexes,
                     const TypeNameFn &typeName,
                     bool *preformatted = nullptr);

}

#endif // ROWJSON_H
