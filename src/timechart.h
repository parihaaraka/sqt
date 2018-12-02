#ifndef TIME_CHART_H
#define TIME_CHART_H

#include <QObject>
#include <QWidget>
#include <QGraphicsView>
#include <QMap>
#include <QGraphicsPathItem>
#include <QDateTime>

class TimeChartScene;
class TimeChart : public QGraphicsView
{
    Q_OBJECT
    friend class TimeChartScene;
public:
    explicit TimeChart(QWidget *parent = nullptr);
    virtual ~TimeChart() override = default;
    bool pathExists(const QString &name);
    void createPath(const QString &name, const QColor &color, bool cumulative = false);
    void appendValue(const QString &name, qreal value, const QDateTime &moment);
    void applyNewValues();
protected:
    virtual void wheelEvent(QWheelEvent *event) override;
    virtual void resizeEvent(QResizeEvent * event) override;
private:
    struct Path {
        QGraphicsPathItem* pathItem;
        qreal prevValue; // to process cumulative values
        bool cumulative;
    };
    QMap<QString, QVector<QPointF>> _pathParts;
    QDateTime _startMoment;
    QMap<QString, Path> _paths;
    QRect _viewportClipRect;
    qreal _scaleX, _scaleY;
    qreal _maxValue = 0;
    QString xLabel(qreal x, qreal secApproxInterval);
    qreal beautifyInterval(qreal interval);
    qreal beautifyTimeInterval(qreal interval);
private slots:
    void adjustViewport();
};

#endif // TIME_CHART_H
