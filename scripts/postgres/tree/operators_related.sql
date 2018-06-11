select
	'operators' node_type,
	null::text "name",
	'Operators' ui_name,
	null::text id,
	'operators.png' icon,
	1 sort1
union all
select
	'operator_classes',
	null::text,
	'Operator classes',
	null,
	'operator-classes.png',
	2
union all
select
	'operator_families',
	null::text,
	'Operator families',
	null,
	'operator-families.png',
	3