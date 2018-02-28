select
	'foreign key' node_type,
	k.[name] + case when k.is_disabled = 1 then ' <i>(<u>disabled</u>)</i>' else '' end +
        '<br><span class="light">&nbsp;&nbsp;(' +
	substring(
		(select ', ' + nc.[name] AS [text()]
		from sys.foreign_key_columns c
			inner join sys.columns nc on c.parent_object_id = nc.object_id and c.parent_column_id = nc.column_id
		where c.constraint_object_id = k.object_id
		order by c.constraint_column_id
		for xml path('')), 3, 1000) +
        ') &rarr; ' + s.[name] + '.' + ro.[name] + ' (' +
	substring(
		(select ', ' + nc.[name] AS [text()]
		from sys.foreign_key_columns c
			inner join sys.columns nc on c.referenced_object_id = nc.object_id and c.referenced_column_id = nc.column_id
		where c.constraint_object_id = k.object_id
		order by nc.column_id
		for xml path('')), 3, 1000) + ')</span>' ui_name,
	k.object_id id,
   k.[name] sort1
from sys.foreign_keys k
     inner join sys.objects ro on k.referenced_object_id = ro.object_id
     inner join sys.schemas s on ro.schema_id = s.schema_id
where k.parent_object_id = $table.id$
