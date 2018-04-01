select
	'rule' node_type,
	r.rulename ui_name,
	quote_ident(r.rulename) "name",
	r.oid id
from pg_rewrite r
where r.ev_class = $table.id$ and r.rulename != '_RETURN';