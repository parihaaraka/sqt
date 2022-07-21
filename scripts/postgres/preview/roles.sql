select
	r.rolname,
	r.oid,
	r.rolsuper,
	r.rolinherit,
	r.rolcreaterole,
	r.rolcreatedb,
	r.rolcanlogin,
	r.rolconnlimit, r.rolvaliduntil,
	(select string_agg(b.rolname, ',')
	from pg_catalog.pg_auth_members m
	  join pg_catalog.pg_roles b on m.roleid = b.oid
	where m.member = r.oid) as memberof,
	r.rolreplication,
	r.rolbypassrls,
	(
		select array_agg(d.datname)
		from pg_database d
		where not d.datistemplate and has_database_privilege(r.oid, d.oid, 'CONNECT')
	) may_connect
from pg_catalog.pg_roles r
order by not rolcanlogin, 1;