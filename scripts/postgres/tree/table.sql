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
where a.attnum > 0 and not a.attisdropped and a.attrelid = $table.id$
union all
select
	'tables',
	'<i>Partitions</i>',
	$table.id$,
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
	'traffic-cone.png',
	x'7FFFFFF3'::int,
	'3'
where '$table.tag$' in ('r', 'p')
union all
select
	'rules',
	'<i>Rules</i>',
	null,
	null,
	'image-saturation-up.png',
	x'7FFFFFF4'::int,
	'4'
where '$table.tag$' != 'f'