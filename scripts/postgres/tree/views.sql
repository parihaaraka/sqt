select 
	--case when c.relkind = 'v'::"char" then 'view' else 'mview' end node_type,
	'table' node_type,
	c.relname ||
		case when c.relkind = 'm'::"char" then '&nbsp;<i>M</i>' else '' end ui_name,
	c.oid id,
	c.relkind::text tag,
	quote_ident(c.relname) "name",
	true allow_multiselect,
	'table-join.png' icon,
	c.relname sort1
from pg_namespace nc
	join pg_class c on nc.oid = c.relnamespace
where nc.oid = $schema.id$ and
	c.relkind in ('v'::"char", 'm'::"char") and 
	not pg_is_other_temp_schema(nc.oid) and 
	(
		pg_has_role(c.relowner, 'usage'::text) or 
		has_table_privilege(c.oid, 'select, insert, update, delete, truncate, references, trigger'::text) or 
		has_any_column_privilege(c.oid, 'select, insert, update, references'::text)
	)
order by c.relname