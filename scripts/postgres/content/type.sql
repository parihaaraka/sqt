do
$$
declare
	_type text := '$type.tag$';
	_obj_name text := '$schema.name$.$type.name$';
	_obj_id oid := $type.id$;
	_content text;
	_comment text;
begin

	if _type = 'e' then -- enum
		select
			'CREATE TYPE ' || _obj_name || E' AS ENUM\n(\n' ||
			string_agg(
			E'\t' || quote_literal(e.enumlabel),
			E',\n' order by e.enumsortorder) || E'\n);'
		into _content
		from pg_catalog.pg_enum e
		where e.enumtypid = _obj_id;			
	elsif _type = 'c' then -- composite
		select
			'CREATE TYPE ' || _obj_name || E' AS\n(\n' ||
			string_agg(
			E'\t' || quote_ident(a.attname) || ' ' || pg_catalog.format_type(a.atttypid, a.atttypmod) || coalesce(
				' COLLATE ' || 
					case when a.attcollation = 0 or pg_describe_object('pg_collation'::regclass::oid, a.attcollation, 0) ilike '%default' then null
					else (pg_identify_object('pg_collation'::regclass::oid, a.attcollation, 0)).identity
					end, ''),
			E',\n' order by a.attnum) || E'\n);'
		into _content
		from pg_catalog.pg_type t
			join pg_catalog.pg_attribute a on t.typrelid = a.attrelid and not a.attisdropped
		where t.oid = _obj_id;
	elsif _type = 'r' then -- range
		select
			'CREATE TYPE ' || _obj_name || E' AS RANGE\n(\n' ||
			E'\tSUBTYPE = ' || t.typname || coalesce(E',\n' ||
			E'\tSUBTYPE_OPCLASS = ' || opc.opcname, '') || coalesce(E',\n' ||
			E'\tCOLLATION = ' || 
				case when r.rngcollation = 0 or pg_describe_object('pg_collation'::regclass::oid, r.rngcollation, 0) ilike '%default' then null
				else (pg_identify_object('pg_collation'::regclass::oid, r.rngcollation, 0)).identity
				end, '') || coalesce(E',\n' ||
			E'\tCANONICAL = ' || nullif(r.rngcanonical::text, '-'), '') || coalesce(E',\n' ||
			E'\tSUBTYPE_DIFF = ' || nullif(r.rngsubdiff::text, '-'), '') ||
			E'\n);'
		into _content
		from pg_catalog.pg_range r
			join pg_catalog.pg_type t on r.rngsubtype = t.oid
			left join pg_catalog.pg_opclass opc on r.rngsubopc = opc.oid
		where r.rngtypid = _obj_id;

	elsif _type in ('b','p') then -- base, preudo
		select
			'CREATE TYPE ' || _obj_name || E'\n(\n' ||
			E'\tINPUT = ' || t.typinput || E',\n' ||
			E'\tOUTPUT = ' || t.typoutput || coalesce(E',\n' ||
			E'\tRECEIVE = ' || nullif(t.typreceive::text, '-'), '') || coalesce(E',\n' ||
			E'\tSEND = ' || nullif(t.typsend::text, '-'), '') || coalesce(E',\n' ||
			E'\tTYPMOD_IN = ' || nullif(t.typmodin::text, '-'), '') || coalesce(E',\n' ||
			E'\tTYPMOD_OUT = ' || nullif(t.typmodout::text, '-'), '') || coalesce(E',\n' ||
			E'\tANALYZE = ' || nullif(t.typanalyze::text, '-'), '') || coalesce(E',\n' ||
			E'\tINTERNALLENGTH = ' || nullif(t.typlen, -1)::text, '') || coalesce(E',\n' ||
			E'\tPASSEDBYVALUE' || case when t.typbyval then '' else null end, '') || coalesce(E',\n' ||
			E'\tALIGNMENT = ' || 
				case t.typalign 
				when 'c'::"char" then 'char' 
				when 's'::"char" then 'short' 
				when 'd'::"char" then 'double' 
				else null -- int4
				end, '') || coalesce(E',\n' ||
			E'\tSTORAGE = ' || case t.typstorage
				when 'e'::"char" then 'EXTERNAL' 
				when 'm'::"char" then 'MAIN' 
				when 'x'::"char" then 'EXTENDED' 
				else null -- plain
				end, '') || coalesce(E',\n' ||
			E'\tCATEGORY = ' || quote_literal(nullif(t.typcategory, 'U'::"char")::text), '') || coalesce(E',\n' ||
			E'\tPREFERRED = ' || nullif(t.typispreferred, false), '') || coalesce(E',\n' ||
			E'\tDEFAULT = ' || t.typdefault, '') || coalesce(E',\n' ||
			E'\tELEMENT = ' || el.typname, '') || coalesce(E',\n' ||
			E'\tDELIMITER = ' || nullif(t.typdelim, ','::"char")::text, '') || coalesce(E',\n' ||
			E'\tCOLLATABLE = ' || nullif(t.typcollation != 0, false), '') ||  -- <-- ? not sure
			E'\n);'
		into _content
		from pg_catalog.pg_type t
			left join pg_catalog.pg_type el on t.typelem = el.oid
		where t.oid = _obj_id;

	else
		_content := E'not implemented\n';
	end if;
	
	_comment := format(E'COMMENT ON TYPE %s IS %s;\n', 
						_obj_name, 
						coalesce(E'\n' || quote_literal(obj_description(_obj_id, 'pg_type')), 'NULL'));
	
	raise notice E'%\n\n%', _content, _comment using hint = 'script';
end
$$
