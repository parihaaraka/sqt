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

			with tmp as
			(
				select ca.attname, c.contype::text, c.conname, pg_get_constraintdef(c.oid, true) def
				from pg_catalog.pg_constraint c
					left join pg_catalog.pg_attribute ca on 
						ca.attrelid = c.conrelid and 
						ca.attnum = any(c.conkey) and
						c.conkey[2] is null
				where c.conrelid = _obj_id
				union all
				select a.attname, 'd', null::text, 'DEFAULT ' || d.adsrc
				from pg_catalog.pg_attribute a
					join pg_catalog.pg_attrdef d on a.attrelid = d.adrelid and a.attnum = d.adnum
					left join (
						pg_depend dep
							join pg_class s on s.oid = dep.objid and s.relkind = 'S'
						) on a.attnum = dep.refobjsubid and a.attrelid = dep.refobjid
				where a.attrelid = _obj_id and 
					-- don't print default in case of sequence ownership
					(s.oid is null or a.atttypid not in ('int'::regtype::oid, 'bigint'::regtype::oid))
			),
			c as
			(
				select attname, array_agg(contype) ctypes, string_agg(
					case contype
						when 'p' then 'PRIMARY KEY'
						when 'f' then regexp_replace(def, '.*\m(REFERENCES.+)', '\1')
						else def
					end, ' '
					order by translate(contype, 'pcdf', '0123')
				) clist
				from tmp
				where attname is not null
				group by attname
			)
			select 
				string_agg(E'\t' || 
					quote_ident(a.attname) || ' ' ||
					case 
						when s.oid is not null and a.atttypid = 'int'::regtype::oid then 'serial'
						when s.oid is not null and a.atttypid = 'bigint'::regtype::oid then 'bigserial'
						else pg_catalog.format_type(a.atttypid, a.atttypmod)
					end ||
					case when a.attnotnull and 'p'::"char" != all(c.ctypes) then ' NOT NULL' else '' end ||
					coalesce(' ' || c.clist, ''),
					E',\n' order by a.attnum
				) ||
				coalesce(E',\n' ||
					(
						select string_agg(E'\t' || def, E',\n')
						from tmp
						where attname is null
					), ''
				)
			into _tmp
			from pg_catalog.pg_attribute a
				left join c on a.attname = c.attname
				left join pg_catalog.pg_attrdef d on a.attrelid = d.adrelid and a.attnum = d.adnum
				left join (
					pg_depend dep
						join pg_class s on s.oid = dep.objid and s.relkind = 'S'
					) on a.attnum = dep.refobjsubid and a.attrelid = dep.refobjid
			where 
				a.attnum > 0 and not a.attisdropped and
				a.attrelid = _obj_id;
							
			if current_setting('default_with_oids', true)::bool is distinct from _t.relhasoids then
				_t.reloptions := coalesce(_t.reloptions, '{}'::text[]) ||
					case when _t.relhasoids then 'OIDS' else 'OIDS=false' end;
			end if;
			
			_create_object := _create_object || _tmp || E'\n)' ||
				case when _t.reloptions is not null then
					E'\nWITH\n(\n' || (
						select string_agg(E'\t' || o, E',\n')
						from unnest(_t.reloptions) o
					) || E'\n)'
					else '' end
				|| E';\n\n';
		end if;	
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
