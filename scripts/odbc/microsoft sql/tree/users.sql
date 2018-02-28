SELECT
      'user' node_type,
      dpr.[name] + '<span class="light">' +
      isnull(' &rarr; ' + (spr.[name] COLLATE SQL_Latin1_General_CP1_CI_AS), '') +
      isnull('<br>&nbsp;&nbsp;' + substring(
               (select ', ' + rp.[name]
	       from sys.database_role_members m
	            inner join sys.database_principals rp on m.role_principal_id = rp.principal_id
	       where m.member_principal_id = dpr.principal_id
	       order by rp.sid
               for xml path('')), 3, 1000), '') + '</span>'
      ui_name,
      dpr.principal_id id,
      dpr.[name] sort1
from sys.database_principals dpr
	left outer join sys.server_principals spr on dpr.[sid] = spr.[sid]
where dpr.[type] in ('S', 'U') and isnull(dpr.[sid], 0x0) != 0x0
