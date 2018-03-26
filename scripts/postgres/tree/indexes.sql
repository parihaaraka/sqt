select
	'index' node_type,
	i.relname || 
		' <i><span class="light">' ||
			case when x.indisprimary then 'P' else '' end ||
			case when x.indisunique then 'u' else '' end ||
		'</span></i>' ui_name,
	i.oid id,
	i.relname "name",
	i.relname sort1
from pg_index x
	join pg_class c on c.oid = x.indrelid
	join pg_class i on i.oid = x.indexrelid
where c.oid = $table.id$