select
      'trigger' node_type,
      '<span class="light">' + s.name + '.</span>' + o.name +
      '<span class="light">' + isnull('(' +
             nullif(case when t.type = 'TA' then 'CLR' else '' end +
	                case when t.is_disabled = 1 then
			     case when t.type = 'TA' then ', ' else '' end +
			     '<u>disabled</u>' else ''
			end, '') +
	     ')', '') + '<br>&nbsp;&nbsp;' +
      case when t.is_instead_of_trigger = 1 then 'INSTEAD OF ' else '' end +
      substring(
               (select ', ' + type_desc
	       from sys.trigger_events
               where object_id = o.object_id
               order by [type]
               for xml path('')), 3, 1000) +
      '</span>' ui_name,
      o.object_id id,
      o.name sort1
from sys.objects o
      inner join sys.triggers t on o.object_id = t.object_id
      inner join sys.schemas s on o.schema_id = s.schema_id
where o.type in ('TR') and o.parent_object_id = isnull($table.id$, $view.id$)
