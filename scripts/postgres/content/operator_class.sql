do
$$
declare
	_obj_name text := '$schema.name$.$operator_class.name$';
	_obj_id oid := $operator_class.id$;
	_operators text;
	_functions text;
	_content text;
	_comment text;
	_pgns regnamespace := 'pg_catalog'::regnamespace;
	_r record;
begin

	select 
		c.*, t.typnamespace, t.typname, m.amname, f.opfnamespace, f.opfname, 
		st.typnamespace storage_typnamespace,
		st.typname storage_typname
	into _r
	from pg_catalog.pg_opclass c
		join pg_catalog.pg_type t on c.opcintype = t.oid
		left join pg_catalog.pg_type st on c.opckeytype = st.oid
		join pg_catalog.pg_am m on c.opcmethod = m.oid
		join pg_catalog.pg_opfamily f on c.opcfamily = f.oid
	where c.oid = _obj_id;

	select
		string_agg(
			E'\t\tOPERATOR ' || amop.amopstrategy || ' ' || op.oprname ||
			case when l.oid = _r.opcintype and r.oid = _r.opcintype then '' else ' (' ||
				coalesce(case when l.typnamespace = _pgns then '' else quote_ident(l.typnamespace::regnamespace::text) || '.' end || l.typname, 'NONE') || ', ' || 
				coalesce(case when r.typnamespace = _pgns then '' else quote_ident(r.typnamespace::regnamespace::text) || '.' end || r.typname, 'NONE') || ')'
			end || 
			case when amop.amoppurpose = 's'::"char" then '' else ' FOR ORDER BY ' || f.opfname end,
			E',\n' order by amop.amopstrategy)
	into _operators
	from pg_catalog.pg_amop amop
		join pg_catalog.pg_operator op on amop.amopopr = op.oid
		left join pg_catalog.pg_opfamily f on amop.amopsortfamily = f.oid
		left join pg_catalog.pg_type l on op.oprleft = l.oid
		left join pg_catalog.pg_type r on op.oprright = r.oid
	where amop.amopfamily = _r.opcfamily and amop.amopmethod = _r.opcmethod;

	select
		string_agg(
			E'\t\tFUNCTION ' || pc.amprocnum || ' (' ||
			coalesce(case when l.typnamespace = _pgns then '' else quote_ident(l.typnamespace::regnamespace::text) || '.' end || l.typname, 'NONE') || ', ' || 
			coalesce(case when r.typnamespace = _pgns then '' else quote_ident(r.typnamespace::regnamespace::text) || '.' end || r.typname, 'NONE') || ') ' || 
			case when p.pronamespace = _pgns then '' else quote_ident(p.pronamespace::regnamespace::text) || '.' end ||
			p.proname || '(' || oidvectortypes(p.proargtypes) || ')',
			E',\n' order by pc.amprocnum)
	into _functions
	from pg_catalog.pg_amproc pc
		join pg_catalog.pg_proc p on pc.amproc = p.oid
		left join pg_catalog.pg_type l on pc.amproclefttype = l.oid
		left join pg_catalog.pg_type r on pc.amprocrighttype = r.oid
	where pc.amprocfamily = _r.opcfamily;

	_content :=
		'CREATE OPERATOR CLASS ' || _obj_name || case when _r.opcdefault then ' DEFAULT' else '' end ||
			' FOR TYPE ' ||
			case when _r.typnamespace = _pgns then '' else quote_ident(_r.typnamespace::regnamespace::text) || '.' end ||
			_r.typname || E'\n\t' ||
			'USING ' || _r.amname || 
			case when _r.opfname = _r.opcname then '' else ' FAMILY ' || 
				case when _r.opfnamespace = _pgns then '' else quote_ident(_r.opfnamespace::regnamespace::text) || '.' end ||
				_r.opfname
			end || E' AS\n' ||
			coalesce(E'\t\tSTORAGE ' || 
				case when _r.storage_typnamespace = _pgns then '' else quote_ident(_r.storage_typnamespace::regnamespace::text) || '.' end ||
				_r.storage_typname || 
				E',\n', '') ||
			_operators || E',\n' || 
			_functions || ';';
	
	_comment := format(E'COMMENT ON OPERATOR CLASS %s USING %s IS %s;\n', 
						_obj_name, _r.amname,
						coalesce(E'\n' || quote_literal(obj_description(_obj_id, 'pg_opclass')), 'NULL'));
	
	raise notice E'%\n\n%', _content, _comment using hint = 'script';
end
$$
