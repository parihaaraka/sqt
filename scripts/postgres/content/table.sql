do
$$
declare
	_create_table text := '';
	_plain_list text;
	_update_list text;
	_values text := '';
	_tmp text;
begin

	if '$children.ids$' = '-1' then
		-- script table structure here

		_create_table := E'create table $schema.name$.$table.name$\n(\n';

		select 
			string_agg(E'\t' || 
				quote_ident(a.attname) || ' ' || 
				pg_catalog.format_type(a.atttypid, a.atttypmod) ||
				case when a.attnotnull then ' not null' else '' end,
				E',\n' order by a.attnum) ||
				coalesce(
					(
						select format(E',\n\tconstraint %I primary key (%s)', 
							c.conname,
							string_agg(ca.attname, ', ' order by array_position(c.conkey, ca.attnum))
							) pkey
						from pg_catalog.pg_constraint c
							join pg_catalog.pg_attribute ca on ca.attrelid = c.conrelid and ca.attnum = any(c.conkey)
						where c.contype = 'p' and c.conrelid = $table.id$
						group by c.conname
					), ''
				)
		into _tmp
		from pg_catalog.pg_attribute a
		where 
			a.attnum > 0 and not a.attisdropped and
			a.attrelid = $table.id$;
		
		_create_table := _create_table || _tmp || E'\n);\n\n';
	
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
		a.attrelid = $table.id$ and
		(a.attnum in ($children.ids$) or '$children.ids$' = '-1');

   raise notice 
'%select %
from $schema.name$.$table.name$
where

insert into $schema.name$.$table.name$ (%)
values (%)

update $schema.name$.$table.name$
set
%
where
', 
	_create_table, _plain_list, _plain_list, _values, _update_list 
   using hint='script';
   
end
$$
