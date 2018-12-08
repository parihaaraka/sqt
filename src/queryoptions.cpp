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
            else if (k == "copy_src" || k == "copy_dst" || k == "charts")
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
    "interval": 1000,
    "charts": [
        {
            "name": "tps",
            "agg_y": {
                "field1": "#0a0",
                "field2": "#a00"
            }
        },
        {
            "name": "block i/o",
            "y": {
                "field3": "#bb0",
                "field4": "#00b"
            }
        }
    ]
}
*/

/*sqt
{
    "charts": [
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

// --- examples ---
/*sqt
{
    "charts": [
        {
            "name": "tps_log",
            "x": "ts",
            "y": {
                "f1": "#0c0"
            }
        }
    ]
}
*/
/*
select s.ts, 5 + 4*random() f1
from generate_series(now(), now() + '20min'::interval, '1sec'::interval) as s(ts)
*/


/*sqt
{
    "interval": 1000,
    "charts": [
        {
            "name": "sessions",
            "y": { "active": "#0b0", "total": "#c00", "idle": "#00c" }
        },
        {
            "name": "transactions, backends",
            "agg_y": { "xact_commit": "#0b0", "xact_rollback": "#c00" },
            "y" : { "numbackends": "#00c" }
        },
        {
            "name": "tuples out",
            "agg_y": { "fetched": "#cb0", "returned": "#0c0" }
        }
    ]
}
*/
/*
select count(*) total,
    count(*) filter (where state = 'active') active,
    count(*) filter (where state = 'idle') idle
from pg_stat_activity;

select
    sum(xact_commit) xact_commit,
    sum(numbackends) numbackends,
    sum(xact_rollback) xact_rollback,
    sum(tup_fetched) fetched,
    sum(tup_returned) returned
from pg_stat_database;
*/
