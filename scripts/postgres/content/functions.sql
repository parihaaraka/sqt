select 
	p.proname || ' (' || oidvectortypes(p.proargtypes) || ')' "function",
	p.oid, 
	case when p.proisagg then 'aggregate' when t.oid is not null then ' trigger' end "type",
	p.proowner::regrole "owner",
	obj_description(p.oid, 'pg_proc') "comment"
from pg_proc p
	left join lateral (
		select t.oid 
		from pg_trigger t 
		where t.tgfoid = p.oid and not t.tgisinternal 
		limit 1) t on true
where p.pronamespace = $schema.id$
order by 1