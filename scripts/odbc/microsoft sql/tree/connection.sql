select
      case when state != 0 then 'dummy' else 'database' end as node_type,
      [name] "name",
      [name] + isnull(' <i>(' +
			case when state != 0 then state_desc COLLATE SQL_Latin1_General_CP1_CI_AS
			     when is_read_only = 1 then 'read only'
			     end
			+ ')</i>', '') ui_name,
      database_id id,
      'database-medium.png' icon,
      '0' + [name] sort1
from master.sys.databases --where database_id > 4
union all
select
      'logins',
      null,
      '<i><u>Logins</u></i>',
      null,
      null,
      '1'
union all
select
      'server roles',
      null,
      '<i><u>Server roles</u></i>',
      null,
      null,
      '2'
union all
select
      'sp_who',
      null,
      '<i><u>Current sessions</u></i>',
      null,
      null,
      '3'
union all
select
      'test_html',
      null,
      '<i><u>Test HTML result</u></i>',
      null,
      null,
      '4'

