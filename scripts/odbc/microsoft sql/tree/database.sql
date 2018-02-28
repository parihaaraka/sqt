select
      'tables' node_type,
      '<i><u>Tables</u></i>' ui_name,
      'fake_id' id,
      cast(null as sysname) icon,
      '0' sort1
union all
select
      'views',
      '<i><u>Views</u></i>',
      null,
      null,
      '1'
union all
select
      'procedures',
      '<i><u>Procedures</u></i>',
      null,
      null,
      '2'
union all
select
      'functions',
      '<i><u>Functions</u></i>',
      null,
      null,
      '3'
union all
select
      'schemas',
      '<i><u>Schemas</u></i>',
      null,
      null,
      '4'
union all
select
      'users',
      '<i><u>Users</u></i>',
      null,
      null,
      '5'
union all
select
      'roles',
      '<i><u>Roles</u></i>',
      null,
      null,
      '6'
