select 
	'domain' node_type,
	t.typname || coalesce(' <i><span class="light">' || 
		pg_catalog.format_type(t.typbasetype, t.typtypmod) ||
		case when t.typnotnull then ' NOT NULL' else '' end ||
		'</span></i>', '') ui_name,
	t.oid id,
	quote_ident(t.typname) "name",
	t.typtype tag
from pg_type t
where t.typnamespace = $schema.id$ and 
	t.typtype = 'd'
