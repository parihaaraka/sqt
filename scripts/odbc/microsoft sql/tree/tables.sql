select
      'table' node_type,
      '<span class="light">' + s.name + '.</span>' + o.name ui_name,
      quotename(s.name) + '.' + quotename(o.name) "name",
      o.object_id id,
      o.name + s.name sort2,
      s.name + o.name sort1,
      cast(1 as bit) allow_multiselect
from sys.objects o
        inner join sys.schemas s on o.schema_id = s.schema_id
where o.type = 'U'
