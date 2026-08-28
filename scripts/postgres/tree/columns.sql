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
