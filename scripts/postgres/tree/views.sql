select 
    'view' node_type,
    c.relname ui_name,
    c.oid id,
    quote_ident(c.relname) "name",
    'table-join.png' icon,
    c.relname sort1
from pg_namespace nc
    join pg_class c on nc.oid = c.relnamespace
where nc.oid = $schema.id$ and
    c.relkind = 'v'::"char" and 
    not pg_is_other_temp_schema(nc.oid) and 
    (
	pg_has_role(c.relowner, 'usage'::text) or 
	has_table_privilege(c.oid, 'select, insert, update, delete, truncate, references, trigger'::text) or 
	has_any_column_privilege(c.oid, 'select, insert, update, references'::text)
    )
order by c.relname