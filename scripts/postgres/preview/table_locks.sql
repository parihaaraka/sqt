select
	l.locktype, l.mode, l."granted", l.pid, pg_blocking_pids(l.pid) as wait_for,
	now() - s.xact_start xact_duration,
/* if version 100000 */
	s.backend_type,
/* endif version */
	s.usename, s.application_name, s.client_addr, s.client_hostname,
	s.backend_start, s.xact_start, s.query_start, s.state_change,
/* if version 90600 */
	s.wait_event_type, s.wait_event,
/* endif version */
	s.state,
	s.backend_xid, s.backend_xmin, s.query
from pg_locks l
	left join pg_stat_activity s on l.pid = s.pid
where l.relation = $table.id$::regclass;
