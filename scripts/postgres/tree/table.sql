with col as
(
	select
		'columns' node_type,
		'<i>Columns</i>' as ui_name,
		null::int id,
		null::text "name",
		true allow_multiselect,
		'table-select-column.png' icon,
		0::int sort1,
		'0' sort2
	from pg_catalog.pg_class c
	where c.oid = $table.id$ and
		-- relnatts counts dropped columns as well, so the folder appears (or
		-- not) according to what the tree is actually about to show
		(
			select count(*)
			from pg_catalog.pg_attribute a
			where a.attrelid = c.oid and a.attnum > 0 and not a.attisdropped
		) > 15
)
select * from col
union all
select
	'column' node_type,
	a.attname ||
		' <span class="light">' ||
		case when x.indexrelid is null then '' else ' ⚷ ' end ||
		pg_catalog.format_type(a.atttypid, a.atttypmod) ||
		case when a.attnotnull then ' NOT NULL' else '' end ||
		'</span>' as ui_name,
	a.attnum id,
	a.attname "name",
	null,
	--case when x.indexrelid is null then 'transparent.png' else 'key.png' end icon,
	null icon,
	attnum sort1,
	'0' || a.attname sort2
from pg_catalog.pg_attribute a
	left join pg_index x on
		$table.id$ = x.indrelid and
		x.indisprimary and
		a.attnum = any(x.indkey) and
		x.indislive
where not exists (select 1 from col) and a.attnum > 0 and not a.attisdropped and a.attrelid = $table.id$
union all
select
	'tables',
	'<i>Partitions</i>',
	$table.id$,
	null,
	null,
	'tables.png',
	x'7FFFFFF0'::int,
	'1'
where '$table.tag$' = 'p'
union all
select
	'triggers',
	'<i>Triggers</i>',
	null,
	null,
	null,
	'arrow-transition.png',
	x'7FFFFFF1'::int,
	'1'
where '$table.tag$' != 'f'
union all
select
	'indexes',
	'<i>Indexes</i>',
	null,
	null,
	null,
	'paper-plane.png',
	x'7FFFFFF2'::int,
	'2'
where '$table.tag$' not in ('v', 'f')
union all
select
	'constraints',
	'<i>Constraints</i>',
	null,
	null,
	null,
	'traffic-cone.png',
	x'7FFFFFF3'::int,
	'3'
where '$table.tag$' in ('r', 'p')
union all
select
	'dependent_constraints',
	'<i>Dependent constraints</i>',
	null,
	null,
	null,
	'traffic-cone.png',
	x'7FFFFFF3'::int,
	'4'
where '$table.tag$' in ('r', 'p')
union all
select
	'rules',
	'<i>Rules</i>',
	null,
	null,
	null,
	'image-saturation-up.png',
	x'7FFFFFF4'::int,
	'5'
where '$table.tag$' != 'f'
union all
select
	'table_locks',
	'<i>Locks</i>',
	null,
	null,
	null,
	null,
	x'7FFFFFF5'::int,
	'6'
union all
select
	'table_stats',
	'<i>Per-column statistics</i>',
	null,
	null,
	null,
	null,
	x'7FFFFFF6'::int,
	'7'
