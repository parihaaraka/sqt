#include "timechartscene.h"
#include <cmath>
#include "timechart.h"

TimeChartScene::TimeChartScene(QObject *parent) :
    QGraphicsScene(parent)
{

}

QRectF TimeChartScene::clipRect()
{
     return _sceneClipRect;
}

void TimeChartScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    TimeChart *chart = qobject_cast<TimeChart*>(views().at(0));
    _sceneClipRect = chart->mapToScene(chart->_viewportClipRect).boundingRect();

    // calculate origin time point to get nice grid values
    qreal hLeft = - QDateTime(chart->_startMoment.date(), QTime()).
            msecsTo(chart->_startMoment) / 1000.0;

    // initial grid values
    qreal left = hLeft + floor((_sceneClipRect.left() - hLeft) / _intervalX) * _intervalX;
    qreal top = _sceneClipRect.top() - fmod(_sceneClipRect.top(), _intervalY);

    auto prevTransform = painter->transform();
    painter->resetTransform();
    QVector<QLineF> grid;
    for (qreal x = left; x < _sceneClipRect.right(); x += _intervalX)
    {
        if (x < _sceneClipRect.left())
            continue;
        // labels
        auto pt = chart->mapFromScene(x, _sceneClipRect.top());
        QString label = chart->xLabel(x, _sceneClipRect.width() / 5);
        QRectF r(pt.x() - 100, pt.y() + 2, 200, _yLabelRect.height());
        painter->drawText(r, Qt::AlignHCenter, label);
        // vertical grid
        grid.append(QLineF(x, rect.top(), x, rect.bottom()));
    }

    for (qreal y = top; y < _sceneClipRect.bottom(); y += _intervalY)
    {
        // labels
        auto pt = chart->mapFromScene(_sceneClipRect.left(), y);
        QString label = QString::number(y);
        QRectF r = _yLabelRect.translated(5, pt.y() - _yLabelRect.top() - _yLabelRect.height()/2);
        // do not draw if the label's lower verge is under clipRect
        if (r.bottom() > chart->_viewportClipRect.bottom())
            continue;
        painter->drawText(r, Qt::AlignRight | Qt::AlignVCenter, label);
        // gorizontal grid
        grid.append(QLineF(rect.left(), y, rect.right(), y));
    }
    painter->setTransform(prevTransform);

    painter->save();
    QPen pen(QColor(230, 230, 230));
    pen.setWidth(0);
    painter->setPen(pen);
    painter->setClipRect(_sceneClipRect);
    painter->drawLines(grid.data(), grid.size());
    painter->restore();
}

void TimeChartScene::drawForeground(QPainter *painter, const QRectF &)
{
    // clip rect border
    painter->save();
    QPen pen(Qt::darkGray);
    pen.setWidth(0);
    painter->setPen(pen);
    painter->drawRect(_sceneClipRect);
    painter->restore();

    // caption
    TimeChart *chart = qobject_cast<TimeChart*>(views().at(0));
    painter->resetTransform();
    QPoint topLeft = chart->mapFromScene(_sceneClipRect.bottomLeft());
    QPoint pt1 = topLeft;
    pt1.ry() -= _yLabelRect.height() * 1.3;
    QPoint pt2 = chart->mapFromScene(_sceneClipRect.bottomRight());
    painter->drawText(QRect(pt1, pt2), Qt::AlignCenter, chart->objectName());
    QPen textPen = painter->pen();

    // labels
    int rowHeight = static_cast<int>(_yLabelRect.height() * 1.2);
    int h = chart->_paths.count() * rowHeight;
    int vMargin = 3;
    int maxWidth = 0;
    const auto labels = chart->_paths.keys();
    for (const auto &label: labels)
        maxWidth = qMax(maxWidth, painter->boundingRect(QRect(0, 0, 0, 0), 0, label).width());

    painter->translate(topLeft.x() + 10, topLeft.y() + 10);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(220, 220, 255, 170));
    painter->drawRoundedRect(QRect(0, 0, maxWidth + rowHeight + 15, h + vMargin * 2), 3, 3);

    int i = 0;
    for (const auto &label: labels)
    {
        TimeChart::Path &path = chart->_paths[label];
        painter->setPen(Qt::NoPen);
        painter->setBrush(path.pathItem->pen().color());
        painter->drawRoundedRect(QRect(6, vMargin + i * rowHeight + 4, rowHeight - 8, rowHeight - 8), 3, 3);
        painter->setPen(textPen);
        painter->drawText(QRectF(6 + rowHeight, vMargin + i * rowHeight, maxWidth, rowHeight), Qt::AlignVCenter, label);
        ++i;
    }
}
