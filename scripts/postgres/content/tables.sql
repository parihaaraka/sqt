select c.oid, c.relname as "name",
    case
        when nc.oid = pg_my_temp_schema() then 'local temporary'
        when c.relkind = any (array['r'::"char", 'p'::"char"]) then 'base table'
        when c.relkind = 'v'::"char" then 'view'
        when c.relkind = 'f'::"char" then 'foreign table'
        else null::text
    end as "type",
        (c.relkind = any (array['r'::"char", 'p'::"char"])) 
        or (c.relkind = any (array['v'::"char", 'f'::"char"])) 
        and (pg_relation_is_updatable(c.oid::regclass, false) & 8) = 8 
    as is_insertable_into,
    pg_size_pretty(pg_relation_size(c.oid)) as "size",
    c.reltuples
from pg_namespace nc
    join pg_class c on nc.oid = c.relnamespace
where nc.oid = $schema.id$ and
    (c.relkind = any (array['r'::"char", 'f'::"char", 'p'::"char"])) and 
    not pg_is_other_temp_schema(nc.oid) and 
    (
        pg_has_role(c.relowner, 'usage'::text) or 
        has_table_privilege(c.oid, 'select, insert, update, delete, truncate, references, trigger'::text) or 
        has_any_column_privilege(c.oid, 'select, insert, update, references'::text)
    )
order by pg_relation_size(c.oid) desc