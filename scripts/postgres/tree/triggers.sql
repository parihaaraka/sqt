SELECT
	'trigger' node_type,
	t.tgname ||
	'<span class="light">&nbsp;&nbsp;' ||
	case 
		when (t.tgtype & (1 << 1))::boolean then 'before' 
		when (t.tgtype & (1 << 6))::boolean then 'instead of' 
		else 'after' 
	end || ' ' ||
	(
		select string_agg(act, '')
		from unnest(
			ARRAY[
				case when (t.tgtype & (1 << 2))::boolean then 'I' else null end,
				case when (t.tgtype & (1 << 3))::boolean then 'D' else null end,
				case when (t.tgtype & (1 << 4))::boolean then 'U' else null end,
				case when (t.tgtype & (1 << 5))::boolean then 'T' else null end
			]) a(act)
		where act is not null
	) || ' ' ||
	case when (t.tgtype & (1 << 0))::boolean then 'row' else 'statement' end ||
	'</span>' ui_name,
	t.oid id,
	t.tgname "name",
	t.tgname sort1
FROM pg_trigger t
WHERE t.tgrelid = coalesce($table.id$, $view.id$) and not t.tgisinternal
