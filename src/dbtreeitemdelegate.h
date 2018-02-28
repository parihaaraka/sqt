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
    explicit DbTreeItemDelegate(QObject *parent = 0);
    virtual void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    virtual QSize sizeHint(const QStyleOptionViewItem & option, const QModelIndex & index) const;
    void prepareDoc(const QStyleOptionViewItem & option, const QModelIndex & index, QTextDocument &doc) const;

};

class MyProxyStyle : public QProxyStyle
{
private:
    QIcon _closed, _open;

public:
    MyProxyStyle(QStyle *style = 0) : QProxyStyle(style)
    {
        Q_UNUSED(style)

        QFileInfo fi_c(QApplication::applicationDirPath() + "/decor/branch-closed.png");
        QFileInfo fi_o(QApplication::applicationDirPath() + "/decor/branch-open.png");
        if (fi_c.exists() && fi_o.exists())
        {
            _closed = QIcon(fi_c.absoluteFilePath());
            _open = QIcon(fi_o.absoluteFilePath());
        }
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = 0) const
    {
        if (element == QStyle::PE_IndicatorBranch)
        {
            painter->save();
            painter->fillRect(option->rect.adjusted(0, 0, 1, 1), painter->background());
            if (_closed.isNull() || _open.isNull())
                QProxyStyle::drawPrimitive(element, option, painter, widget);
            else
            {
                if (option->state & QStyle::State_Children)
                {
                    if (option->state & QStyle::State_Open)
                        _open.paint(painter, option->rect, Qt::AlignCenter | Qt::AlignVCenter);
                    else
                        _closed.paint(painter, option->rect, Qt::AlignCenter | Qt::AlignVCenter);
                }
            }
            painter->restore();
        }
        else QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

#endif // DBTREEITEMDELEGATE_H
