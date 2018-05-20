#ifndef DATATABLE_H
#define DATATABLE_H

#include <QtGlobal>
#include <QVariant>
#include <QVector>
#include <QMetaType>
#include <QMutex>

class DataTable;
// TODO
// rethink this mostly legacy code
class DataColumn
{
public:
    DataColumn(const DataColumn &column) = default;
    DataColumn(const QString &name, const QString typeName, QMetaType::Type type, int sqlType, int length, int16_t decDigits, int8_t nullableDesc, Qt::AlignmentFlag hAlignment, int arrayElementType = -1);

    // ctor for postgres only for delayed column type specification (it's about arrays)
    DataColumn(const QString &name, QMetaType::Type type, int sqlType, int modifier, int8_t nullableDesc, Qt::AlignmentFlag hAlignment);
    void clarifyType(const QString &type, int length = -1, int16_t decDigits = -1, int arrayElementType = -1);

    QMetaType::Type variantType() const noexcept { return _varType; }
    int sqlType() const noexcept { return _sqlType; }
    int arrayElementType() const noexcept { return _arrayElementType; }
    QString name() const noexcept { return _name; }
    Qt::AlignmentFlag hAlignment() const noexcept { return _hAlignment; }
    int length() const noexcept { return _length; }
    int16_t scale() const noexcept { return _decDigits; }
    int modifier() const noexcept { return _modifier; }
    QString typeName() const noexcept { return _typeName; }

private:
    QString _name, _typeName;
    QMetaType::Type _varType;
    int _sqlType;
    int _length = -1;
    int _modifier = -1;
    int16_t _decDigits = -1;
    int8_t _nullableDesc = 1;
    Qt::AlignmentFlag _hAlignment = Qt::AlignLeft;
    int _arrayElementType = -1;
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
