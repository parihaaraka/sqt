select
      'database' node_type,
      quote_ident(datname) "name",
      datname ui_name,
      oid::text id,
      'database-medium.png' icon,
      '00' || datname sort1
from pg_database 
where not datistemplate
union all
select
      'roles',
      null,
      '<i>Roles</i>',
      null,
      'fingerprint.png',
      '01'
union all
select
      'cluster_state',
      null,
      '<i>Cluster state</i>',
      null,
      'information-white.png',
      '02'
where $dbms.version$ > 90600
union all
select
      'pg_settings',
      null,
      '<i>Settings</i>',
      null,
      'equalizer.png',
      '04'
union all
select
      'tablespaces',
      null,
      '<i>Tablespaces</i>',
      null,
      null,
      '05'
union all
select
      'sessions',
      null,
      '<i>Current sessions</i>',
      null,
      null,
      '06'
union all
select
      'test_html',
      null,
      '<i>Test HTML result</i>',
      null,
      null,
      '10'
