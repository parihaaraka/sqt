#include "datatable.h"
#include <QApplication>

DataTable::DataTable(const DataTable &table) : QObject()
{
    for(const DataColumn *c: table._columns)
        _columns.append(new DataColumn(*c));
    for(const DataRow *r: table._rows)
    {
        _rows.append(new DataRow(*r));
        _rows.last()->_table = this;
    }
}

DataTable::DataTable(QObject *parent): QObject(parent)
{
}

DataTable::~DataTable()
{
    clear();
}

void DataTable::clear()
{
    qDeleteAll(_rows);
    _rows.clear();
    qDeleteAll(_columns);
    _columns.clear();
}

void DataTable::clearRows()
{
    qDeleteAll(_rows);
    _rows.clear();
}

int DataTable::columnCount() const
{
    return _columns.size();
}

int DataTable::rowCount() const
{
    return _rows.size();
}

QVariant DataTable::value(int row, int column) const
{
    if (column >= 0 && column < _columns.size() &&
            row >= 0 && row < _rows.size())
        return (*_rows[row])[column];
    return QVariant();
}

QVariant DataTable::value(int row, QString columnName) const
{
    return value(row, getColumnOrd(columnName));
}

DataRow& DataTable::getRow(int ind) const
{
    return *_rows.at(ind);
}

DataRow& DataTable::addRow()
{
    DataRow* new_row = new DataRow(this);
    _rows.append(new_row);
    return *new_row;
}

void DataTable::addColumn(DataColumn *column)
{
    _columns.append(column);
}

void DataTable::addRow(DataRow *row)
{
    // mutex lock is removed in favoir of outermost usage
    row->_table = this;
    _rows.append(row);
}

DataTable* DataTable::takeRows(DataTable *source)
{
    if (!source || this == source)
        return this;

    if (_columns.isEmpty())
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
        for (const DataColumn *c: qAsConst(source->_columns))
#else
        for (const DataColumn *c: std::as_const(source->_columns))
#endif
            _columns.append(new DataColumn(*c));
    }

    _rows.append(source->_rows);
    source->_rows.clear();
    return this;
}

DataColumn& DataTable::getColumn(int ord) const
{
    return *_columns.at(ord);
}

DataColumn& DataTable::getColumn(QString column_name) const
{
    return *_columns.at(getColumnOrd(column_name));
}

DataRow::DataRow(const DataRow &row)
{
    _table = row._table;
    _row = row._row;
}

DataRow::DataRow(DataTable* table)
{
    _table = table;
    for (int i = 0; i < table->columnCount(); ++i)
	{
        _row.append(QVariant());
	}
}

DataRow::~DataRow()
{
    _row.clear();
}

int DataTable::getColumnOrd(QString column_name) const
{
    for (int i = 0; i < columnCount(); ++i)
	{
        if (_columns.at(i)->name() == column_name)
            return i;
	}
	return -1;
}

QVariant& DataRow::operator [](QString column_name)
{
    return _row[_table->getColumnOrd(column_name)];
}

QVariant& DataRow::operator [](int index)
{
    return _row[index];
}

DataColumn::DataColumn(const QString &name, const QString &typeName, QMetaType::Type type, int sqlType, int length, int16_t decDigits, int8_t nullableDesc, Qt::AlignmentFlag hAlignment, int arrayElementType) :
    _name(name), _typeName(typeName), _varType(type), _sqlType(sqlType), _length(length), _decDigits(decDigits), _nullableDesc(nullableDesc), _hAlignment(hAlignment), _arrayElementType(arrayElementType)
{
}

DataColumn::DataColumn(const QString &name, QMetaType::Type type, int sqlType, int modifier, int8_t nullableDesc, Qt::AlignmentFlag hAlignment) :
    _name(name), _varType(type), _sqlType(sqlType), _modifier(modifier), _nullableDesc(nullableDesc), _hAlignment(hAlignment)
{
}

void DataColumn::clarifyType(const QString &type, int length, int16_t decDigits, int arrayElementType)
{
    _typeName = type;
    _length = length;
    _decDigits = decDigits;
    _arrayElementType = arrayElementType;
}


