/* V100000+ */
do
$$
declare
	_s record;
	_owned_by text;
begin

	select * into _s
	from pg_catalog.pg_sequence s
		cross join $schema.name$.$sequence.name$ ss
	where s.seqrelid = $sequence.id$;

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
	
	raise notice 'CREATE SEQUENCE $schema.name$.$sequence.name$
	INCREMENT %
	MINVALUE %
	MAXVALUE %
	START %
	CACHE %
	%CYCLE
	OWNED BY %;', 
		_s.seqincrement, 
		_s.seqmin, 
		_s.seqmax, 
		_s.last_value, 
		_s.seqcache, 
		case when _s.seqcycle then '' else 'NO ' end,
		_owned_by
	using hint='script';
end
$$

/* V90000+ */
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
	
	raise notice 'CREATE SEQUENCE $schema.name$.$sequence.name$
	INCREMENT %
	MINVALUE %
	MAXVALUE %
	START %
	CACHE %
	%CYCLE
	OWNED BY %;', 
		_s.increment_by, 
		_s.min_value, 
		_s.max_value, 
		_s.last_value, 
		_s.cache_value, 
		case when _s.is_cycled then '' else 'NO ' end,
		_owned_by
	using hint='script';
end
$$
