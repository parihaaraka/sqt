select 
	i.relname as index_name,
	regexp_replace(pg_get_indexdef(i.oid), '.*(USING\s*)+?', '') expr,
	pg_size_pretty(pg_relation_size(x.indexrelid)) index_size,
	stat.idx_scan, 
	stat.idx_tup_read,
	stat.idx_tup_fetch,
	x.indisunique
from pg_index x
	join pg_class i on x.indexrelid = i.oid
	join pg_stat_all_indexes stat on x.indexrelid = stat.indexrelid
where x.indrelid = $table.id$
