do
$$
declare
	_s record;
	_owned_by text;
begin
	select * into _s 
	from $schema.name$.$sequence.name$;

	select coalesce(quote_ident(t.relname) || '.' || quote_ident(a.attname), 'none')
	into _owned_by
	from pg_class s
		left join
		(
			pg_depend d
				join pg_class t on d.refobjid = t.oid
				join pg_attribute a on a.attnum = d.refobjsubid and t.oid = a.attrelid
		) on s.oid = d.objid
	where s.oid = $sequence.id$;
	
	raise notice 'create sequence $schema.name$.$sequence.name$
	increment %
	minvalue %
	maxvalue %
	start %
	cache %
	%cycle
	owned by %;', 
		_s.increment_by, 
		_s.min_value, 
		_s.max_value, 
		_s.last_value, 
		_s.cache_value, 
		case when _s.is_cycled then '' else 'no ' end,
		_owned_by
	using hint='script';
end
$$
