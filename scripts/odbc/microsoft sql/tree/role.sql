SELECT
      'role member' node_type,
      rp.[name] + ' <span class="light"><i>(' +
                lower(rp.type_desc COLLATE SQL_Latin1_General_CP1_CI_AS)
                + ')</i></span>' ui_name,
      rp.principal_id id,
      case when rp.[type] in ('S', 'U') then '0' else '1' end + rp.[name] sort1,
      rp.[name] sort2
from sys.database_role_members m
     inner join sys.database_principals rp on m.member_principal_id = rp.principal_id
where m.role_principal_id = $role.id$
