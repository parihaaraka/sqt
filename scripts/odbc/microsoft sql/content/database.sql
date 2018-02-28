declare @result nvarchar(max);

select @result =
'<b>&nbsp;<u>' + db.name + '</u></b><br>' +
'<table>' + 
'<tr><td><i>state:</i></td><td>' + (db.state_desc COLLATE SQL_Latin1_General_CP1_CI_AS) + '</td></tr>' +
'<tr><td><i>collation:</i></td><td>' + db.collation_name + '</td></tr>' +
'<tr><td><i>files:</i></td><td>' + isnull('<table>' +
	(select 
		(select type_desc + ': ' as td for xml path(''), type),
		(select name + ' (' + lower(state_desc) + ') ' as td for xml path(''), type),
		(select cast(size*8/1024 as nvarchar(50)) + ' MB ' as td for xml path(''), type),
		(select physical_name as td for xml path(''), type)
	from sys.master_files 
	where database_id = $database.id$
	order by type_desc desc, size desc
	for xml path('tr')) +
'</table>', '') +
'</td></tr>' +
'<tr><td><i>user access:</i></td><td>' + db.user_access_desc + '</td></tr>' +
'<tr><td><i>recovery model:</i></td><td>' + db.recovery_model_desc + '</td></tr>' +
'<tr><td><i>compatibility level:</i></td><td>' + cast(db.compatibility_level as nvarchar(5)) +
	case db.compatibility_level
	when 60 then ' (SQL Server 6.0)'
	when 65 then ' (SQL Server 6.5)'
	when 70 then ' (SQL Server 7.0)'
	when 80 then ' (SQL Server 2000)'
	when 90 then ' (SQL Server 2005)'
	when 100 then ' (SQL Server 2008)'
	when 110 then ' (SQL Server 2012)'
	when 120 then ' (SQL Server 2014)'
	when 130 then ' (SQL Server 2016)'
	when 140 then ' (SQL Server 2017)'
	else ''
	end + '</td></tr>' +
'<tr><td><i>create date:</i></td><td>' + convert(varchar(20), db.create_date, 120) + '</td></tr>' +
'<tr><td><i>backups:</i></td><td><table>' + 
	isnull((select
		(select case type when 'D' then 'Database'
				when 'I' then 'Differential database'
				when 'L' then 'Log'
				when 'F' then 'File or filegroup'
				when 'G' then 'Differential file'
				when 'P' then 'Partial'
				when 'Q' then 'Differential partial' else '' end as td for xml path(''), type),
		(select ' ' +
			ltrim(isnull(str(abs(datediff(day, getdate(),backup_finish_date))) + ' days ago', 'NEVER')) + ' (' +
		convert(varchar(20), backup_start_date, 120) + ', done in ' +
		+ cast(datediff(second, bk.backup_start_date, bk.backup_finish_date) as varchar(4)) + 
		' seconds)'  as td for xml path(''), type)
	from msdb..backupset bk 
	where bk.database_name = '$database.name$' and backup_set_id >= isnull((
				select top(1) backup_set_id 
				from msdb..backupset 
				where database_name = '$database.name$' and type = 'D' order by backup_set_id desc), 0)
	order by backup_set_id desc
	for xml path('tr')), '') +
'</table></td></tr>' +
'</table>'
from sys.databases db
where db.database_id = $database.id$;

select @result as html;
