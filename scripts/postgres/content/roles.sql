do
$$
declare 
	_r record;
	_script text := '';
	_cursor refcursor;
	_levels jsonb;
begin
	-- provide corect order of roles
	with recursive tmp as
	(
		select r.oid roleid, 0 lvl
		from pg_roles r
		where not exists (select 1 from pg_auth_members where member = r.oid)
		union all
		select m.member, tmp.lvl + 1
		from tmp
			join pg_auth_members m on tmp.roleid = m.roleid
	),
	levels as
	(
		select roleid, min(lvl) lvl
		from tmp
		group by roleid
	)
	select jsonb_object_agg(roleid::text, lvl) into _levels
	from levels;
	
	if has_table_privilege('pg_authid'::regclass, 'select') then
		open _cursor no scroll for
			select r.oid, r.*
			from pg_catalog.pg_authid r
			where r.oid in ($children.ids$) or '$children.ids$' = '-1'
			order by (_levels->>(r.oid::text))::int, r.rolcanlogin;
	else
		open _cursor no scroll for
			select *
			from pg_catalog.pg_roles r
			where r.oid in ($children.ids$) or '$children.ids$' = '-1'
			order by (_levels->>(r.oid::text))::int, r.rolcanlogin;
	end if;

	loop
		fetch _cursor into _r;
		exit when not found;

		_script := _script ||
			'CREATE ROLE ' || quote_ident(_r.rolname) || 
				coalesce(' WITH' || 
					nullif(
						case when _r.rolcanlogin then E'\n\tLOGIN' else '' end ||
						case when _r.rolsuper then E'\n\tSUPERUSER' else '' end ||
						case when not _r.rolinherit then E'\n\tNOINHERIT' else '' end ||
						case when _r.rolcreaterole then E'\n\tCREATEROLE' else '' end ||
						case when _r.rolcreatedb then E'\n\tCREATEDB' else '' end ||
						case when _r.rolreplication then E'\n\tREPLICATION' else '' end ||
						case when _r.rolbypassrls then E'\n\tBYPASSRLS' else '' end ||
						case when _r.rolconnlimit != -1 then E'\n\tCONNECTION LIMIT ' || _r.rolconnlimit::text else '' end ||
						case when _r.rolpassword is not null then E'\n\tENCRYPTED PASSWORD ' || quote_literal(_r.rolpassword) else '' end ||
						case when _r.rolvaliduntil is not null then E'\n\tVALID UNTIL ' || quote_literal(_r.rolvaliduntil::text) else '' end ||
						coalesce(E'\n\tIN ROLE ' || 
							(select string_agg(b.rolname, ', ')
							from pg_catalog.pg_auth_members m
								join pg_catalog.pg_roles b on m.roleid = b.oid
							where m.member = _r.oid and not m.admin_option), '')
							, '')
					, '') || E';\n' ||
				coalesce(
					(select string_agg(E'GRANT ' || quote_ident(b.rolname) || ' TO ' || 
						quote_ident(_r.rolname) || ' WITH ADMIN OPTION;', E'\n')
					from pg_catalog.pg_auth_members m
						join pg_catalog.pg_roles b on m.roleid = b.oid
					where m.member = _r.oid and m.admin_option) || E'\n'
					, '') ||
			coalesce('COMMENT ON ROLE ' || quote_ident(_r.rolname) || E' IS\n' ||
				quote_literal(shobj_description(_r.oid, 'pg_authid')) || E'\n', '');
	end loop;
	close _cursor;
	
	raise notice '%', _script using hint = 'script';
end
$$
