select 
	p.proname || ' (' || oidvectortypes(p.proargtypes) || ')' "procedure",
	p.oid,
	p.proowner::regrole "owner",
	obj_description(p.oid, 'pg_proc') "comment"
from pg_proc p
where p.pronamespace = $schema.id$ and p.prokind = 'p'
order by 1
