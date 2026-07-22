select
	'schema' node_type,
	case when nspname in ('information_schema', 'pg_catalog') then '<span class="light">' || nspname || '</span>'
		else nspname end ui_name,
	quote_ident(nspname) "name",
	oid id,
	'layers-small.png' icon,
	case when nspname in ('information_schema', 'pg_catalog') then '0' else '1' end || nspname sort1
from pg_catalog.pg_namespace
where nspname like ('$schemas_folder.name$' || '%')