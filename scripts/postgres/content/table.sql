do
$$
declare
	_create_object text := '';
	_plain_list text;
	_update_list text;
	_values text := '';
	_tmp text;
	_relkind text := '$table.tag$';
	_obj_name text := '$schema.name$.$table.name$';
	_obj_id oid := $table.id$;
	_t record;
	_inherits text;
	_comment text := obj_description(_obj_id, 'pg_class');
	_col_comments text;
	_obj_type text;
	_partitioned_by text;
	_partition_of text;
	_partition_for text;
begin

	if '$children.ids$' = '-1' then

		-- TODO pg v10 support (partition by, etc.)
		
		-- script table structure
		if _relkind = 'v' then
			_create_object := 'CREATE OR REPLACE VIEW ' || _obj_name || E'\nAS\n' ||
				pg_get_viewdef(_obj_id::regclass, true) || E'\n\n';
			_obj_type := 'VIEW';
		elsif _relkind = 'm' then
			_create_object := 'CREATE MATERIALIZED VIEW ' || _obj_name || E'\nAS\n' ||
				pg_get_viewdef(_obj_id::regclass, true) || E'\n\n';
			_obj_type := 'MATERIALIZED VIEW';
		else
			select
				c.relpersistence, c.relkind, c.reloptions, c.reltablespace,
				ft.ftoptions, fs.srvname,
/* if version 110000 */
				false as relhasoids, c.relispartition, c.relpartbound
/* elif version 90600 */
				c.relhasoids, false as relispartition
/* endif version */
			from pg_class c
				left join pg_foreign_table ft on c.oid = ft.ftrelid
				left join pg_foreign_server fs on ft.ftserver = fs.oid
			into _t
			where c.oid = _obj_id;

/* if version 110000 */
			if _t.relispartition = true then
				select
					E'\nPARTITION OF ' || inhparent::regclass::text,
					' ' || pg_get_expr(_t.relpartbound, _obj_id)
				into _partition_of, _partition_for
				from pg_inherits
				where inhrelid = _obj_id;
			else
/* elif version 90600 */
			if true then
/* endif version */
				select E'\nINHERITS (' || string_agg(inhparent::regclass::text, ', ' order by inhseqno) || ')'
				into _inherits
				from pg_inherits
				where inhrelid = _obj_id;
			end if;
			
			_create_object := 
				E'CREATE ' ||
				case 
					when _t.relpersistence = 'u'::"char" then 'UNLOGGED '
					when _t.relkind = 'f'::"char" then 'FOREIGN '
					else '' 
				end || 
				'TABLE ' || _obj_name || coalesce(' ' || _partition_of, '') || E'\n(\n';
			_obj_type := case when _relkind = 'f' then 'FOREIGN ' else '' end || 'TABLE';

			if _relkind = 'p' then
				_partitioned_by := E'\nPARTITION BY ' || pg_get_partkeydef(_obj_id);
			end if;
			
			with tmp as -- union of constraints data
			(
				select ca.attnum, c.contype::text, c.conname, 
					case 
						when c.contype::text in ('p','u','x') then
							-- place index parameters after first closing parenthesis
							regexp_replace(pg_get_constraintdef(c.oid, true), '\)', ')' ||
								coalesce(' WITH (' || btrim(i.reloptions::text, '{}') || ')', '') ||
								case 
									when coalesce(i.reltablespace, 0) = 0 then '' 
									else E' USING INDEX TABLESPACE ' || (pg_identify_object('pg_tablespace'::regclass::oid, i.reltablespace, 0)).identity
								end
							)
						else
							pg_get_constraintdef(c.oid, true)
					end def
				from pg_catalog.pg_constraint c
					left join pg_class i on c.conindid = i.oid
					left join pg_catalog.pg_attribute ca on 
						ca.attrelid = c.conrelid and 
						ca.attnum = any(c.conkey) and
						c.conkey[2] is null
				where c.conrelid = _obj_id
				union all
				select a.attnum, 'd', null::text, 'DEFAULT ' || pg_get_expr(d.adbin, d.adrelid)
				from pg_catalog.pg_attribute a
					join pg_catalog.pg_attrdef d on a.attrelid = d.adrelid and a.attnum = d.adnum
					left join (
						pg_depend dep
							join pg_class s on s.oid = dep.objid and s.relkind = 'S'
						) on a.attnum = dep.refobjsubid and a.attrelid = dep.refobjid
				where a.attrelid = _obj_id and 
					-- don't print default in case of sequence ownership
					(
						s.oid is null or 
						a.atttypid not in (
							'smallint'::regtype::oid, 'int'::regtype::oid, 'bigint'::regtype::oid
						)
					)
			),
			c as -- single column constraints to be concatenated with fields
			(
				select 
					attnum, 
					array_agg(contype) ctypes, 
					string_agg(
						case
							when contype in ('p', 'u') then regexp_replace(def, '\s+\([^)]+\)(\s*)', '\1')
							when contype = 'f' then regexp_replace(def, '.*\m(REFERENCES.+)', '\1')
							else def
						end, ' '
						order by translate(contype, 'pcduf', '01234')
					) clist
				from tmp
				where attnum is not null
				group by attnum
			),
			to_show as  -- sets of per-column data of table definition to apply length-specific format + trailing constraints
			(
				select
					a.attnum, a.attislocal,
					col_description(_obj_id, a.attnum) description,
					case when a.attislocal then '' else '-- inherited:  ' end ||
						quote_ident(a.attname) || ' ' ||
							case 
								when s.oid is not null and a.atttypid = 'smallint'::regtype::oid then 'smallserial'
								when s.oid is not null and a.atttypid = 'int'::regtype::oid then 'serial'
								when s.oid is not null and a.atttypid = 'bigint'::regtype::oid then 'bigserial'
								else pg_catalog.format_type(a.atttypid, a.atttypmod)
							end ||
							case
								-- do we have a simpler way to detect default collation oid?
								when a.attcollation = 0 or pg_describe_object('pg_collation'::regclass::oid, a.attcollation, 0) ilike '%default' then ''
								else ' COLLATE ' || (pg_identify_object('pg_collation'::regclass::oid, a.attcollation, 0)).identity
							end ||
							case 
								when a.attnotnull and ('p'::"char" != all(c.ctypes) or c.ctypes is null) then ' NOT NULL' 
								else '' 
							end ||
							coalesce(' ' || c.clist, '') definition
				from pg_catalog.pg_attribute a
					left join c on a.attnum = c.attnum
					left join pg_catalog.pg_attrdef d on a.attrelid = d.adrelid and a.attnum = d.adnum
					left join (
						pg_depend dep
							join pg_class s on s.oid = dep.objid and s.relkind = 'S'
						) on a.attnum = dep.refobjsubid and a.attrelid = dep.refobjid
				where 
					a.attnum > 0 and not a.attisdropped and
					a.attrelid = _obj_id
				union all
				select
					1000000 + row_number() over(), true,
					null,
					def
				from tmp
				where attnum is null
			)
			select
				string_agg(
					case
						when length(definition) + length(description) > 110 then
							-- comment above column definition
							--coalesce(E'\t\t--\u2193 ' || replace(description, E'\n', E'\n\t\t-- ') || E'\n', '') ||
							-- column
							E'\t' ||	definition || 
								case 
									when attnum = (select max(attnum) from to_show where attislocal) then '' 
									else ',' 
								end ||
							coalesce(E'\n' || E'\t\t--\u2191 ' || replace(description, E'\n', E'\n\t\t-- '), '')
						else
							-- column
							E'\t' ||	definition ||
								case 
									when attnum = (select max(attnum) from to_show where attislocal) then '' 
									else ',' 
								end ||
							-- comment to the right
							coalesce(E'   -- ' || regexp_replace(description, '\s*\n\s*', ' ', 'g'), '')
					end,
					E'\n' order by attnum
				)
			into _tmp
			from to_show;

			if current_setting('default_with_oids')::bool is distinct from _t.relhasoids then
				_t.reloptions := coalesce(_t.reloptions, '{}'::text[]) ||
					case when _t.relhasoids then 'OIDS' else 'OIDS=false' end;
			end if;
			
			_create_object := _create_object || _tmp || E'\n)' ||
				coalesce(_inherits, _partition_for, '') ||
				coalesce(_partitioned_by, '') ||
				case
					when _t.relkind = 'f'::"char" then E'\nSERVER ' || _t.srvname || 
						coalesce(E'\nOPTIONS (' || 
							(
								select string_agg(regexp_replace(o, '=(.+)$', ' ''\1'''), ', ')
								from unnest(_t.ftoptions) o
							) || ')', '')
					else ''
				end ||
				case 
					when _t.reloptions is not null then
						E'\nWITH\n(\n' || (
							select string_agg(E'\t' || o, E',\n')
							from unnest(_t.reloptions) o
						) || E'\n)'
					else ''
				end || 
				case 
					when _t.reltablespace = 0 then '' 
					else E'\nTABLESPACE ' || (pg_identify_object('pg_tablespace'::regclass::oid, _t.reltablespace, 0)).identity
				end ||
				E';\n\n';
		end if;

		-- prepend description		
		select coalesce(
				case 
					when array_length(regexp_split_to_array(description, E'\n'), 1) > 1 then
						E'/*\n' || description || E'\n*/\n'
					else
						'-- ' || replace(description, E'\n', E'\n-- ') || E'\n\n'
				end, ''
			) || _create_object ||
				format(E'COMMENT ON %s %s IS %s;\n\n', _obj_type, _obj_name, coalesce(E'\n' || quote_literal(_comment), 'NULL'))
		into _create_object
		from (values (obj_description(_obj_id, 'pg_class'))) as v(description);

	end if;

	-- prepare helpful queries for db programmer
	with tmp as
	(
		select 
			row_number() over (order by a.attnum) rn,
			quote_ident(a.attname) attname,
			col_description(_obj_id, a.attnum) descr
		from pg_catalog.pg_attribute a
		where 
			a.attnum > 0 and not a.attisdropped and
			a.attrelid = _obj_id and
			(a.attnum in ($children.ids$) or '$children.ids$' = '-1')
	)
	select 
		string_agg(attname, ', ' order by rn),
		string_agg(E'\t' || attname || ' = $' || rn::text, E',\n' order by rn),
		string_agg('$' || rn::text, ', '),
		string_agg(
			format(E'COMMENT ON COLUMN %s.%s IS \n%s;', _obj_name, tmp.attname, quote_literal(descr)),
			E'\n'
		) filter (where tmp.descr is not null) || E'\n\n'
	into _plain_list, _update_list, _values, _col_comments
	from tmp;

   raise notice 
'%SELECT %
FROM %

INSERT INTO % (%)
VALUES (%);

UPDATE %
SET
%
WHERE
', 
	_create_object || coalesce(_col_comments, ''), _plain_list, _obj_name,
	_obj_name, _plain_list, _values, 
	_obj_name, _update_list 
   using hint = 'script';
   
end
$$

