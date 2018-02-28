select
      'function' node_type,
      '<span class="light">' + s.name + '.</span>' + o.name +
		' <i><span class="light">(' +
		case o.type
			when 'FN' then 'scalar'
			when 'TF' then 'table'
			when 'FS' then 'CLR scalar'
			when 'FT' then 'CLR table'
		end + ')</span></i>' ui_name,
	o.object_id id,
   o.name + s.name sort2,
   s.name + o.name sort1
from sys.objects o
	inner join sys.schemas s on o.schema_id = s.schema_id
where type in ('FN', 'FS', 'FT', 'TF')
