select regexp_replace(pg_get_ruledef($rule.id$, true), '(\s+)(on|where|do)\s+', E'\n\t\\2 ', 'ig') as script;
