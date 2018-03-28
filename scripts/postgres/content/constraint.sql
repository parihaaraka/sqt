select E'ALTER TABLE $schema.name$.$table.name$ DROP CONSTRAINT $constraint.name$;\n\n' ||
	E'ALTER TABLE $schema.name$.$table.name$\n\tADD CONSTRAINT $constraint.name$\n\t' ||
	regexp_replace(pg_get_constraintdef($constraint.id$, true), '\m(on|using|with)\M', E'\n\t\\1', 'ig') || E';\n' as script