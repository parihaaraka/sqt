select
      'functions' node_type,
      null::text "name",
      'Functions' ui_name,
      null::text id,
      null::text icon,
      '1' sort1
union all
select
      'sequences',
      null::text,
      'Sequences',
      null,
      null,
      '2'
union all
select
      'tables',
      null::text,
      'Tables',
      'dummy',
      'tables.png',
      '3'
union all
select
      'views',
      null::text,
      'Views',
      null,
      'views.png',
      '4'
order by sort1