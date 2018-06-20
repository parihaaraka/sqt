select
	'index' node_type,
	i.relname || 
		' <span class="light">' || am.amname || ' <i>' ||
			case when x.indisprimary then 'P' else '' end ||
			case when x.indisunique then 'u' else '' end ||
		'</i></span>' ui_name,
	i.oid id,
	i.relname "name",
	i.reltablespace tag,
	i.relname sort1
from pg_index x
	join pg_class c on c.oid = x.indrelid
	join pg_class i on i.oid = x.indexrelid
	left join pg_am am on i.relam = am.oid
where c.oid = $table.id$ and x.indislive