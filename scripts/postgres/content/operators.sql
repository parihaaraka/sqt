select
	op.oprname || ' (' || 
	case
	when op.oprkind = 'b'::"char" then l.typname || ',' || r.typname
	else coalesce(l.typname, r.typname)
	end || ') → ' || res.typname "operator",
	op.oprowner::regrole "owner",
	obj_description(op.oid, 'pg_operator') "comment"
from pg_catalog.pg_operator op
	left join pg_catalog.pg_type l on op.oprleft = l.oid
	left join pg_catalog.pg_type r on op.oprright = r.oid
	left join pg_catalog.pg_type res on op.oprresult = res.oid
where op.oprnamespace = $schema.id$
order by 1
