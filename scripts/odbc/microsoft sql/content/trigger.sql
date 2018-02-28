declare @result nvarchar(max);

select @result =
N'-- created:  ' + convert(nvarchar(19), o.create_date, 120) + '
-- modified: ' + convert(nvarchar(19), o.modify_date, 120) + '

' + replace(sm.definition, 'CREATE TRIGGER', 'ALTER TRIGGER')
from sys.sql_modules as sm
	left outer join sys.objects as o on sm.object_id = o.object_id
where sm.object_id = $trigger.id$;

select @result script;
