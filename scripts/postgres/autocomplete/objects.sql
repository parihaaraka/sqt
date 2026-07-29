/*
autocomplete source, schema-level
*/

with ns as
(
	select oid, nspname
	from pg_catalog.pg_namespace
	where nspname not like 'pg_toast%' and nspname not like 'pg_temp%'
),
target_ns as
(
	select ns.*, cs.ord
	from ns
		join unnest(current_schemas(true)) with ordinality as cs(n, ord) on ns.nspname = cs.n
	where '$schema.name$' = 'NULL'
	union all
	-- add schema that is not in search_path
	select *, 0
	from ns
	where nspname = '$schema.name$'
)
--schemas
select nspname "name", jsonb_build_object('n', 'schema') info
from ns
where '$schema.name$' = 'NULL' -- return schemas if current schema is not specified
union all
--<search_path>.table,view,etc
select * from (
	select distinct on (c.relname) -- first found in search path or specified schema
		c.relname,
		jsonb_build_object(
			'n',  -- name
			case c.relkind
				when 'r'::"char" then 'table'
				when 'p'::"char" then 'partitioned table'
				when 'f'::"char" then 'foreign table'
				when 'c'::"char" then 'composite type'
				when 'v'::"char" then 'view'
				when 'm'::"char" then 'materialized view'
				when 'S'::"char" then 'sequence'
				else null
			end,
			'd', 	-- description
			obj_description(c.oid, 'pg_class')
		)
	from target_ns ns
		join pg_class c on ns.oid = c.relnamespace
	where c.relkind not in ('i'::"char", 'I'::"char", 't'::"char") -- not an index or toast table
	order by c.relname, ns.ord
) tmp
union all
--<search_path>.type
select * from (
	select distinct on (t.typname)  -- first found in search path or specified schema
		t.typname,
		jsonb_build_object(
			'n',
			case
				when t.typtype = 'b'::"char" then 'base type'
				when t.typtype = 'd'::"char" then 'domain'
				when t.typtype = 'e'::"char" then 'enum'
				when t.typtype = 'p'::"char" then 'pseudo-type'
				when t.typtype = 'r'::"char" then 'range'
			end,
			'd',
			trim(
				case when t.typbasetype != 0  then '→ ' || pg_catalog.format_type(t.typbasetype, t.typtypmod) || E'\n' else '' end ||
				case when t2.typbasetype != 0 then '  → ' || pg_catalog.format_type(t2.typbasetype, t2.typtypmod) || E'\n' else '' end ||
				case when t3.typbasetype != 0 then '    → ' || pg_catalog.format_type(t3.typbasetype, t3.typtypmod) || E'\n' else '' end ||
				case when t4.typbasetype != 0 then '    → ' || pg_catalog.format_type(t4.typbasetype, t4.typtypmod) || E'\n' else '' end ||
				E'\n' ||
				coalesce(obj_description(t.oid, 'pg_type'), ''),
			E'\n')
		)
	from target_ns ns
		join pg_type t on ns.oid = t.typnamespace
		left join pg_class c on t.typrelid = c.oid
		left join pg_type t2 on t.typtype = 'd'::"char" and t.typbasetype != 0 and t.typbasetype = t2.oid
		left join pg_type t3 on t2.typtype = 'd'::"char" and t2.typbasetype != 0 and t2.typbasetype = t3.oid
		left join pg_type t4 on t2.typtype = 'b'::"char" and t2.typcategory = 'A' and t2.typelem != 0 and t2.typelem = t4.oid
	where	c.relkind is null and 	-- see prev subquery
		t.typname not like '\_%' -- not an array
	order by t.typname, ns.ord
) tmp
union all
--<search_path>.function
select
	p.proname,
	jsonb_agg(
		jsonb_build_object(
			'n',
			case when '$schema.name$' = 'NULL' then quote_ident(ns.nspname) || '.' else '' end ||
			quote_ident(p.proname) ||
			'(' || oidvectortypes(p.proargtypes) || ') → ' ||
			-- prorettype::regtype::text shows long name (e.g. timestamp with time zone) but displays arrays with brackets
			-- typname contains short name with leading '_' for arrays
			case when left(t.typname, 1) = '_' then p.prorettype::regtype::text else t.typname end,
			'd',
			d
		)
	)
from target_ns ns
	join pg_proc p on ns.oid = p.pronamespace
	left join pg_type t on p.prorettype = t.oid
	left join lateral obj_description(p.oid, 'pg_proc') d on true
where p.prorettype != 'trigger'::regtype and
	(
		d is null or
		(
			d not like 'I/O%' and
			d not like 'internal%' and
			d not like 'hash%' and
			d not like 'SP-GiST support%' and
			d not in ('less-equal-greater' ,'(internal)', 'larger of two', 'smaller of two') and
			d not like '%selectivity%' and
			d !~ '(operator|final function|transition function|support|pg_upgrade|osolete\)|combine function|serial function)$' and
			d not like '%index access%'
		)
	)
group by p.proname
order by 1