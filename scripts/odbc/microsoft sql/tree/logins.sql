SELECT
      'login' node_type,
      p.[name] + ' <span class="light"><i>(' +
           lower(p.type_desc COLLATE SQL_Latin1_General_CP1_CI_AS) +
           case when p.is_disabled = 1 then ', <u>disabled</u>' else '' end + ')</i>' +
           isnull('<br>&nbsp;&nbsp;' + substring(
               (select ', ' + rp.[name]
               from sys.server_role_members m
                  inner join sys.server_principals rp on m.role_principal_id = rp.principal_id
               where m.member_principal_id = p.principal_id
               order by rp.sid
               for xml path('')), 3, 1000), '') + '</span>'
      ui_name,
      p.principal_id id,
      p.[name] sort1
from sys.server_principals p
where p.[type] in ('S', 'U') and p.[name] not like 'NT %'
