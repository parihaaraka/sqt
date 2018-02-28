select
    case c.relkind when 'v' then 'view' else 'table' end node_type,
    c.relname ui_name,
    c.relfilenode id,
    --quote_ident(c.relfilenode::regclass::text) "name",
    c.relfilenode::regclass::text "name",
    case c.relkind when 'v' then 'table-join.png' else 'table.png' end icon,
    c.relname sort1
from pg_catalog.pg_class c
    join pg_catalog.pg_namespace n on n.oid = c.relnamespace
where n.nspname = 'information_schema'
