select 
	t.typname, t.oid, t.typowner::regrole, obj_description(t.oid, 'pg_type') "comment"
from pg_type t
	left join pg_class c on t.typrelid = c.oid
where t.typnamespace = $schema.id$ and 
	t.typtype not in ('d'::"char") and --exclude domains
	(c.relkind is null or c.relkind = 'c') and -- exclude composite types
	t.typname not like '\_%' -- not an array
order by 1