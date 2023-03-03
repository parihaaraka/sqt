select
	i.relname as index_name,
	regexp_replace(pg_get_indexdef(i.oid), '.*(USING\s*)+?', '') expr,
	x.indisunique,
	pg_size_pretty(pg_relation_size(x.indexrelid)) index_size,
	stat.idx_scan,
	stat.idx_tup_read,
	stat.idx_tup_fetch,
	iostat.idx_blks_read,
	iostat.idx_blks_hit
from pg_index x
	left join pg_class i on x.indexrelid = i.oid
	left join pg_stat_all_indexes stat on x.indexrelid = stat.indexrelid
	left join pg_statio_all_indexes iostat on x.indexrelid = iostat.indexrelid
where x.indrelid = $table.id$
