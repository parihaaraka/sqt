do
$$
declare
	_type text := '$type.tag$';
	_obj_name text := '$schema.name$.$type.name$';
	_obj_id oid := $type.id$;
	_content text;
begin

	if _type = 'e' then -- enum
		select
			'CREATE TYPE ' || _obj_name || E' AS ENUM\n(\n' ||
			string_agg(
			E'\t' || quote_literal(e.enumlabel),
			E',\n' order by e.enumsortorder) || E'\n);'
		into _content
		from pg_enum e
		where e.enumtypid = _obj_id;			
	elsif _type = 'c' then -- composite
		select
			'CREATE TYPE ' || _obj_name || E' AS\n(\n' ||
			string_agg(
			E'\t' || quote_ident(a.attname) || ' ' || pg_catalog.format_type(a.atttypid, a.atttypmod),
			E',\n' order by a.attnum) || E'\n);'
		into _content
		from pg_type t
			join pg_attribute a on t.typrelid = a.attrelid and not a.attisdropped
		where t.oid = _obj_id;
	else
		_content := 'not implemented';
	end if;
	
	raise notice '%', _content using hint = 'script';
end
$$
