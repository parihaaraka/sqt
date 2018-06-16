select 
	'operator' node_type,
	right(left(xmlelement(name x, op.oprname)::text, -4), -3) || ' <i><span class="light">(' || 
		case
		when op.oprkind = 'b'::"char" then l.typname || ',' || r.typname
		else coalesce(l.typname, r.typname)
		end || ') → ' || res.typname || '</span></i>' ui_name,
	op.oprname "name",
	op.oid id,
	op.oprname || case
		when op.oprkind = 'b'::"char" then l.typname || ',' || r.typname
		else coalesce(l.typname, r.typname)
		end sort1
from pg_catalog.pg_operator op
	left join pg_catalog.pg_type l on op.oprleft = l.oid
	left join pg_catalog.pg_type r on op.oprright = r.oid
	left join pg_catalog.pg_type res on op.oprresult = res.oid
where op.oprnamespace = $schema.id$