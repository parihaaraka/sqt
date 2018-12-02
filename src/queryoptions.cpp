#include "queryoptions.h"
#include <QJsonParseError>
#include <QJsonArray>
#include <QRegularExpression>

QJsonObject QueryOptions::Extract(const QString &query)
{
    // TODO pop errors up

    // search for comments /*sqt ... */
    QRegularExpression cre(R"(\/\*sqt\b)", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator i = cre.globalMatch(query);

    QJsonObject result;
    QJsonParseError err;
    while (i.hasNext())
    {
        QRegularExpressionMatch commentMatch = i.next();
        QByteArray tmpSrc = query.midRef(commentMatch.capturedEnd()).toUtf8();
        QJsonDocument doc = QJsonDocument::fromJson(tmpSrc, &err);
        // The error is mandatory for the first try, because we need closing parenthes.
        if (err.error == QJsonParseError::GarbageAtEnd)
        {
            tmpSrc.resize(err.offset);
            doc = QJsonDocument::fromJson(tmpSrc, &err);
        }

        if (err.error != QJsonParseError::NoError && !doc.isObject())
            break;

        // merge result with last options scope
        QJsonObject tmp;
        for (const QString &k: doc.object().keys())
        {
            QJsonValue v = doc.object()[k];
            if (k == "interval" && !result.contains(k))
                tmp.insert(k, v);
            else if (k == "copy_src" || k == "copy_dst" || k == "graphs")
            {
                if (!result.contains(k))
                    tmp.insert(k, v.isArray() ? v : QJsonArray{v});
                else
                {
                    QJsonValue tmpVal = result[k];
                    QJsonArray tmpArray = tmpVal.isArray() ? tmpVal.toArray() : QJsonArray{tmpVal};
                    if (v.isArray())
                    {
                        QJsonArray innerArray = v.toArray();
                        while (!innerArray.empty())
                            tmpArray.append(innerArray.takeAt(0));
                    }
                    else
                        tmpArray.append(v);
                    tmp.insert(k, tmpArray);
                }
            }
        }

        for (const QString &k: result.keys())
        {
            if (tmp.contains(k))
                continue;
            tmp.insert(k, result[k]);
        }
        result.swap(tmp);
    }
    return result;
}

/*sqt
{
    "interval": "1000",
    "graphs": [
        {
            "name": "tps",
            "dif_y": {
                "field1": "#0f0",
                "field2": "#f00"
            }
        },
        {
            "name": "block i/o",
            "y": {
                "field1": "#0f0",
                "field2": "#f00"
            }
        }
    ]
}
*/

/*sqt
{
    "graphs": [
        {
            "name": "tps_log",
            "x": "timestamp_field",
            "y": {
                "field1": "#0f0",
                "field2": "#f00"
            }
        }
    ]
}
*/

/*sqt
{ "copy_dst": ["file1", "file2"] }
*/
