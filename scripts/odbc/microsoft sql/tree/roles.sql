SELECT
      'role' node_type,
      [name] ui_name,
      principal_id id,
      case when [name] like 'db[_]%' then '1' else '0' end + [name] sort1,
      [name] sort2
from sys.database_principals
where [type] = 'R' and [name] != 'public'
