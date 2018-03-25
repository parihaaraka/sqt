do
$$
declare
	_opt text;
	_opts text[] := '{}'::text[];
	_def jsonb;
	_r record;
	_params text;
begin
	_def := to_jsonb((select pg_aggregate from pg_aggregate where aggfnoid = $agg_function.id$));
	for _r in select * from jsonb_each_text(_def) loop
		_opt :=
			case
			when _r.key = 'aggtransfn' then 'sfunc = ' || _r.value
			when _r.key = 'aggtranstype' then 'stype = ' || _r.value::regtype
			when _r.key = 'aggfinalfn' and _r.value != '-' then 'finalfunc = ' || _r.value
			when _r.key = 'aggsortop' and _r.value != '0' then 'sortop = ' || (select quote_ident(oprname) from pg_operator where oid = _r.value::oid)
			when _r.key = 'agginitval' and _r.value is not null then 'initcond = ' || quote_literal(_r.value)
			when _r.key = 'aggfinalextra' and _r.value::boolean then 'finalfunc_extra'
			when _r.key = 'aggcombinefn' and _r.value != '-' then 'combinefunc = ' || _r.value
			when _r.key = 'aggserialfn' and _r.value != '-' then 'serialfunc = ' || _r.value
			when _r.key = 'aggdeserialfn' and _r.value != '-' then 'deserialfunc = ' || _r.value
			else null
			end;
			
		if _opt is not null then
			_opts := _opts || _opt;
		end if;
	end loop;
	
	select string_agg(E'\t' || p, E',\n') into _opt
	from unnest(_opts) o(p);
	
	select oidvectortypes(proargtypes) into _params
	from pg_proc
	where "oid" = $agg_function.id$;

	raise notice E'create aggregate $schema.name$.$agg_function.name$(%)\n(\n%\n);', _params, _opt using hint='script';
end
$$
