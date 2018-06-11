do
$$
declare
	_obj_id oid := $operator.id$;
	_pgns regnamespace := 'pg_catalog'::regnamespace;
	_obj_name text := case when $schema.id$ = _pgns then '' else  '$schema.name$.' end || '$operator.name$';
	_content text;
	_comment text;
	_ltypname text;
	_rtypname text;
begin

	select
		'CREATE OPERATOR ' || _obj_name || E'\n(\n' ||
		E'\tPROCEDURE = ' || 
			case when p.pronamespace = _pgns then '' else quote_ident(p.pronamespace::regnamespace::text) || '.' end || 
			quote_ident(p.proname) || coalesce(E',\n' ||
		E'\tLEFTARG = ' || 
			case when l.typnamespace = _pgns then '' else quote_ident(l.typnamespace::regnamespace::text) || '.' end || 
			l.typname, '') || coalesce(E',\n' ||
		E'\tRIGHTARG = ' || 
			case when r.typnamespace = _pgns then '' else quote_ident(r.typnamespace::regnamespace::text) || '.' end || 
			r.typname, '') || coalesce(E',\n' ||
		E'\tCOMMUTATOR = ' || 
			case 
				when com.oprnamespace != op.oprnamespace 
				then 'OPERATOR(' || quote_ident(com.oprnamespace::regnamespace::text) || '.' || com.oprname || ')'
				else com.oprname
			end, '') || coalesce(E',\n' ||
		E'\tNEGATOR = ' || 
			case 
				when neg.oprnamespace != op.oprnamespace 
				then 'OPERATOR(' || quote_ident(neg.oprnamespace::regnamespace::text) || '.' || neg.oprname || ')'
				else neg.oprname
			end, '') || coalesce(E',\n' ||
		E'\tRESTRICT = ' || 
			case when rst.pronamespace = _pgns then '' else quote_ident(rst.pronamespace::regnamespace::text) || '.' end || 
			quote_ident(rst.proname), '') || coalesce(E',\n' ||
		E'\tJOIN = ' || 
			case when j.pronamespace = _pgns then '' else quote_ident(j.pronamespace::regnamespace::text) || '.' end || 
			quote_ident(j.proname), '') || coalesce(E',\n' ||
		E'\tHASHES' || case when op.oprcanhash then '' else null end, '') || coalesce(E',\n' ||
		E'\tMERGES' || case when op.oprcanmerge then '' else null end, '') ||
		E'\n);', l.typname, r.typname
	into _content, _ltypname, _rtypname
	from pg_catalog.pg_operator op
		left join pg_catalog.pg_type l on op.oprleft = l.oid
		left join pg_catalog.pg_type r on op.oprright = r.oid
		left join pg_catalog.pg_operator com on op.oprcom = com.oid
		left join pg_catalog.pg_operator neg on op.oprnegate = neg.oid
		left join pg_catalog.pg_proc rst on op.oprrest = rst.oid
		left join pg_catalog.pg_proc j on op.oprjoin = j.oid
		left join pg_catalog.pg_proc p on op.oprcode = p.oid
	where op.oid = _obj_id;
	
	_comment := format(E'COMMENT ON OPERATOR %s(%s, %s) IS %s;\n', 
						_obj_name, coalesce(_ltypname, 'NONE'), coalesce(_rtypname, 'NONE'),
						coalesce(E'\n' || quote_literal(obj_description(_obj_id, 'pg_operator')), 'NULL'));
	
	raise notice E'%\n\n%', _content, _comment using hint = 'script';
end
$$
