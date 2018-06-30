do
$$
declare 
	_r record;
	_acl text;
	_def_acl text;
	_script text;
	_db_id oid := $database.id$;
	_db_name text := '$database.name$';
begin

	select * into _r
	from pg_database
	where oid = _db_id;

	with tmp as
	(
		select 
			d.defaclobjtype obj,
			a.grantee,
			string_agg(a.privilege_type, ', ') priv,
			a.is_grantable
 		from pg_default_acl d
			cross join lateral aclexplode(d.defaclacl) a
		where d.defaclnamespace = 0 -- db level
		group by d.defaclobjtype, a.grantee, a.is_grantable
	)
	select string_agg(
		E'ALTER DEFAULT PRIVILEGES\n\tGRANT ' || 
		priv || ' ON ' ||
		case tmp.obj
			when 'r'::"char" then 'TABLES'
			when 'f'::"char" then 'FUNCTIONS'
			when 'S'::"char" then 'SEQUENCES'
			when 'T'::"char" then 'TYPES'
			else tmp.obj::text
		end || ' TO ' ||
		case when tmp.grantee = 0 then 'public' else quote_ident(grantee::regrole::text) end || 
		case when tmp.is_grantable then ' WITH GRANT OPTION' else '' end || ';'
		, E'\n')
	into _def_acl
	from tmp;
	
	with tmp as
	(
		select 
			grantee,
			string_agg(privilege_type, ', ') priv,
			array_agg(privilege_type) priv_array,
			is_grantable 
		from aclexplode(_r.datacl) a
		group by grantee, is_grantable
	),
	tmp2 as
	(
		select 
			string_agg(case when grantee = 0 then 'public' else quote_ident(grantee::regrole::text) end, ', ') grantees,
			priv, priv_array, is_grantable
		from tmp
		group by priv, priv_array, is_grantable
	)
	select string_agg(
		'GRANT ' || 
		case when cardinality(priv_array) = 3 /*check exact values?*/ then 'ALL' else priv end || 
		' ON DATABASE ' || _db_name || ' TO ' || grantees ||
		case when is_grantable then ' WITH GRANT OPTION' else '' end || ';'
		, E'\n')
	into _acl
	from tmp2;

	_script := 'CREATE DATABASE ' || _db_name || ' WITH' ||
		E'\n\tOWNER = ' || quote_ident(_r.datdba::regrole::text) ||
		E'\n\tENCODING = ' || quote_literal(pg_encoding_to_char(_r.encoding)) ||
		E'\n\tLC_COLLATE = ' || quote_literal(_r.datcollate) ||
		E'\n\tLC_CTYPE = ' || quote_literal(_r.datctype) ||
		E'\n\tTABLESPACE = ' || (select spcname from pg_tablespace where oid = _r.dattablespace) ||
		case when not _r.datallowconn then E'\n\tALLOW_CONNECTIONS = false' else '' end ||
		case when not _r.datconnlimit != -1 then E'\n\tCONNECTION LIMIT = ' || _r.datconnlimit::text else '' end ||
		case when _r.datistemplate then E'\n\tIS_TEMPLATE = true' else '' end
		|| E';\n\n' ||
		format(E'COMMENT ON DATABASE %s IS %s;\n', 
			_db_name,
			coalesce(E'\n' || quote_literal(shobj_description(_db_id, 'pg_database')), 'NULL')) ||
		coalesce(E'\n' || _def_acl, '') ||
		coalesce(E'\n' || _acl, '');
		 
	raise notice '%', _script using hint = 'script';

end
$$
