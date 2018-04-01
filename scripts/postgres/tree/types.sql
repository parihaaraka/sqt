select 
	'type' node_type,
	t.typname || coalesce(' <i><span class="light">' || 
		case 
			when t.typtype = 'c'::"char" then 'composite'
			when t.typtype = 'd'::"char" then 'domain'
			when t.typtype = 'e'::"char" then 'enum'
			when t.typtype = 'p'::"char" then 'pseudo-type'
			when t.typtype = 'r'::"char" then 'range'
		end || '</span></i>', '') ui_name,
	t.oid id,
	quote_ident(t.typname) "name",
	t.typtype tag
from pg_type t
	left join pg_class c on t.typrelid = c.oid
where t.typnamespace = $schema.id$ and 
	t.typtype != 'b' and
	(c.relkind is null or c.relkind = 'c')
