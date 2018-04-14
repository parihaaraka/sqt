select E'DROP INDEX $schema.name$.$index.name$;\n\n' ||
	regexp_replace(pg_get_indexdef($index.id$), '\m(on|using|with)\M', E'\n\t\\1', 'ig') || 
	case 
		when $index.tag$ = 0 then '' 
		else E'\n\tTABLESPACE ' || (pg_identify_object('pg_tablespace'::regclass::oid, $index.tag$, 0)).identity
	end ||
	E';\n' as script