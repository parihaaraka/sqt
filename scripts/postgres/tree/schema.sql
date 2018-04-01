select
	'functions' node_type,
	null::text "name",
	'Functions' ui_name,
	null::text id,
	'function.png' icon,
	--null::text icon,
	'1' sort1
union all
select
	'sequences',
	null::text,
	'Sequences',
	null,
	'ui-paginator.png',
	'2'
union all
select
	'tables',
	null::text,
	'Tables',
	null,
	'tables.png',
	'3'
union all
select
	'types',
	null::text,
	'Types',
	null,
	'block.png',
	'4'
union all
select
	'views',
	null::text,
	'Views',
	null,
	'views.png',
	'5'
