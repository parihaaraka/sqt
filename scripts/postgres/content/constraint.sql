select E'ALTER TABLE ' || nullif(conrelid, 0)::regclass::text || E' DROP CONSTRAINT $constraint.name$;\n\n' ||
	E'ALTER TABLE ' || nullif(conrelid, 0)::regclass::text || E'\n\tADD CONSTRAINT $constraint.name$\n\t' ||
	regexp_replace(pg_get_constraintdef(oid, true), '\m(on|using|with)\M', E'\n\t\\1', 'ig') || E';\n' as script
from pg_constraint
where oid = $constraint.id$