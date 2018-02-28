select
      'default constraint' node_type,
      k.[name] + '<br>&nbsp;&nbsp;<span class="light"><i>default</i> ' +
      k.definition +
      '</span>' ui_name,
      k.object_id id,
      '0' + k.[name] sort1
from sys.default_constraints k
where k.parent_object_id = $table.id$
union all
select
      'check constraint' node_type,
      k.[name] + '<span class="light">' +
               case when k.is_disabled = 1 then ' <i>(<u>disabled</u>)</i>' else '' end +
               '<br>&nbsp;&nbsp;<i>check</i> ' +
      (select definition from sys.check_constraints
       where object_id = k.object_id for xml path('')) +
      '</span>' ui_name,
      k.object_id id,
      '1' + k.[name] sort1
from sys.check_constraints k
where k.parent_object_id = $table.id$
