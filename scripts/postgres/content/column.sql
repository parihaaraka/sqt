with tmp as
(
	select *
	from pg_stats
	where tablename = '$table.name$' and schemaname = '$schema.name$' and attname = '$column.name$'
),
tmp2 as
(
	select k,v
	from jsonb_each_text(to_jsonb((select tmp from tmp)) - '{tablename,schemaname,attname}'::text[]) as x(k,v)
)
select coalesce(nullif(string_agg(format('%-25s: %s', k, v::text), E'\n'), ''), '-- statistics not found') as script
from tmp2
--order by 1