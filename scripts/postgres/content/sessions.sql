/* V90000+ */
select
    s.datname, s.pid, s.usename, s.application_name, s.client_addr, s.client_hostname, 
    s.backend_start, s.xact_start, s.query_start, s.state_change,
    s.wait_event_type, s.wait_event, s.state,
    s.backend_xid, s.backend_xmin, s.query
from pg_stat_activity s
order by 1, 3, 2

/* V100000+ */
select 
    s.datname, s.pid, s.backend_type, s.usename, s.application_name, s.client_addr, s.client_hostname, 
    s.backend_start, s.xact_start, s.query_start, s.state_change,
    s.wait_event_type, s.wait_event, s.state,
    s.backend_xid, s.backend_xmin, s.query
from pg_stat_activity s
order by 1, 4, 2