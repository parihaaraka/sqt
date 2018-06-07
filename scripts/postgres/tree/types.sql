select 
	'type' node_type,
	t.typname || coalesce(' <i><span class="light">' || 
		case 
			when t.typtype = 'b'::"char" then 'base'
			when t.typtype = 'c'::"char" then 'composite'
			when t.typtype = 'e'::"char" then 'enum'
			when t.typtype = 'p'::"char" then 'pseudo-type'
			when t.typtype = 'r'::"char" then 'range'
		end || '</span></i>', '') ui_name,
	t.oid id,
	quote_ident(t.typname) "name",
	t.typtype tag,
	t.typname sort1
from pg_type t
	left join pg_class c on t.typrelid = c.oid
where t.typnamespace = $schema.id$ and 
	t.typtype not in ('d'::"char") and --exclude domains
	(c.relkind is null or c.relkind = 'c') and -- exclude composite types
	t.typname not like '\_%' -- not an array
