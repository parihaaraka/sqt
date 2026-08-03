do
$$
declare
	_type text := '$domain.tag$';
	_obj_name text := '$schema.name$.$domain.name$';
	_obj_id oid := $domain.id$;
	_content text;
	_comment text;
	_nested_types text;
begin

	select
		'CREATE DOMAIN ' || _obj_name || E' AS ' || pg_catalog.format_type(t.typbasetype, t.typtypmod) ||
		case
			-- do we have a simpler way to detect default collation oid?
			when t.typcollation = 0 or pg_describe_object('pg_collation'::regclass::oid, t.typcollation, 0) ilike '%default%' then ''
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
		) || ';',

		trim(
			case when t2.typbasetype != 0 then '-- → ' || pg_catalog.format_type(t2.typbasetype, t2.typtypmod) || E'\n' else '' end ||
			case when t3.typbasetype != 0 then '--  → ' || pg_catalog.format_type(t3.typbasetype, t3.typtypmod) || E'\n' else '' end ||
			case when t4.typbasetype != 0 then '--  → ' || pg_catalog.format_type(t4.typbasetype, t4.typtypmod) || E'\n' else '' end,
		E'\n')
	into _content, _nested_types
	from pg_type t
		left join pg_catalog.pg_constraint c on t.oid = c.contypid

		left join pg_type t2 on t.typtype = 'd'::"char" and t.typbasetype != 0 and t.typbasetype = t2.oid
		left join pg_type t3 on t2.typtype = 'd'::"char" and t2.typbasetype != 0 and t2.typbasetype = t3.oid
		left join pg_type t4 on t2.typtype = 'b'::"char" and t2.typcategory = 'A' and t2.typelem != 0 and t2.typelem = t4.oid
	where t.oid = _obj_id;

	_comment := format(E'COMMENT ON DOMAIN %s IS %s;\n',
		_obj_name,
		coalesce(E'\n' || quote_literal(obj_description(_obj_id, 'pg_type')), 'NULL'));

	raise notice E'%\n%\n%', _content, _nested_types, _comment using hint = 'script';
end
$$
