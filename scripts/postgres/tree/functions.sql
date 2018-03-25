select
	case when p.proisagg then 'agg_function' else 'function' end node_type,
	p.oid id,
	format(
		'%I<span class="light">(%s)</span> <i>%s</i>', --<br/>&nbsp;-> %s</span>',
		p.proname, 
		--pg_get_function_identity_arguments(p.oid),
		oidvectortypes(p.proargtypes),
		--pg_get_function_result(p.oid)
		case when p.proisagg then '&#931;' else '' end
		) ui_name,
	quote_ident(p.proname) "name",
	--'function.png' icon,
	p.proname || oidvectortypes(p.proargtypes) sort1
from pg_proc p 
where p.pronamespace = $schema.id$