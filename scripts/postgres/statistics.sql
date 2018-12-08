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
