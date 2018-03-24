select * from jsonb_each_text(to_jsonb((select r from pg_control_recovery() r)))
order by 1
