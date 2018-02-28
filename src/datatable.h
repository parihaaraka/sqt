#ifndef DATATABLE_H
#define DATATABLE_H

#include <QtGlobal>
#include <QVariant>
#include <QVector>
#include <QMetaType>
#include <QMutex>

class DataTable;

class DataColumn
{
public:
    DataColumn(const DataColumn &column) = default;
    DataColumn(QString _col_name, QMetaType::Type type, int sql_type, int size, int16_t _dec_digits, int8_t _nullable_desc, Qt::AlignmentFlag hAlignment);
    QMetaType::Type variantType() { return _var_type; }
    int sqlType() { return _sql_type; }
    QString name() { return _col_name; }
    Qt::AlignmentFlag hAlignment() { return _hAlignment; }
private:
    QString _col_name;
    QMetaType::Type _var_type;
    int _sql_type;
    int _col_size;
    int16_t _dec_digits;
    int8_t _nullable_desc;
    Qt::AlignmentFlag _hAlignment;
};

class DataRow
{
    friend class DataTable;
public:
    DataRow(const DataRow &row);
    DataRow(DataTable* _table);
    ~DataRow();
    QVariant& operator[](QString column_name);
    QVariant& operator[](int index);
private:
    QVector<QVariant> _row;
    DataTable* _table;
};

class DataTable : public QObject
{
    Q_OBJECT
public:
    DataTable(const DataTable &table);
    DataTable(QObject *parent = nullptr);
    ~DataTable();
	void clear();
    DataColumn& addColumn(QString col_name, QMetaType::Type type, int sql_type, int size, int16_t dec_digits, int8_t nullable_desc, Qt::AlignmentFlag hAlignment);
    DataRow& addRow();
    void addColumn(DataColumn *column);
    void addRow(DataRow *row);
    DataColumn& getColumn(QString column_name) const;
    DataColumn& getColumn(int ord) const;
    int getColumnOrd(QString column_name) const;
    DataRow& getRow(int ind) const;
    mutable QMutex mutex;
public slots:
    int columnCount() const;
    int rowCount() const;
    QVariant value(int row, int column) const;
    QVariant value(int row, QString columnName) const;
    DataTable* takeRows(DataTable *source);
private:
    QVector<DataColumn*> _columns;
    QVector<DataRow*> _rows;
};

Q_DECLARE_METATYPE(DataTable)

#endif // DATATABLE_H
