select E'DROP INDEX $schema.name$.$index.name$;\n\n' ||
	regexp_replace(pg_get_indexdef($index.id$), '\m(on|using|with)\M', E'\n\t\\1', 'ig') || E';\n' as script