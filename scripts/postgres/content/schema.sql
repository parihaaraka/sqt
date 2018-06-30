do
$$
declare
	_acl text;
	_def_acl text;
	_script text;
	_obj_id oid := $schema.id$;
	_obj_name text := '$schema.name$';
	_nspacl aclitem[];
begin

	select
		'CREATE SCHEMA ' || _obj_name || E'\n\tAUTHORIZATION ' || 
		quote_ident(nspowner::regrole::text) || E';\n\n' || 
		format(E'COMMENT ON SCHEMA %s IS %s;\n', 
			_obj_name, 
			coalesce(E'\n' || quote_literal(obj_description(_obj_id, 'pg_namespace')), 'NULL')),
		nspacl
	into _script, _nspacl
	from pg_catalog.pg_namespace
	where oid = _obj_id;

	with tmp as
	(
		select 
			d.defaclobjtype obj,
			a.grantee,
			string_agg(a.privilege_type, ', ') priv,
			array_agg(a.privilege_type) priv_array,
			a.is_grantable
 		from pg_default_acl d
			cross join lateral aclexplode(d.defaclacl) a
		where d.defaclnamespace = _obj_id
		group by d.defaclobjtype, a.grantee, a.is_grantable
	),
	tmp2 as
	(
		select 
			string_agg(case when grantee = 0 then 'public' else quote_ident(grantee::regrole::text) end, ', ') grantees,
			obj, priv, priv_array, is_grantable
		from tmp
		group by obj, priv, priv_array, is_grantable
	)
	select string_agg(
		E'ALTER DEFAULT PRIVILEGES IN SCHEMA ' || _obj_name || 
		E'\n\tGRANT ' || 
		case
			when obj = 'r'::"char" and cardinality(priv_array) = 7 then 'ALL'
			else priv
		end ||
		' ON ' ||
		case obj
			when 'r'::"char" then 'TABLES'
			when 'f'::"char" then 'FUNCTIONS'
			when 'S'::"char" then 'SEQUENCES'
			when 'T'::"char" then 'TYPES'
			else obj::text
		end || E' TO ' || grantees ||
		case when is_grantable then ' WITH GRANT OPTION' else '' end || ';'
		, E'\n')
	into _def_acl
	from tmp2;
	
	with tmp as
	(
		select 
			grantee,
			string_agg(privilege_type, ', ') priv,
			array_agg(privilege_type) priv_array,
			is_grantable
		from aclexplode(_nspacl) a
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
		case when cardinality(priv_array) = 2 /*check exact values?*/ then 'ALL' else priv end || 
		' ON SCHEMA ' || _obj_name || ' TO ' || grantees ||
		case when is_grantable then ' WITH GRANT OPTION' else '' end || ';'
		, E'\n')
	into _acl
	from tmp2;

	_script := _script ||
		coalesce(E'\n' || _def_acl, '') ||
		coalesce(E'\n' || _acl, '');

	raise notice '%', _script using hint = 'script';

end
$$
