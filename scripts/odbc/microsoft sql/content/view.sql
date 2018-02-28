declare 
    @list nvarchar(max), 
    @create_script nvarchar(max);

set @list = N'';

select @list = @list +
       case when @list = '' then '' else ', ' end + '[' + [name] + ']'
from sys.columns
where object_id = $view.id$ and (column_id in ($children.ids$) or '$children.ids$' = '-1')
order by column_id;

if '$children.ids$' = '-1'
   select @create_script =
N'-- created:  ' + convert(nvarchar(19), o.create_date, 120) + '
-- modified: ' + convert(nvarchar(19), o.modify_date, 120) + '

' + sm.definition
  from sys.sql_modules as sm
	left outer join sys.objects as o on sm.object_id = o.object_id
  where sm.object_id = $view.id$;

select isnull(@create_script + N'
--------------------------------

', N'') + N'select
    ' + @list + N'
from $view.name$
where' script;
