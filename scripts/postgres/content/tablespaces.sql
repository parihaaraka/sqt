/* V90200+ */
select
    s.spcname "name",
    pg_size_pretty(pg_tablespace_size(s.oid)) "size",
    s.oid,
    r.rolname "owner",
    pg_tablespace_location(s.oid) "location",
    s.spcacl acl,
    s.spcoptions "options",
    shobj_description(s.oid, 'pg_tablespace') description
from pg_tablespace s
    join pg_roles r on s.spcowner = r.oid
order by 1

