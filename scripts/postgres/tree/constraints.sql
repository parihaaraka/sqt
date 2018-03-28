select
	'constraint' node_type,
	conname || coalesce(
		'&nbsp;&nbsp;<i><span class="light">' ||
			case contype::text
				when 'f' then ' &rarr; ' || nullif(confrelid, 0)::regclass::text
				when 'c' then 'check' 
				when 'p' then 'primary key' 
				when 'u' then 'unique' 
				when 't' then 'trigger' 
				when 'x' then 'exclusion' 
				else null 
			end || '</span></i>',
		'') ui_name,
	oid id,
	conname "name"
from pg_constraint
where conrelid = $table.id$
