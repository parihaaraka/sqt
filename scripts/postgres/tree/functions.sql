/* if version 110000 */
select
	case p.prokind when 'a' then 'agg_function' else 'function' end node_type,
	p.oid id,
	format(
		'%I <span class="light">(%s)</span> <i>%s</i>', --<br/>&nbsp;-> %s</span>',
		p.proname, 
		--pg_get_function_identity_arguments(p.oid),
		oidvectortypes(p.proargtypes),
		--pg_get_function_result(p.oid)
		case 
			when p.prokind = 'a' then '&#931;'
			when t.oid is not null then ' tr'
			else '' 
		end
		) ui_name,
	quote_ident(p.proname) "name",
	--'function.png' icon,
	(p.prokind != 'a' and p.proargnames is not null) as allow_multiselect,
	p.proname || oidvectortypes(p.proargtypes) sort1,
	case 
		when p.prokind = 'a' then '2'
		when t.oid is not null then '1'
		else '0'
	end || p.proname || oidvectortypes(p.proargtypes) sort2
from pg_proc p
	left join lateral (
		select t.oid 
		from pg_trigger t 
		where t.tgfoid = p.oid and not t.tgisinternal 
		limit 1) t on true
where p.pronamespace = $schema.id$ and p.prokind != 'p'

/* elif version 80000 */
select
	case when p.proisagg then 'agg_function' else 'function' end node_type,
	p.oid id,
	format(
		'%I <span class="light">(%s)</span> <i>%s</i>', --<br/>&nbsp;-> %s</span>',
		p.proname, 
		--pg_get_function_identity_arguments(p.oid),
		oidvectortypes(p.proargtypes),
		--pg_get_function_result(p.oid)
		case 
			when p.proisagg then '&#931;'
			when t.oid is not null then ' tr'
			else '' 
		end
		) ui_name,
	quote_ident(p.proname) "name",
	--'function.png' icon,
	(not p.proisagg and p.proargnames is not null) as allow_multiselect,
	p.proname || oidvectortypes(p.proargtypes) sort1,
	case 
		when p.proisagg then '2'
		when t.oid is not null then '1'
		else '0'
	end || p.proname || oidvectortypes(p.proargtypes) sort2
from pg_proc p
	left join lateral (
		select t.oid 
		from pg_trigger t 
		where t.tgfoid = p.oid and not t.tgisinternal 
		limit 1) t on true
where p.pronamespace = $schema.id$
/* endif version */