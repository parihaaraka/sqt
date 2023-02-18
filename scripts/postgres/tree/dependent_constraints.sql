select
        'constraint' node_type,
        conname || coalesce(
                '&nbsp;&nbsp;<i><span class="light">' ||
                        case contype::text
                                when 'f' then ' &larr; ' || nullif(conrelid, 0)::regclass::text
                                else contype::text
                        end || '</span></i>',
                '') ui_name,
        oid id,
        conname "name"
from pg_constraint
where confrelid = $table.id$
