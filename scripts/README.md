The script's file name (excluding extension) corresponds to the node's type (see `node_type` column below) being processed.

A script may contain comments corresponding to regexp `(?=\/\*\s*V(\d+)\+\s*\*\/)`
For example:
```sql
/* V90000+ */
select s.datname, s.pid, s.usename --, ...
from pg_stat_activity s

/* V100000+ */
select s.datname, s.pid, s.backend_type, s.usename --, ...
from pg_stat_activity s
```
Such boundaries split a script into parts acording to dbms version. PostgreSQL uses libpq's PQserverVersion(), ODBC data sources must provide version.sql or version.qs to return this value if used within scripts. E.g. `scripts/odbc/microsoft sql/version.sql` (uses compartibility_level as a comparable version).

## `/tree` scripts

Script to build tree must return resultset with the following columns:

|name|type|description|
|--|--|--|
|ui_name|varchar|*Mandatory* label to be displayed. E.g.:<br/>`<span class="light">light-colored text</span>`<br/>`<i>italic text</i>`<br/>and so on|
|node_type|varchar|*Mandatory* type of node.<br/>(= file name of dependent script inside sibling folders)|
|name|varchar|`name` field to reference to from another script. Usually it's a full or schemaless quoted or unquoted db object's name depending on the way you use the value within the script.|
|id|varchar|As a rule it's an internal id of database object to be referenced from dependent scripts (or any other convenient value).|
|tag|varchar/int|Any data to be used within dependent scripts.|
|icon|varchar|Icon file name.|
|allow_multiselect|bool|Whether the object may have multiple selected children.|
|sort1|varchar/int|Value to sort on.|
|sort2|varchar/int|Value for alternative sorting.|

Script may contain macro `$<node_type>.<name|id|tag>$` E.g. if current object (being processed by current script) has parent node of type `schema`, then `$schema.id$` within script will be replaced with `id`
value of that node. `$schema.name$` and `$schema.tag$` will be replaced with that node's `name` or `tag` value accordingly. If current node has no parent of specified type, or that parent does not have corresponding field, the macro is replaced by `NULL`.

Actually, user is free to place any kind of data in columns `name`, `id` and `tag`. This is just what you get in script by means of the corresponding macro.

>There are two hardcoded node types: `connection` and `database`. `connection` is used internally to represent database connection, so the first script to be executed is `connection.sql` or `connection.qs`. `database` nodes are usually children of `connection` node, and their `name` property has a special meaning: every database node generates separate database connection with connection string built with parent connection string appended with `;Database={<name>}` for odbc data source and `dbname='<name>'` for postgresql data source.

## `/content` scripts
Script must return one of the following resultset:
- 1 row 1 column named `script`;
- 1 row 1 column named `html`;
- any other resultset.

Sql script for PostgreSQL may contain [plpgsql anonymous code block](https://github.com/parihaaraka/sqt/blob/master/scripts/postgres/content/table.sql) and return textual data in the following ways: 
- `raise notice '<db object related sql script content>' using hint='script'`;
- `raise notice '<html content>' using hint='html'`.


Besides described macroses the script may contain:
* `$dbms.version$` - comparable dbms version (integer value);
* `$children.ids$` - ids of selected nodes (comma separated);
* `$children.names$` - names of selected nodes (comma separated, enclosed in single quotes if not yet). 
`$children.<ids|names>` macro is actual in case of multiple nodes selection and works within parent node content script. When there is no multiple selected children of the node being scripted, the first macro is replaced by -1, the second one - by `''` (empty quoted string).

## `/preview` scripts
Every node type may have corresponding script to display additional single resultset. Use it, for example, to display table/view content preview. Current implementation displays preview resultset only if content script didn't return resultset already.
