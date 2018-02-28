select 
    'function' node_type,
    p.oid id,
    format(
	'%I<span class="light">(%s)</span>', --<br/>&nbsp;-> %s</span>',
	p.proname, 
	--pg_get_function_identity_arguments(p.oid),
	oidvectortypes(p.proargtypes)
	--pg_get_function_result(p.oid)
	) ui_name,
    'function.png' icon,
    p.proname || oidvectortypes(p.proargtypes) sort1
from pg_proc p 
where p.pronamespace = $schema.id$