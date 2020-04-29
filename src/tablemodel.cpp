#include "datatable.h"
#include "tablemodel.h"
#include <QBrush>
#include <QDateTime>

TableModel::TableModel(QObject *parent) :
    QAbstractItemModel(parent)
{
    _table = new DataTable();
}

TableModel::~TableModel()
{
    delete _table;
}

QModelIndex TableModel::parent(const QModelIndex &) const
{
    return QModelIndex();
}

int TableModel::rowCount(const QModelIndex &) const
{
    return _table->rowCount();
}

int TableModel::columnCount(const QModelIndex &) const
{
    return _table->columnCount();
}

QModelIndex TableModel::index(int row, int column, const QModelIndex &) const
{
    return createIndex(row, column);
}

QVariant TableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();
    //int sqlType = _table->getColumn(index.column()).sqlType();
    switch (role)
    {
    case Qt::SizeHintRole:
    {
        // keep default cell width convenient to use
        QVariant &res = _table->getRow(index.row())[index.column()];
        if (!res.isNull() && res.toString().length() > 100)
            return QSize(500, -1);
        return QVariant();
    }
    case Qt::TextAlignmentRole:
        return int(_table->getColumn(index.column()).hAlignment());// | Qt::AlignVCenter);
    case Qt::BackgroundRole:
        if (_table->getRow(index.row())[index.column()].isNull())
            return QBrush(QColor(0, 0, 0, 15));
        return QVariant();
    case Qt::DisplayRole:
    {
        QVariant &res = _table->getRow(index.row())[index.column()];
        QMetaType::Type type = QMetaType::Type(res.type());
        if (type == QMetaType::QTime)
        {
            return qvariant_cast<QTime>(res).toString("hh:mm:ss.zzz");
        }
        else if (type == QMetaType::QDate)
        {
            return qvariant_cast<QDate>(res).toString("yyyy-MM-dd");
        }
        else if (type == QMetaType::QDateTime)
        {
            QDateTime dt = qvariant_cast<QDateTime>(res);
            if (dt.time().msecsTo(QTime(0, 0)) == 0)
                return dt.toString("yyyy-MM-dd");
            return dt.toString("yyyy-MM-dd hh:mm:ss.zzz");
        }
    }
    [[fallthrough]];
    case Qt::EditRole:
        return _table->getRow(index.row())[index.column()];
    }
    return QVariant();
}

Qt::ItemFlags TableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags(index);
}

QVariant TableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (section < 0)
        return QVariant();

    if (orientation == Qt::Horizontal)
    {
        if (role == Qt::ToolTipRole)
        {
            QString colType = _table->getColumn(section).typeName();
            return (colType.isEmpty() ? QVariant() : colType);
        }
        return (role == Qt::DisplayRole ? _table->getColumn(section).name() : QVariant());
    }

    if (role == Qt::TextAlignmentRole)
        return int(Qt::AlignRight);// | Qt::AlignVCenter);

    // first row header cotains total numer of rows
    if (role == Qt::ToolTipRole && !section)
        return tr("total number of rows");
    if (role == Qt::DisplayRole)
        return (section || rowCount() < 5 ?
                    QString::number(section + 1):
                    "↓" + QString::number(rowCount()));

    return QVariant();
}

void TableModel::take(DataTable *srcTable)
{
    // the columns do not alter in parallel - no need to use mutex
    if (srcTable->columnCount() != columnCount())
    {
        clear();
        beginInsertColumns(QModelIndex(), 0, srcTable->columnCount() - 1);
        for (int c = 0; c < srcTable->columnCount(); ++c)
            _table->addColumn(new DataColumn(srcTable->getColumn(c)));
        endInsertColumns();
    }
    QMutexLocker srcLocker(&srcTable->mutex);
    int rows = srcTable->rowCount();
    if (rows)
    {
        QMutexLocker dstLocker(&_table->mutex);
        int rowcount = _table->rowCount();
        beginInsertRows(QModelIndex(), rowcount, rowcount + rows - 1);
        _table->takeRows(srcTable);
        endInsertRows();
    }
}

void TableModel::clear()
{
    beginResetModel();
    _table->clear();
    endResetModel();
}
