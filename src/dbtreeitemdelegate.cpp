#include "dbtreeitemdelegate.h"
#include <QPainter>
#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QTreeView>
#include <QSortFilterProxyModel>
#include "dbobject.h"
#include "dbconnection.h"
#include "dbconnectionfactory.h"

DbTreeItemDelegate::DbTreeItemDelegate(QObject *parent) :
    QStyledItemDelegate(parent)
{
}

void DbTreeItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem style(option);
    initStyleOption(&style, index);

    auto isCurrent = qobject_cast<const QAbstractItemView*>(option.widget)->currentIndex() == index;
    if (isCurrent || style.state & (QStyle::State_HasFocus | QStyle::State_Selected))
    {
        QRect selFrame = option.rect;
        selFrame.setRight(selFrame.right() - 1);
        selFrame.setHeight(selFrame.height() - 1);
        painter->save();
        QColor hl = option.widget->palette().color(QPalette::Highlight);
        hl.setAlpha(style.state & QStyle::State_HasFocus ? 230 : 90);   // frame
        QPen pen(hl, 0);
        painter->setPen(pen);
        QLinearGradient gradient(0, option.rect.top(), 0, option.rect.bottom());
        int dAlpha = (style.state & QStyle::State_Selected ? 50 : 0)
                + (style.state & QStyle::State_HasFocus ? 20 : 0);
        hl.setAlpha(10 + dAlpha);
        gradient.setColorAt(0.3, hl);
        hl.setAlpha(30 + dAlpha);
        gradient.setColorAt(1, hl);
        painter->setBrush(QBrush(gradient));
        painter->drawRect(selFrame);
        painter->restore();
    }

    style.text = QString();
    style.state &= ~(QStyle::State_HasFocus | QStyle::State_Selected);

    QVariant icon = index.data(Qt::DecorationRole);
    if (icon.isValid())
    {
        QIcon i = qvariant_cast<QIcon>(icon);
        i.paint(painter, option.rect, Qt::AlignLeft | Qt::AlignVCenter);

        // draw connection state indicator near the icon
        const QModelIndex currentNodeIndex = qobject_cast<const QSortFilterProxyModel*>(index.model())->mapToSource(index);
        DbObject *obj = static_cast<DbObject*>(currentNodeIndex.internalPointer());
        QString type = obj->data(DbObject::TypeRole).toString();
        if (type == "connection" || type == "database")
        {
            auto con = DbConnectionFactory::connection(QString::number(std::intptr_t(obj)));
            if (con)
            {
                QColor color = (con->isOpened() ? Qt::green : Qt::red);
                painter->setBrush(QBrush(color));
                painter->setPen(color.darker(160));
                painter->drawEllipse(option.rect.topLeft() + QPoint(17, 4), 2, 2);
            }
        }
    }

    QTextDocument doc;
    QColor regularColor, lightColor;

    regularColor = style.palette.color(QPalette::Text);
    int l = regularColor.lightness();
    l = (l >= 100 ? 50 : 120);

    lightColor = QColor::fromHsv(regularColor.hsvHue(), regularColor.hslSaturation(), l);
    doc.setDefaultStyleSheet(QString("span.light { color: %1; } span.regular { color: %2; }").
                             arg(lightColor.name(), regularColor.name()));
    prepareDocToDrawDbTreeNode(option, index, doc);

    painter->save();
    painter->translate(option.rect.topLeft() +
                       QPoint(
                           style.features & QStyleOptionViewItem::HasDecoration ? 24 : 4,
                           int((option.rect.height() - doc.size().height()) / 2)
                           )
                       );
    doc.drawContents(painter, QRect(QPoint(0,0), option.rect.size()));
    painter->restore();
}

QSize DbTreeItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize sz; // = QStyledItemDelegate::sizeHint(option, index);
    QTextDocument doc;
    prepareDocToDrawDbTreeNode(option, index, doc);
    sz.setWidth(option.rect.x() +
                (option.features & QStyleOptionViewItem::HasDecoration ? 24 : 4) +
                int(doc.documentLayout()->documentSize().width()) + 1);
    sz.setHeight(int(doc.documentLayout()->documentSize().height()) + 4);
    return sz;
}

void DbTreeItemDelegate::prepareDocToDrawDbTreeNode(const QStyleOptionViewItem &option, const QModelIndex &index, QTextDocument &doc) const
{
    Q_UNUSED(option)

    doc.setDocumentMargin(0);
    QTextOption opt;
    opt.setWrapMode(QTextOption::NoWrap);
    doc.setDefaultTextOption(opt);
    //doc.setTextWidth(option.rect.width());   default = -1

    doc.setHtml(QString("<span class=\"regular\">%1</span>").arg(index.data().toString()));
}

MyProxyStyle::MyProxyStyle(QStyle *style) : QProxyStyle(style)
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

void MyProxyStyle::drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
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
