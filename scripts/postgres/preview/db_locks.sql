select
	l.locktype, l.relation::regclass::text relation,
	l.page, l.tuple, l.virtualxid, l.transactionid,
	--l.classid, l.objid,
	l.mode, l."granted", l.pid, pg_blocking_pids(l.pid) as wait_for,
	now() - s.xact_start xact_duration,
	s.backend_type, s.usename, s.application_name, s.client_addr, s.client_hostname,
	s.backend_start, s.xact_start, s.query_start, s.state_change,
	s.wait_event_type, s.wait_event, s.state,
	s.backend_xid, s.backend_xmin, s.query
from pg_locks l
	left join pg_class c on l.relation = c.oid
	left join pg_stat_activity s on l.pid = s.pid
where (l.database = $database.id$ or l.database = 0 or l.database is null) and (c.oid is null or c.relnamespace::regnamespace::text not in ('pg_catalog'))
order by 1, 2