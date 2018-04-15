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
begin

	if '$children.ids$' = '-1' then
		-- script table structure here

		if _relkind = 'v' then
			_create_object := 'CREATE OR REPLACE VIEW ' || _obj_name || E'\nAS\n' ||
				pg_get_viewdef(_obj_id::regclass, true) || E'\n\n';
		elsif _relkind = 'm' then
			_create_object := 'CREATE MATERIALIZED VIEW ' || _obj_name || E'\nAS\n' ||
				pg_get_viewdef(_obj_id::regclass, true) || E'\n\n';
		else
			select * from pg_class into _t
			where oid = _obj_id;
			
			_create_object := 
				E'CREATE ' ||
				case when _t.relpersistence = 'u'::"char" then 'UNLOGGED ' else '' end || 
				'TABLE ' || _obj_name || E'\n(\n';

			select E'\nINHERITS (' || string_agg(inhparent::regclass::text, ', ' order by inhseqno) || ')'
			into _inherits
			from pg_inherits
			where inhrelid = _obj_id;
			
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
				select a.attnum, 'd', null::text, 'DEFAULT ' || d.adsrc
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
								when a.attnotnull and 'p'::"char" != all(c.ctypes) then ' NOT NULL' 
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
							coalesce(E'\t\t--\u2193 ' || replace(description, E'\n', E'\n\t\t-- ') || E'\n', '') ||
							-- column
							E'\t' ||	definition || 
								case 
									when attnum = (select max(attnum) from to_show where attislocal) then '' 
									else ',' 
								end
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
				coalesce(_inherits, '') ||
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
			) || _create_object
		into _create_object
		from (values (obj_description(_obj_id, 'pg_class'))) as v(description);

	end if;

	-- prepare helpful queries for db programmer
	select 
		string_agg(quote_ident(a.attname), ', ' order by a.attnum),
		string_agg(E'\t' || quote_ident(a.attname) || ' = ?', E',\n' order by a.attnum),
		string_agg('?', ', ')
	into _plain_list, _update_list, _values
	from pg_catalog.pg_attribute a
	where 
		a.attnum > 0 and not a.attisdropped and
		a.attrelid = _obj_id and
		(a.attnum in ($children.ids$) or '$children.ids$' = '-1');

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
	_create_object, _plain_list, _obj_name,
	_obj_name, _plain_list, _values, 
	_obj_name, _update_list 
   using hint = 'script';
   
end
$$
