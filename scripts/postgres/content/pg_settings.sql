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
	sourceline
/* if version 90500 */
	,pending_restart
/* endif version */
from pg_settings
where boot_val is distinct from setting
order by "name"
