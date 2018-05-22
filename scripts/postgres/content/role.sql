select 
	format(E'COMMENT ON ROLE %I IS %s;\n', 
		'$role.name$', 
		coalesce(E'\n' || quote_literal(shobj_description($role.id$, 'pg_authid')), 'NULL')) script
