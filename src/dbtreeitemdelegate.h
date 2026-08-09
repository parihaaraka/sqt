#ifndef DBTREEITEMDELEGATE_H
#define DBTREEITEMDELEGATE_H

#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QProxyStyle>
#include <QPainter>
#include <QApplication>
#include <QFileInfo>

class DbTreeItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit DbTreeItemDelegate(QObject *parent = nullptr);
    virtual void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    virtual QSize sizeHint(const QStyleOptionViewItem & option, const QModelIndex & index) const;
    void prepareDocToDrawDbTreeNode(const QStyleOptionViewItem & option, const QModelIndex & index, QTextDocument &doc) const;

};

class MyProxyStyle : public QProxyStyle
{
private:
    QIcon _closed, _open;

public:
    MyProxyStyle(QStyle *style = nullptr);
    /// Re-reads the branch arrows through the current resource roots. Called by
    /// the constructor, and again when the assets directory changes - the style
    /// is installed on the view and outlives such a change.
    void loadIcons();
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const;

};

#endif // DBTREEITEMDELEGATE_H
