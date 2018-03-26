select
	'sequence' node_type,
	s.relname || coalesce('<i><span class="light"> &rarr; ' || quote_ident(t.relname) || '(' || quote_ident(a.attname) || ')</span></i>', '') ui_name,
	s.oid id,
	s.relname "name",
	s.relname sort1
from pg_class s
	left join
	(
		pg_depend d
			join pg_class t on d.refobjid = t.oid
			join pg_attribute a on a.attnum = d.refobjsubid and t.oid = a.attrelid
	) on s.oid = d.objid
where s.relkind = 'S' and s.relnamespace = $schema.id$;
