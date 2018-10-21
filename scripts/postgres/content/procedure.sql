do
$$
declare
	_proc_oid oid := $procedure.id$;
	_comment text := obj_description(_proc_oid, 'pg_proc');
	_proc_name text := '$schema.name$.$procedure.name$';
	_tmp text;
begin

	if '$children.ids$' = '-1' then
		select E'/*\nDROP PROCEDURE ' || _proc_name || '(' || oidvectortypes(p.proargtypes) || E');\n' ||
			E'COMMENT ON PROCEDURE ' || _proc_name || '(' || oidvectortypes(p.proargtypes) || ') IS ' || 
				coalesce(E'\n' || quote_literal(_comment), 'NULL') || E';\n*/\n'
		into _tmp
		from pg_proc p
		where p.oid = _proc_oid;
	
		raise notice E'%\n%;', _tmp, rtrim(pg_get_functiondef(_proc_oid), E'\n')
			using hint = 'script';
	else
		with tmp as
		(
			select a.n, a.i, row_number() over () as rn
			from pg_proc p
				cross join lateral (
					-- stuff to support functions with anonimous arguments
					select a.n, coalesce(a.i, s.i) i
					from generate_series(1, p.pronargs) s(i)
						left join unnest(p.proargnames) with ordinality a(n, i) on s.i = a.i
					) a
			where p.oid = _proc_oid and a.i in ($children.ids$)
		)
		select 'CALL ' || _proc_name || '(' ||
			string_agg(
					coalesce(n || '=>', 
						case when rn = 1 then '/*you can''t use named notation with anon arguments*/' 
						else '' 
						end) || 
				'$' || rn::text, 
				', ' order by tmp.i) || E');\n'
		into _tmp
		from tmp;
		
		raise notice E'%', _tmp using hint = 'script';
	end if;
end
$$
