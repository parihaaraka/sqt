select
      'index' node_type,
      i.[name] +
      case when i.is_primary_key = 1 then ' <i>(pk)</i>'
           when i.is_unique = 1 then ' <i>(unique)</i>' else '' end +
        '<br><span class="light">&nbsp;&nbsp;' + substring(
               (select ', ' + nc.[name] AS [text()]
               from sys.index_columns c
                    inner join sys.columns nc on c.object_id = nc.object_id and c.column_id = nc.column_id
               where c.object_id = i.object_id and c.index_id = i.index_id
               order by c.key_ordinal
               for xml path('')), 3, 1000) +
        '</span>' ui_name,
      i.index_id id,
      i.index_id sort1,
      i.[name] sort2
from sys.indexes i
where i.object_id = isnull($table.id$, $view.id$) and i.type != 0