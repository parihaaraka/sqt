select 
	E'CREATE SCHEMA $schema.name$\n\tAUTHORIZATION ' || 
	quote_ident(nspowner::regrole::text) || E';\n\n' || 
	format(E'COMMENT ON SCHEMA %s IS %s;\n', 
		'$schema.name$', 
		coalesce(E'\n' || quote_literal(obj_description($schema.id$, 'pg_namespace')), 'NULL'))
	script
from pg_catalog.pg_namespace
where oid = $schema.id$;
