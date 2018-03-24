select
	"name",
	setting,
	unit,
	short_desc || coalesce(extra_desc) description,
	context,
	vartype,
	source,
	min_val,
	max_val,
	enumvals,
	boot_val,
	reset_val,
	sourcefile,
	sourceline,
	pending_restart
from pg_settings 
where category = '$pg_settings_group.name$'
order by "name"
