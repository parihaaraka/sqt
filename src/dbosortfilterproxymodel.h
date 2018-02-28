#ifndef DBOSORTFILTERPROXYMODEL_H
#define DBOSORTFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>

class DboSortFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit DboSortFilterProxyModel(QObject *parent = 0);
    bool compare(const QVariant &varl, const QVariant &varr) const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const;

signals:
    
public slots:
    
};

#endif // DBOSORTFILTERPROXYMODEL_H
