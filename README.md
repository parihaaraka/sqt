# sqt
`sqt` (sql query tool) is a cross-platform program to provide typical sql data source exploring and programming interface.

## Overview
The subject was aimed to provide fast and convenient MS SQL query tool under linux. The only existing ODBC interface lead to support of any other ODBC data source. As a result of pgAdmin3 deprecation `sqt` was modified to have a native PostgreSQL support via libpq. Due to my current needs PostgreSQL support is in priority.
The main target audience are db programmers.
#### Binaries
Standalone (outdated) [sqt for windows x64](https://drive.google.com/open?id=1pD-twf3N0svQv-UzolVLlaUl3PZ1GK8U) (~12Mb)

## Feature highlights
* Customizable objects tree and textual/tabular content view (see [scripts/README.md](https://github.com/parihaaraka/sqt/blob/master/scripts/README.md)) let you build you own tree with application-specific nodes;
* adjustable sql highlighting (see any existing `hl.conf` for more details);
* alternative sorting (e.g., sort table columns in original/alphabetical order);
* multiple selection to generate appropriate SQL code (e.g., select/insert/update commands with selected columns only); 
* convenient (Qt Creator-like) search/replace panel;
* multiple resultsets support;
* bookmarks (mark: `Ctrl+M`, previous: `Ctrl+,`, next: `Ctrl+.`, last: `Ctrl+L`);
* uppercase (`Ctrl+U`), lowercase (`Ctrl+Shift+U`, `Ctrl+Win+U`)
* code completion support (`Ctrl+Space`);
* json viewer with highlighting and extracting json from it's nested text value (`Ctrl+J`);
* totalling selected cells (`F6`);
* customizable time charts to display [current](https://github.com/parihaaraka/sqt/blob/master/scripts/postgres/statistics.sql) or [recorded](https://github.com/parihaaraka/sqt/blob/master/scripts/postgres/recorded_statistics.sql) statistics;
* resultsets structure textual output;
* pg: client-side COPY to file or log widget, COPY from file;
* pg: receiving notifications (`NOTIFY`) and messages (`RAISE`).

![screenshot](https://github.com/parihaaraka/sqt/wiki/img/screenshot1.png)
![screenshot](https://github.com/parihaaraka/sqt/wiki/img/charts.png)

## COPY to/from local file via meta-comments (pg only)
Use `COPY FROM STDIN` and `COPY TO STDOUT` forms of the command with some magic in comments:
```sql
/*sqt { "copy_dst": ["/tmp/pg_stat_activity.csv", "/tmp/pg_stat_database.csv"] } */
copy (select * from pg_stat_activity) to stdout with (format csv, header);
copy (select * from pg_stat_database) to stdout with (format csv, header);
```
Specify an empty string instead of file name for output to the log widget:
```sql
/*sqt { "copy_dst": "" } */
copy (select * from pg_stat_activity) to stdout with (format csv, header);
```
* As you can see, the non-array form of `copy_dst` may be used in case of single source query.


```sql
create table tmp1 as select * from pg_stat_activity limit 0;
create table tmp2 as select * from pg_stat_database limit 0;

/*sqt { "copy_src": ["/tmp/pg_stat_activity.csv", "/tmp/pg_stat_database.csv"] } */
copy tmp1 from stdin with (format csv, header);
copy tmp2 from stdin with (format csv, header);
```

## Charting via meta-comments
### Timelines example:
```sql
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
```
`interval` - interval to reexecute queries (milliseconds);

`charts` - list of charts with names and graphical paths description;

`agg_y` - cumulative values source.

### Plot some source of (x,y) values
```sql
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
select s.ts, 5 + 4*random() f1
from generate_series(now(), now() + '20min'::interval, '1sec'::interval) as s(ts)
```

## Build instruction
You may build the project by means of QtCreator or execute this sequence of commands from the project's root directory:
```
mkdir build && cd build && qmake ../src/sqt.pro && make
```
Qt toolchain must be installed and be available via PATH.

## Todo
* Improve code completion, prepare scripts to fetch metadata from non-postgresql data sources;
* executing of JavaScript along with SQL to run automation tasks;
* new script types to extend objects tree interaction (modifying, administration tasks);
* enhance scripts to make `sqt` as useful as possible (+provide scripts for different dbms, versions, generic odbc data source);
* batch mode;
* reports.

### Acknowledgment
Some icons by [Yusuke  Kamiyamane](http://p.yusukekamiyamane.com). All rights reserved.
