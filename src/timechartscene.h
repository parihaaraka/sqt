#ifndef TIMECHARTSCENE_H
#define TIMECHARTSCENE_H

#include <QObject>
#include <QGraphicsScene>

class TimeChartScene : public QGraphicsScene
{
    Q_OBJECT
    friend class TimeChart;
public:
    QRectF clipRect();
private:
    QRectF _sceneClipRect;
    QRect _yLabelRect;
    qreal _intervalX = 0;
    qreal _intervalY = 0;
protected:
    TimeChartScene(QObject *parent = nullptr);
    virtual ~TimeChartScene() override = default;
    virtual void drawBackground(QPainter *painter, const QRectF &rect) override;
    virtual void drawForeground(QPainter *painter, const QRectF &rect) override;
};

#endif // TIMECHARTSCENE_H
