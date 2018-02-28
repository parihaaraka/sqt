select
      'database' node_type,
      quote_ident(datname) "name",
      datname ui_name,
      oid::text id,
      'database-medium.png' icon,
      '0' || datname sort1
from pg_database 
where not datistemplate
union all
select
      'roles',
      null,
      '<i><u>Roles</u></i>',
      'fake_id',
      'fingerprint.png',
      '2'
union all
select
      'tablespaces',
      null,
      '<i><u>Tablespaces</u></i>',
      null,
      null,
      '3'
union all
select
      'sessions',
      null,
      '<i><u>Current sessions</u></i>',
      null,
      null,
      '4'
union all
select
      'test_html',
      null,
      '<i><u>Test HTML result</u></i>',
      null,
      null,
      '5'
order by 1, 3
