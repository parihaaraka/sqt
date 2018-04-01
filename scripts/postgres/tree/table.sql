select
    'column' node_type,
    a.attname || ' <span class="light">' || 
        pg_catalog.format_type(a.atttypid, a.atttypmod) || 
        case when a.attnotnull then ' NOT NULL' else '' end ||
        '</span>' as ui_name,
    a.attnum id,
    a.attname "name",
    null icon,
    attnum sort1,
    '0' || a.attname sort2
from pg_catalog.pg_attribute a
where a.attnum > 0 and not a.attisdropped and a.attrelid = $table.id$
union all
select
    'triggers',
    '<i><u>Triggers</u></i>',
    null,
    null,
    'arrow-transition.png',
    x'7FFFFFF0'::int,
    '1'
where '$table.tag$' != 'f'
union all
select
    'indexes',
    '<i><u>Indexes</u></i>',
    null,
    null,
    'paper-plane.png',
    x'7FFFFFF1'::int,
    '2'
where '$table.tag$' not in ('v', 'f')
union all
select
    'constraints',
    '<i><u>Constraints</u></i>',
    null,
    null,
    'traffic-cone.png',
    x'7FFFFFF2'::int,
    '3'
where '$table.tag$' in ('r', 'p')
union all
select
    'rules',
    '<i><u>Rules</u></i>',
    null,
    null,
    'image-saturation-up.png',
    x'7FFFFFF3'::int,
    '4'
where '$table.tag$' != 'f'