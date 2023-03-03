select
	attname, inherited, null_frac, avg_width, n_distinct,
	most_common_vals, most_common_freqs, histogram_bounds,
	correlation, most_common_elems, most_common_elem_freqs, elem_count_histogram
from pg_stats where tablename = '$table.name$' and schemaname = '$schema.name$'
