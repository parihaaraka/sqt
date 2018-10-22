select
	'functions' node_type,
	null::text "name",
	'Functions' ui_name,
	null::text id,
	'function.png' icon,
	--null::text icon,
	1 sort1
union all
select
	'procedures',
	null,
	'Procedures',
	null,
	'procedure.png',
	2
where $dbms.version$ >= 110000
union all
select
	'sequences',
	null,
	'Sequences',
	null,
	'ui-paginator.png',
	3
union all
select
	'tables',
	null,
	'Tables',
	null,
	'tables.png',
	4
union all
select
	'types',
	null,
	'Types',
	null,
	'block.png',
	5
union all
select
	'domains',
	null,
	'Domains',
	null,
	'hard-hat.png',
	6
union all
select
	'views',
	null,
	'Views',
	null,
	'views.png',
	7
union all
select
	'operators_related',
	null,
	'Ops',
	null,
	'operators-related.png',
	8
