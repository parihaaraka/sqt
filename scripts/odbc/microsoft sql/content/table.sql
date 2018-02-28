declare 
    @insert_script nvarchar(max), 
    @update_script nvarchar(max), 
    @list nvarchar(max), 
    @values nvarchar(max);

set @insert_script = N'insert into $table.name$ (';
set @update_script = N'update $table.name$
set ';
set @list = N'';
set @values = N'';

select @insert_script = @insert_script +
       case when @list = N'' then N'' else N', ' end + quotename([name]),
       @update_script = @update_script +
       case when @list = N'' then N'' else N',
    ' end + quotename([name]) + N' = ?',
       @list = @list +
       case when @list = N'' then N'' else N', ' end + quotename([name]),
       @values = @values +
       case when @values = N'' then N'?' else N', ?' end
from sys.columns
where object_id = $table.id$ and (column_id in ($children.ids$) or '$children.ids$' = '-1')
order by column_id;

set @insert_script = @insert_script + N')
values (' + @values + N')';
set @update_script = @update_script + N'
where ';

select N'select
    ' + @list + N'
from $table.name$
where

' + @insert_script + N'

' + @update_script script;
