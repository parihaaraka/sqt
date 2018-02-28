select
      'parameter' node_type,
      isnull(nullif(p.[name], ''), 'returns') + ' <span class="light">' + t.[name] +
	case
	when t.[name] like '%char%' or t.[name] like '%binary%' then '(' +
		case when p.max_length = -1 then 'max'
		else cast(p.max_length / (case when t.[name] like 'n%' then 2 else 1 end) as varchar(10))
		end + ')'
	when t.[name] = 'float' and p.[precision] != 53 then '(' + cast(p.[precision] as varchar(3)) + ')'
	when t.[name] in ('numeric', 'decimal') then '(' + cast(p.[precision] as varchar(3)) + case when p.[scale] != 0 then ',' + cast(p.[scale] as varchar(3)) else '' end + ')'
	else ''
	end + '</span>' ui_name,
      cast(p.parameter_id as sysname) id,
      null icon,
      p.parameter_id sort1,
      p.[name] sort2
from sys.parameters p
	left outer join sys.types t on p.user_type_id = t.user_type_id
where p.object_id = $procedure.id$
