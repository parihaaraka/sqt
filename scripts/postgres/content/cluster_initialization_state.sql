select * from jsonb_each_text(to_jsonb((select r from pg_control_init() r)))
order by 1
