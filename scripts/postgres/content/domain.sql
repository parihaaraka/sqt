do
$$
declare
	_type text := '$domain.tag$';
	_obj_name text := '$schema.name$.$domain.name$';
	_obj_id oid := $domain.id$;
	_content text;
	_comment text;
begin

	select
		'CREATE DOMAIN ' || _obj_name || E' AS ' || pg_catalog.format_type(t.typbasetype, t.typtypmod) ||
		case
			-- do we have a simpler way to detect default collation oid?
			when t.typcollation = 0 or pg_describe_object('pg_collation'::regclass::oid, t.typcollation, 0) ilike '%default' then ''
			else E'\n\tCOLLATE ' || (pg_identify_object('pg_collation'::regclass::oid, t.typcollation, 0)).identity
		end ||
		coalesce(E'\n\tDEFAULT ' || t.typdefault, '') ||
		case when t.typnotnull then E'\n\tNOT NULL' else '' end ||
		coalesce(
			(
				select string_agg(E'\n\tCONSTRAINT ' || quote_ident(c.conname) || ' ' || pg_catalog.pg_get_constraintdef(c.oid, true), '')
				from pg_catalog.pg_constraint c 
				where c.contypid = t.oid
			), ''
		) || ';'
	into _content
	from pg_type t
		left join pg_catalog.pg_constraint c on t.oid = c.contypid 
	where t.oid = _obj_id;
	
	_comment := format(E'COMMENT ON DOMAIN %s IS %s;\n', 
		_obj_name,
		coalesce(E'\n' || quote_literal(obj_description(_obj_id, 'pg_type')), 'NULL'));
	
	raise notice E'%\n\n%', _content, _comment using hint = 'script';
end
$$
