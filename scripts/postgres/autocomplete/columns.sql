/*
autocomplete source, table-level
*/

with t as
(
	select c.oid
	from pg_catalog.pg_class c
		join pg_catalog.pg_namespace ns on c.relnamespace = ns.oid
		left join unnest(current_schemas(true)) with ordinality as cs(n, ord) on ns.nspname = cs.n
	where 
		c.relname = '$table.name$' and
		(
			('$schema.name$' = 'NULL' and cs.ord is not null) or -- on search patch
			ns.nspname = '$schema.name$' -- exact namespace
		) and
		c.relkind in ('r'::"char", 'p'::"char", 'f'::"char", 'v'::"char", 'm'::"char")
	order by coalesce(cs.ord, 0)
	limit 1
)
select
    a.attname "name",
    jsonb_build_object(
    	'n', pg_catalog.format_type(a.atttypid, a.atttypmod),
    	'd', col_description(t.oid, a.attnum)
    ) info
from t
	join pg_catalog.pg_attribute a on t.oid = a.attrelid
where a.attnum > 0 and not a.attisdropped 
order by 1