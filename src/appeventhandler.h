#ifndef APPEVENTHANDLER_H
#define APPEVENTHANDLER_H

#include <QObject>

class AppEventHandler : public QObject
{
    Q_OBJECT
public:
    explicit AppEventHandler(QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *event);
};

#endif // APPEVENTHANDLER_H
