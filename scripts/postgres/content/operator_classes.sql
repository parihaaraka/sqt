select 
	op.opcname || ' (' || m.amname || ')' "opclass",
	op.oid,
	op.opcowner::regrole "owner",
	obj_description(op.oid, 'pg_opclass') "comment"
from pg_catalog.pg_opclass op
	left join pg_catalog.pg_am m on op.opcmethod = m.oid
where op.opcnamespace = $schema.id$
order by 1
