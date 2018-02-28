select 'CREATE OR REPLACE VIEW $view.name$
AS
' || pg_get_viewdef('$view.id$'::regclass, true) as script