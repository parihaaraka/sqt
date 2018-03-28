select E'/*\nDROP TRIGGER $trigger.name$ ON $schema.name$.$table.name$;\n\n' ||
	regexp_replace(pg_get_triggerdef($trigger.id$, true) || E';\n*/\n\n', '\m(after|before|on|for|execute)\M', E'\n\t\\1', 'ig') ||
	pg_get_functiondef((select tgfoid from pg_trigger where oid = $trigger.id$)) || E';\n' as script
