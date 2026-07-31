/*
F3 source: resolve 1- or 2-part name of the object under cursor.
Caller substitutes raw (unquoted, case folded) identifiers:
	$f3.qualifier$ - the word to the left of the one under cursor ('NULL' if absent),
	$f3.name$      - the word under cursor.
Names longer than 2 parts are clamped by caller: being pressed on the 3rd part
(sch.tbl.col) f3 scripts sch.tbl, so the left neighbour is the only context needed.
Every row is a candidate to script: "type" is the /content script to execute,
the rest of columns are its $<type>.<name|id|tag>$, $schema.*$ and $table.*$ macroses.
Rows are ordered by the schema position within search_path, so the most probable
candidate comes first. System objects are ignored.
*/

/* if version 110000 */
with ns as
(
	select
		n.oid,
		n.nspname,
		array_position(current_schemas(true), n.nspname) ord
	from pg_catalog.pg_namespace n
	where n.nspname != 'information_schema' and
		n.nspname not like 'pg\_%' -- not a system schema
),
scope as
(
	-- schemas to search the name in: the qualifier only or every schema
	select ns.*
	from ns
	where '$f3.qualifier$' = 'NULL' or ns.nspname = '$f3.qualifier$'
),
host as
(
	-- relation/composite type to search the name among its columns/fields
	-- (the qualifier of <relation>.<column> and <composite type>.<field>)
	select
		c.oid,
		c.relname,
		c.relkind,
		ns.oid nsoid,
		ns.nspname,
		ns.ord
	from ns
		join pg_catalog.pg_class c on ns.oid = c.relnamespace
	where '$f3.qualifier$' != 'NULL' and
		c.relname = '$f3.qualifier$' and
		c.relkind in ('r'::"char", 'p'::"char", 'f'::"char", 'v'::"char", 'm'::"char", 'c'::"char")
),
obj as
(
	--schema
	select
		'schema' "type",
		quote_ident(ns.nspname) schema_name,
		ns.oid schema_id,
		quote_ident(ns.nspname) "name",
		ns.oid::text id,
		null::text tag,
		null::text table_name,
		null::oid table_id,
		ns.ord
	from ns
	where '$f3.qualifier$' = 'NULL' and ns.nspname = '$f3.name$'
	union all
	--table, view, materialized view, foreign table, sequence, index
	select
		case c.relkind
			when 'S'::"char" then 'sequence'
			when 'i'::"char" then 'index'
			when 'I'::"char" then 'index'
			else 'table'
		end,
		quote_ident(s.nspname), s.oid,
		quote_ident(c.relname), c.oid::text,
		case
			-- index.sql expects tablespace oid, table.sql expects relkind
			when c.relkind in ('i'::"char", 'I'::"char") then c.reltablespace::text
			else c.relkind::text
		end,
		null, null,
		s.ord
	from scope s
		join pg_catalog.pg_class c on s.oid = c.relnamespace
	where c.relname = '$f3.name$' and
		c.relkind in ('r'::"char", 'p'::"char", 'f'::"char", 'v'::"char", 'm'::"char",
			'S'::"char", 'i'::"char", 'I'::"char")
	union all
	--type, domain
	select
		case when t.typtype = 'd'::"char" then 'domain' else 'type' end,
		quote_ident(s.nspname), s.oid,
		quote_ident(t.typname), t.oid::text,
		t.typtype::text,
		null, null,
		s.ord
	from scope s
		join pg_catalog.pg_type t on s.oid = t.typnamespace
		left join pg_catalog.pg_class c on t.typrelid = c.oid
	where t.typname = '$f3.name$' and
		coalesce(c.relkind, 'c'::"char") = 'c'::"char" and -- not a table/view/etc rowtype
		not exists (select 1 from pg_catalog.pg_type a where a.typarray = t.oid) -- not an array
	union all
	--function, procedure, aggregate
	select
		case p.prokind
			when 'p'::"char" then 'procedure'
			when 'a'::"char" then 'agg_function'
			else 'function'
		end,
		quote_ident(s.nspname), s.oid,
		quote_ident(p.proname), p.oid::text,
		null, null, null,
		s.ord
	from scope s
		join pg_catalog.pg_proc p on s.oid = p.pronamespace
	where p.proname = '$f3.name$'
	union all
	--operator class
	select
		'operator_class',
		quote_ident(s.nspname), s.oid,
		quote_ident(op.opcname), op.oid::text,
		null, null, null,
		s.ord
	from scope s
		join pg_catalog.pg_opclass op on s.oid = op.opcnamespace
	where op.opcname = '$f3.name$'
	union all
	--trigger
	select
		'trigger',
		quote_ident(s.nspname), s.oid,
		quote_ident(t.tgname), t.oid::text,
		null,
		quote_ident(c.relname), c.oid,
		s.ord
	from scope s
		join pg_catalog.pg_class c on s.oid = c.relnamespace
		join pg_catalog.pg_trigger t on c.oid = t.tgrelid and not t.tgisinternal
	where t.tgname = '$f3.name$'
	union all
	--constraint
	select
		'constraint',
		quote_ident(s.nspname), s.oid,
		quote_ident(x.conname), x.oid::text,
		null,
		quote_ident(c.relname), c.oid,
		s.ord
	from scope s
		join pg_catalog.pg_constraint x on s.oid = x.connamespace
		left join pg_catalog.pg_class c on nullif(x.conrelid, 0) = c.oid
	where x.conname = '$f3.name$'
	union all
	--rule
	select
		'rule',
		quote_ident(s.nspname), s.oid,
		quote_ident(r.rulename), r.oid::text,
		null,
		quote_ident(c.relname), c.oid,
		s.ord
	from scope s
		join pg_catalog.pg_class c on s.oid = c.relnamespace
		join pg_catalog.pg_rewrite r on c.oid = r.ev_class and r.rulename != '_RETURN'
	where r.rulename = '$f3.name$'
	union all
	--<relation>.<column>
	select
		'column',
		quote_ident(h.nspname), h.nsoid,
		quote_ident(a.attname), a.attnum::text,
		null,
		quote_ident(h.relname), h.oid,
		h.ord
	from host h
		join pg_catalog.pg_attribute a on h.oid = a.attrelid
	where h.relkind != 'c'::"char" and
		a.attname = '$f3.name$' and
		a.attnum > 0 and not a.attisdropped
	union all
	--<composite type>.<field> (there is no per field script, so script the type)
	select
		'type',
		quote_ident(h.nspname), h.nsoid,
		quote_ident(t.typname), t.oid::text,
		t.typtype::text,
		null, null,
		h.ord
	from host h
		join pg_catalog.pg_type t on h.oid = t.typrelid
		join pg_catalog.pg_attribute a on h.oid = a.attrelid
	where h.relkind = 'c'::"char" and
		a.attname = '$f3.name$' and
		a.attnum > 0 and not a.attisdropped
	union all
	--extension (cluster level object, so a single part name only)
	select
		'extension',
		null, null,
		quote_ident(e.extname), e.oid::text,
		null, null, null,
		null
	from pg_catalog.pg_extension e
	where '$f3.qualifier$' = 'NULL' and e.extname = '$f3.name$'
	union all
	--role (cluster level object, so a single part name only)
	select
		'role',
		null, null,
		quote_ident(r.rolname), r.oid::text,
		null, null, null,
		null
	-- predefined pg_* roles are listed as well, just like the objects tree does
	from pg_catalog.pg_roles r
	where '$f3.qualifier$' = 'NULL' and r.rolname = '$f3.name$'
)
select
	"type",
	schema_name,
	schema_id,
	"name",
	id,
	tag,
	table_name,
	table_id
from obj
order by ord nulls last, "type", schema_name, "name"
/* endif version */
