select distinct
	'pg_settings_group' node_type,
	category "name",
	category ui_name,
	category sort1
from pg_settings 
