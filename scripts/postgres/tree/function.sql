select
	'fn_argument' node_type,
	arg.i id,
	coalesce(arg.n || '&nbsp;&nbsp;', '') || '<span class="light">' || 
		format_type(arg.t, null) ||
		'</span>' ui_name,
	quote_ident(arg.n) "name",
	arg.i sort1,
	arg.n sort2
from pg_proc p
	cross join unnest(p.proargnames, p.proargmodes, p.proargtypes::oid[]) with ordinality arg(n, m, t, i)
where p.oid = $function.id$ and arg.t is not null
