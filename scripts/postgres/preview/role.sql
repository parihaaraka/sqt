with recursive tmp1 as
(
	-- roles which contain current role
	select member, roleid, 0 lvl
	from pg_auth_members
	where member = $role.id$
	union all
	select m.member, m.roleid, tmp1.lvl + 1
	from tmp1
		join pg_auth_members m on tmp1.roleid = m.member
),
tmp2 as
(  -- members of current role
	select member, roleid, 0 lvl
	from pg_auth_members
	where roleid = $role.id$
	union all
	select m.member, m.roleid, tmp2.lvl + 1
	from tmp2
		join pg_auth_members m on tmp2.member = m.roleid
),
res1 as
(
	select distinct on (tmp1.roleid) tmp1.lvl,
		case when tmp1.lvl = 0 then 'direct' else 'indirect' end member_of,
		null::text contains,
		tmp1.roleid
	from tmp1
	order by tmp1.roleid, tmp1.lvl
),
res2 as
(
	select distinct on (tmp2.member) tmp2.lvl,
		null::text member_of,
		case when tmp2.lvl = 0 then 'direct' else 'indirect' end contains,
		tmp2.member
	from tmp2
	order by tmp2.member, tmp2.lvl
)
select
	res.member_of, res.contains,
	pg_get_userbyid(res.roleid) rolname,
	res.roleid,
	r.rolsuper, r.rolinherit, r.rolcreaterole, r.rolcreatedb, r.rolcanlogin, r.rolreplication,
	(
		select array_agg(d.datname)
		from pg_database d
		where not d.datistemplate and has_database_privilege(r.oid, d.oid, 'CONNECT')
	) may_connect
from (select * from res1 union all select * from res2) res
	join pg_roles r on res.roleid = r.oid
order by res.member_of is null, res.lvl, 2