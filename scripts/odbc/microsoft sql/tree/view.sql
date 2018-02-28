select
      'column' node_type,
      c.[name] + ' <span class="light">' + t.[name] +
	case
	when t.[name] like '%char%' or t.[name] like '%binary%' then '(' +
		case when c.max_length = -1 then 'max'
		else cast(c.max_length / (case when t.[name] like 'n%' then 2 else 1 end) as varchar(10))
		end + ')'
	when t.[name] = 'float' and c.[precision] != 53 then '(' + cast(c.[precision] as varchar(3)) + ')'
	when t.[name] in ('numeric', 'decimal') then '(' + cast(c.[precision] as varchar(3)) + case when c.[scale] != 0 then ',' + cast(c.[scale] as varchar(3)) else '' end + ')'
	else '' +
	case when c.is_identity = 1 then ' identity(' + cast(ident_seed('$view.name$') as varchar(20)) + ',' + cast(ident_incr('$view.name$') as varchar(20)) + ')' else '' end +
	case when c.is_nullable = 0 then ' not null' else '' end + '</span>'
	end ui_name,
      cast(c.column_id as sysname) id,
      null icon,
      c.column_id sort1,
      '0' + c.[name] sort2
from sys.columns c
	left outer join sys.types t on c.user_type_id = t.user_type_id
where c.object_id = $view.id$
union all
select
      'triggers',
      '<i><u>Triggers</u></i>',
      null,
      null,
      1000,
      '1'
union all
select
      'indexes',
      '<i><u>Indexes</u></i>',
      null,
      null,
      1001,
      '2'
