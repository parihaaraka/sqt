select 
	'extension' node_type,
	extname || '<span class="light"> ' || extversion || '</span>' ui_name,
	extname "name",
	oid id
from pg_extension