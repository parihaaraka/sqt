select regexp_replace(pg_get_indexdef($index.id$), '\m(on|using|with)\M', E'\n\t\\1', 'ig') as script
