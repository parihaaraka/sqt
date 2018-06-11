select
	'operator_class' node_type,
	right(left(xmlelement(name x, oc.opcname)::text, -4), -3) || ' <i><span class="light">(' || 
		m.amname || ')' || '</span></i>' ui_name,
	oc.opcname "name",
	oc.oid id,
	oc.opcname || m.amname sort1,
	m.amname || oc.opcname sort2
from pg_catalog.pg_opclass oc
	left join pg_catalog.pg_am m on oc.opcmethod = m.oid
where oc.opcnamespace = $schema.id$
