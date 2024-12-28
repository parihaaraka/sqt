#include "timechart.h"
#include "timechartscene.h"
#include <QGuiApplication>
#include <QApplication>
//#include <QDesktopWidget>
#include <QScreen>
#include <cmath>
#include <QScrollBar>
#include <QWheelEvent>

class ChartPath : public QGraphicsPathItem
{
public:
    ChartPath(const QPainterPath &path, QGraphicsItem *parent = nullptr);
    virtual ~ChartPath() override = default;
    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr) override;
};

TimeChart::TimeChart(QWidget *parent) : QGraphicsView(parent)
{
    TimeChartScene *scene = new TimeChartScene(this);
    setRenderHints(QPainter::Antialiasing |
                   QPainter::SmoothPixmapTransform |
                   QPainter::TextAntialiasing);
    //setMouseTracking(true);

    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _scaleX = 10;
    _scaleY = -1;
    scale(_scaleX, _scaleY);
    scene->setSceneRect({0, 0, 30, 50});
    setScene(scene);
}

bool TimeChart::pathExists(const QString &name) const
{
    return _paths.contains(name);
}

QStringList TimeChart::pathNames() const
{
    return _paths.keys();
}

void TimeChart::createPath(const QString &name, const QColor &color, bool cumulative)
{
    QPen p(color, 2);
    p.setCosmetic(true);
    ChartPath *cpath = new ChartPath(QPainterPath{}); // owned by scene on addItem()
    cpath->setPen(p);
    _paths.insert(name, TimeChart::Path {cpath, std::numeric_limits<qreal>::quiet_NaN(), cumulative});
    scene()->addItem(cpath);
}

void TimeChart::appendValue(const QString &name, qreal value, const QDateTime &moment)
{
    // ensure the graph exists
    auto it = _paths.find(name);
    if (it == _paths.end())
        return;

    // create temporary values array if necessary
    auto pit = _pathParts.find(name);
    if (pit == _pathParts.end())
        pit = _pathParts.insert(name, {});

    if (!_startMoment.isValid())
        _startMoment = moment;

    QPointF newPt(_startMoment.msecsTo(moment) / 1000.0, value);
    pit.value().append(newPt);
}

void TimeChart::applyNewValues()
{
    QScrollBar *scroll = horizontalScrollBar();
    int scrollPos = scroll->value();
    bool wantScroll = (scroll->value() == scroll->maximum() || !scroll->isVisible());

    qreal _maxX = 0;
    bool adjustViewport = false;
    for (auto pit = _pathParts.begin(); pit != _pathParts.end(); ++pit)
    {
        TimeChart::Path &pathStruct = _paths[pit.key()];
        QGraphicsPathItem &graphicsItem = *pathStruct.pathItem;
        QPainterPath p = graphicsItem.path();
        for (auto &pt : pit.value())
        {
            qreal originalY = pt.y();
            auto cnt = p.elementCount();
            if (pathStruct.cumulative && !std::isnan(pathStruct.prevValue))
                pt.ry() -= pathStruct.prevValue;

            if (cnt)
                p.lineTo(pt);
            else if (!pathStruct.cumulative || !std::isnan(pathStruct.prevValue))
                p.moveTo(pt);

            if (pt.y() > _maxValue && (!pathStruct.cumulative || !std::isnan(pathStruct.prevValue)))
            {
                _maxValue = pt.y();
                adjustViewport = true;
            }
            pathStruct.prevValue = originalY;
        }
        _maxX = qMax(_maxX, pit.value().last().x());
        graphicsItem.setPath(p);
    }

    if (_pathParts.empty())
        return;
    _pathParts.clear();

    QRectF sr = sceneRect();
    sr.setRight(_maxX);
    auto *s = qobject_cast<TimeChartScene*>(scene());
    setSceneRect(sr);
    if (adjustViewport || s->_intervalY == 0.0)
        this->adjustViewport();
    scroll->setValue(wantScroll ? scroll->maximum() : scrollPos);
}

void TimeChart::setXSourceField(const QString &name)
{
    _xSourceField = name;
}

QString TimeChart::xSourceField() const
{
    return _xSourceField;
}

QString TimeChart::xLabel(qreal x, qreal secApproxInterval)
{
    QDateTime ts = _startMoment.addMSecs(static_cast<qint64>(x * 1000));
    // fix rounding problems
    auto ms = ts.time().msec();
    if (ms > 990)
        ts = ts.addMSecs(1000 - ms);
    else if (ms > 0 && ms < 10)
        ts = ts.addMSecs(-ms);

    if (secApproxInterval > 3600 * 3)
        return ts.toString("dd MMM HH:mm");
    if (secApproxInterval > 180)
        return ts.toString("HH:mm");
    if (secApproxInterval > 3)
        return ts.toString("HH:mm:ss");
    return ts.toString("HH:mm:ss.") +
            QString("%1").arg(static_cast<int>(round(ts.time().msec() / 100.0)));
}

void TimeChart::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier))
    {
        QGraphicsView::wheelEvent(event);
        return;
    }

    if (!event->angleDelta().isNull())
    {
        qreal degrees = event->angleDelta().y() / 8;
        qreal k = 1 + degrees / 90.0;
        qreal newScale = _scaleX * k;
        if (newScale <= 1000 && newScale > 0.005)
        {
            _scaleX = newScale;
            scale(k, 1.0);
            adjustViewport();
            update();
        }
    }
    event->accept();
}

void TimeChart::resizeEvent(QResizeEvent *event)
{
    QScrollBar *scroll = horizontalScrollBar();
    qreal scrollPos = static_cast<qreal>(scroll->value()) / scroll->maximum();
    adjustViewport();
    scroll->setValue(static_cast<int>(scroll->maximum() * scrollPos));
    QGraphicsView::resizeEvent(event);
}

void TimeChart::adjustViewport()
{
    QFontMetrics fontMetrics(font());
    auto tmpBoundingRect = fontMetrics.boundingRect("X");
    QRect vp = viewport()->rect();
    constexpr int viewportRightMargin = 10;
    int viewportBottomMargin = qRound(tmpBoundingRect.height() * 1.3);
    int viewportTopMargin = viewportBottomMargin;
    auto *s = qobject_cast<TimeChartScene*>(scene());

    // adjust sceneRect to fit into view and place 0 to lower frame border
    QRectF sr = sceneRect();
    qreal sceneHeight = _maxValue * 1.2 - 0; // lower bound is 0
    int viewportClipHeight = vp.height() - viewportTopMargin - viewportBottomMargin;
    sr.setTop(0 - sceneHeight * viewportBottomMargin / viewportClipHeight);
    sr.setBottom(_maxValue * 1.2 + sceneHeight * viewportTopMargin / viewportClipHeight);
    setSceneRect(sr);

    /* works too
    _scaleY = - viewport()->height() / sr.height();
    resetTransform();
    scale(_scaleX, _scaleY);
    */

    qreal prevScaleY = _scaleY;
    _scaleY = - viewport()->height() / sr.height();
    if (!std::isinf(_scaleY))
        scale(1, _scaleY / prevScaleY);
    else
        _scaleY = prevScaleY;

    qreal sceneTop = mapToScene(0, viewportTopMargin).y();
    qreal sceneBottom = mapToScene(0, vp.bottom() - viewportBottomMargin).y();
    s->_intervalY = beautifyInterval((sceneTop - sceneBottom) / 5);

    // use negative value to reserve space for '-'
    QString maxLengthLabel = QString::number(
                - qMax(
                    static_cast<int>(sceneTop),
                    static_cast<int>(sceneBottom)
                    ) - s->_intervalY);
    s->_yLabelRect = fontMetrics.boundingRect(maxLengthLabel);
    _viewportClipRect = vp.adjusted(
                static_cast<int>(s->_yLabelRect.width()) + viewportRightMargin,
                viewportBottomMargin,
                -viewportRightMargin,
                -viewportBottomMargin);

    auto sceneClipRect = mapToScene(_viewportClipRect).boundingRect();
    s->_intervalX = beautifyTimeInterval(sceneClipRect.width() / 5);
}

qreal TimeChart::beautifyInterval(qreal interval)
{
    qreal power = round(std::log10(interval));
    qreal res = pow(10, power);
    if (res > interval)
        res /= 2;
    else if (res < interval / 2)
        res *= 2;
    return res;
}

qreal TimeChart::beautifyTimeInterval(qreal interval)
{
    qreal secsInterval = round(interval);
    QVector<qreal> divisors {
        3600 * 24,
        3600 * 12,
        3600 *  6,
        3600 *  3,
        3600,
        1800, // 30 min
         900, // 15 min
         600, // 10 min
         300, //  5 min
         150, //2.5 min
          60, //  1 min
          30, 15, 10, 5, 2
    };
    for (qreal div: divisors)
    {
        if (round(secsInterval / div) >= 1)
            return div;
    }
    return 1; // 1 sec
}

ChartPath::ChartPath(const QPainterPath &path, QGraphicsItem *parent) :
    QGraphicsPathItem(path, parent)
{

}

void ChartPath::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    painter->setClipRect(qobject_cast<TimeChartScene*>(scene())->clipRect());
    QGraphicsPathItem::paint(painter, option, widget);
    painter->setClipping(false);
}
