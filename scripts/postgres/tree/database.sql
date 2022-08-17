select
	'schema' node_type,
	case when nspname in ('information_schema', 'pg_catalog') then '<span class="light">' || nspname || '</span>'
		else nspname end ui_name,
	quote_ident(nspname) "name",
	oid id,
	'layers-small.png' icon,
	case when nspname in ('information_schema', 'pg_catalog') then '0' else '1' end || nspname sort1
from pg_catalog.pg_namespace
where nspname !~ ('pg_toast.*|pg_temp.*')
union all
select
	'extensions' node_type,
	'<i>Extensions</i>' ui_name,
	null "name",
	null id,
	'sd-memory-card.png' icon,
	'z1' sort1
union all
select
	'db_locks',
	'<i>Locks</i>',
	null,
	null,
	null,
	'z2' sort1
