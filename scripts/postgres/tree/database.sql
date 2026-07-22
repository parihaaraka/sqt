with s as (
	select (regexp_match(nspname, '([^_]+)'))[1] prefix, array_agg(oid) oids, count(*) cnt
	from pg_catalog.pg_namespace
	where nspname !~ ('pg_toast.*|pg_temp.*') and nspname ~ '^[^_]+_'
	group by (regexp_match(nspname, '([^_]+)'))[1]
	having count(*) > 5
)
select
	'schema' node_type,
	case when ns.nspname in ('information_schema', 'pg_catalog') then '<span class="light">' || ns.nspname || '</span>'
		else ns.nspname end ui_name,
	quote_ident(ns.nspname) "name",
	ns.oid id,
	'layers-small.png' icon,
	case when ns.nspname in ('information_schema', 'pg_catalog') then '0' else '1' end || ns.nspname sort1
from pg_catalog.pg_namespace ns
	left join s on ns.oid = any(s.oids)
where ns.nspname !~ ('pg_toast.*|pg_temp.*') and s.prefix is null
union all
select
	'schemas_folder' node_type,
	s.prefix || ' <i><span class="light">group</span></i>' ui_name,
	s.prefix "name",
	null id,
	'layers-small.png' icon,
	'1' || prefix sort1 --
from s