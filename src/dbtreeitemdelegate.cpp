#include "dbtreeitemdelegate.h"
#include <QPainter>
#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QTreeView>

DbTreeItemDelegate::DbTreeItemDelegate(QObject *parent) :
    QStyledItemDelegate(parent)
{
}

void DbTreeItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem style(option);
    initStyleOption(&style, index);

    if (style.state & (QStyle::State_HasFocus | QStyle::State_Selected))
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
    }

    QTextDocument doc;
    QColor regularColor, lightColor;

    regularColor = style.palette.color(QPalette::Text);
    int l = regularColor.lightness();
    l = (l >= 100 ? 50 : 120);

    lightColor = QColor::fromHsv(regularColor.hsvHue(), regularColor.hslSaturation(), l);
    doc.setDefaultStyleSheet(QString("span.light { color: %1; } span.regular { color: %2; }").
                             arg(lightColor.name(), regularColor.name()));
    prepareDoc(option, index, doc);

    painter->save();
    painter->translate(option.rect.topLeft() + QPoint(
                           style.features & QStyleOptionViewItem::HasDecoration ? 24 : 4,
                           (option.rect.height() - doc.size().height()) / 2));
    doc.drawContents(painter, QRect(QPoint(0,0), option.rect.size()));
    painter->restore();
}

QSize DbTreeItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize sz; // = QStyledItemDelegate::sizeHint(option, index);
    QTextDocument doc;
    prepareDoc(option, index, doc);
    sz.setWidth(option.rect.x() +
                (option.features & QStyleOptionViewItem::HasDecoration ? 24 : 4) +
                doc.documentLayout()->documentSize().width() + 1);
    sz.setHeight(doc.documentLayout()->documentSize().height() + 4);
    return sz;
}

void DbTreeItemDelegate::prepareDoc(const QStyleOptionViewItem &option, const QModelIndex &index, QTextDocument &doc) const
{
    Q_UNUSED(option);
    QFont objectsFont(QApplication::font());
    objectsFont.setPointSize(9);        // TODO: move to settings

    doc.setDefaultFont(objectsFont);
    doc.setDocumentMargin(0);

    QTextOption opt;
    opt.setWrapMode(QTextOption::NoWrap);
    doc.setDefaultTextOption(opt);
    //doc.setTextWidth(option.rect.width());   default = -1

    doc.setHtml(QString("<span class=\"regular\">%1</span>").arg(index.data().toString()));
}
