select
      'procedure' node_type,
      '<span class="light">' + s.name + '.</span>' + o.name ui_name,
      o.object_id id,
      o.name + s.name sort2,
      s.name + o.name sort1
from sys.objects o
        inner join sys.schemas s on o.schema_id = s.schema_id
where o.type in ('P')
