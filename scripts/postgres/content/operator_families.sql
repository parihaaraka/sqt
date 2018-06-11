select f.opfname || coalesce(' (' || m.amname || ')', '') "opfamily",
	f.opfowner::regrole "owner",
	obj_description(f.oid, 'pg_opfamily') "comment"
from pg_catalog.pg_opfamily f
	left join pg_catalog.pg_am m on f.opfmethod = m.oid
where f.opfnamespace = $schema.id$
order by 1
