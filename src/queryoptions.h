#ifndef QUERYOPTIONS_H
#define QUERYOPTIONS_H

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>

class QueryOptions
{
public:
    QueryOptions() = delete;
    static QJsonObject Extract(const QString &query);
};

#endif // QUERYOPTIONS_H
