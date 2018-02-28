#include "dbosortfilterproxymodel.h"
#include "dbobject.h"

DboSortFilterProxyModel::DboSortFilterProxyModel(QObject *parent) :
    QSortFilterProxyModel(parent)
{
}

bool DboSortFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceRow)
    Q_UNUSED(sourceParent)
    return true;
}

bool DboSortFilterProxyModel::lessThan(const QModelIndex &left,
                                      const QModelIndex &right) const
{
    /*
    // nodes with id are always first
    QVariant leftId = sourceModel()->data(left, DbObject::IdRole);
    QVariant rightId = sourceModel()->data(right, DbObject::IdRole);
    if (leftId.isValid() && !rightId.isValid())
        return true;
    if (!leftId.isValid() && rightId.isValid())
        return false;
    */

    QVariant sortMode = sourceModel()->data(left.parent(), DbObject::CurrentSortRole);
    int mode = (sortMode.isValid() ? sortMode.toInt() : 0);

    QVariant leftS1 = sourceModel()->data(left, DbObject::Sort1Role);
    QVariant leftS2 = sourceModel()->data(left, DbObject::Sort2Role);
    QString leftN = sourceModel()->data(left, Qt::DisplayRole).toString().toLower();
    QVariant rightS1 = sourceModel()->data(right, DbObject::Sort1Role);
    QVariant rightS2 = sourceModel()->data(right, DbObject::Sort2Role);
    QString rightN = sourceModel()->data(right, Qt::DisplayRole).toString().toLower();

    if (mode == 0)
    {
        if (leftS1.isValid() && rightS1.isValid())
            return compare(leftS1, rightS1);
    }
    else
    {
        if (leftS2.isValid() && rightS2.isValid())
            return compare(leftS2, rightS2);
    }
    return leftN < rightN;
}

bool DboSortFilterProxyModel::compare(const QVariant &varl, const QVariant &varr) const
{
    bool ok_l, ok_r;
    qlonglong long_varl = varl.toLongLong(&ok_l);
    qlonglong long_varr = varr.toLongLong(&ok_r);
    if (ok_l && ok_r)
        return long_varl < long_varr;

    return varl.toString().toLower() < varr.toString().toLower();
}
