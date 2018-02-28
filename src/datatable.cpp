#include "datatable.h"
#include <QApplication>
#include <QDebug>

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

int DataTable::columnCount() const
{
    return _columns.size();
}

int DataTable::rowCount() const
{
    QMutexLocker locker(&mutex);
    return _rows.size();
}

QVariant DataTable::value(int row, int column) const
{
    if (column >= 0 && column < _columns.size() &&
            row >= 0 && row < rowCount())
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

DataColumn& DataTable::addColumn(QString col_name, QMetaType::Type type, int sql_type, int size, int16_t dec_digits, int8_t nullable_desc, Qt::AlignmentFlag hAlignment)
{
    DataColumn* new_col = new DataColumn(col_name, type, sql_type, size, dec_digits, nullable_desc, hAlignment);
    _columns.append(new_col);
	return *new_col;
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
    QMutexLocker locker(&mutex);
    row->_table = this;
    _rows.append(row);
}

DataTable* DataTable::takeRows(DataTable *source)
{
    if (!source || this == source)
        return this;

    if (_columns.isEmpty())
    {
        for (const DataColumn *c: source->_columns)
            _columns.append(new DataColumn(*c));
    }

    // deadlock conditions are improbable in this application
    QMutexLocker src_locker(&source->mutex);
    QMutexLocker dst_locker(&mutex);
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

DataColumn::DataColumn(QString col_name, QMetaType::Type type, int sql_type, int size, int16_t dec_digits, int8_t nullable_desc, Qt::AlignmentFlag hAlignment) :
    _col_name(col_name), _var_type(type), _sql_type(sql_type), _col_size(size), _dec_digits(dec_digits), _nullable_desc(nullable_desc), _hAlignment(hAlignment)
{
}


