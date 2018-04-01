select 'DROP EXTENSION ' || quote_ident(extname) || E';\n\n' ||
	'CREATE EXTENSION ' || quote_ident(extname) ||
	E'\n\tSCHEMA ' || quote_ident(extnamespace::regnamespace::text) ||
	E'\n\tVERSION ' || quote_ident(extversion) || ';' as script
from pg_extension
where oid = $extension.id$;