select 
	'table' node_type,
	c.relname || coalesce(' <i><span class="light"> ' ||
		case 
			when c.relpersistence = 'u'::"char" then 'unlogged'
			when c.relkind = 'f'::"char" then
				'&larr; ' ||
					(select srvname
					from pg_foreign_table t
						join pg_foreign_server s on t.ftserver = s.oid
					where t.ftrelid = c.oid)
		end || '</span></i>', '') ui_name,
	c.oid id,
	c.relkind::text tag,
	quote_ident(c.relname) "name",
	true allow_multiselect,
	'table.png' icon,
	c.relname sort1,
	c.relkind::text || c.relname sort2
from pg_class c
where c.relnamespace = $schema.id$ and
	(c.relkind = any (array['r'::"char", 'f'::"char", 'p'::"char"])) and 
	not pg_is_other_temp_schema(c.relnamespace) and 
	(
		pg_has_role(c.relowner, 'usage'::text) or 
		has_table_privilege(c.oid, 'select, insert, update, delete, truncate, references, trigger'::text) or 
		has_any_column_privilege(c.oid, 'select, insert, update, references'::text)
	)
order by c.relname desc