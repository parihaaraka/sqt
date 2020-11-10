select
	c.relname as "name",
	c.oid,
	case
		when c.relnamespace = pg_my_temp_schema() then 'local temporary'
		when c.relkind = 'r'::"char" then 'ordinary table'
		when c.relkind = 'p'::"char" then 'partitioned table'
		when c.relkind = 'v'::"char" then 'view'
		when c.relkind = 'f'::"char" then 'foreign table'
		else null::text
	end as "type",
		(c.relkind = any (array['r'::"char", 'p'::"char"]))
		or (c.relkind = any (array['v'::"char", 'f'::"char"]))
		and (pg_relation_is_updatable(c.oid::regclass, false) & 8) = 8
	as is_insertable_into,
	pg_size_pretty(pg_relation_size(c.oid)) as table_size,
	c.reltuples,
	(
		select pg_size_pretty(sum(pg_relation_size(x.indexrelid)))
		from pg_index x
		where x.indrelid = c.oid
	) indexes_size,
	pg_size_pretty(inherited.inherited_size) inherited_size,
	inherited.inherited_reltuples,
	pg_size_pretty(inherited.inherited_indexes_size) inherited_indexes_size
from pg_class c
	left join lateral (
		select
			sum(pg_relation_size(i.inhrelid)) inherited_size,
			sum(pg_class.reltuples) inherited_reltuples,
			(
				select sum(pg_relation_size(indexrelid))
				from pg_index
				where indrelid = any(array_agg(pg_class.oid))
			) inherited_indexes_size
		from pg_inherits i
			join pg_class on i.inhrelid = pg_class.oid
		where i.inhparent = c.oid
	) inherited on c.relkind = 'p'::"char"
where c.relnamespace = $schema.id$ and
	(c.relkind = any (array['r'::"char", 'f'::"char", 'p'::"char"])) and
	not pg_is_other_temp_schema(c.relnamespace) and
	(
		pg_has_role(c.relowner, 'usage'::text) or
		has_table_privilege(c.oid, 'select, insert, update, delete, truncate, references, trigger'::text) or
		has_any_column_privilege(c.oid, 'select, insert, update, references'::text)
	)
/* if version 110000 */
	and
	(
		(
			$tables.id$ is null  and
			coalesce(c.relispartition, false) = false
		)
		or
		(
			coalesce(c.relispartition, false) = true and
			exists (
				select 1
				from pg_inherits p
				where p.inhrelid = c.oid and p.inhparent = $tables.id$
			)
		)
	)
/* endif version */
order by greatest(pg_relation_size(c.oid), inherited.inherited_size) desc