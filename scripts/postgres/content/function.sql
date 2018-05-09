do
$$
declare
	_fn_oid oid := $function.id$;
	_comment text := obj_description(_fn_oid, 'pg_proc');
	_fn_name text := '$schema.name$.$function.name$';
	_tmp text;
begin

	if '$children.ids$' = '-1' then
		select E'/*\nDROP FUNCTION ' || _fn_name || '(' || oidvectortypes(p.proargtypes) || E');\n' ||
			E'COMMENT ON FUNCTION ' || _fn_name || '(' || oidvectortypes(p.proargtypes) || ') IS ' || 
				coalesce(E'\n' || quote_literal(_comment), 'NULL') || E';\n*/\n'
		into _tmp
		from pg_proc p
		where p.oid = _fn_oid;
	
		raise notice E'%\n%;', _tmp, rtrim(pg_get_functiondef(_fn_oid), E'\n')
			using hint = 'script';
	else
		with tmp as
		(
			select a.n, row_number() over () as i
			from pg_proc p
				cross join unnest(p.proargnames) with ordinality a(n, i)
			where p.oid = _fn_oid and a.i in ($children.ids$)
		)
		select 'SELECT * FROM ' || _fn_name || '(' ||
			string_agg(n || '=>$' || i::text, ', ') || E');\n'
		into _tmp
		from tmp;
		
		raise notice E'%', _tmp using hint = 'script';
	end if;
end
$$