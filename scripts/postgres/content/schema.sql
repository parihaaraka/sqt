select 
	E'CREATE SCHEMA $schema.name$\n\tAUTHORIZATION ' || 
	quote_ident(nspowner::regrole::text) ||';' script
from pg_catalog.pg_namespace
where oid = $schema.id$;
