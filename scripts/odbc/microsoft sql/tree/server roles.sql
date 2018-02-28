DECLARE @t table ([name] sysname, descr sysname);
insert into @t exec sp_helpsrvrole;
select
      'server role' node_type,
      [name] + isnull('<br>&nbsp;&nbsp;<span class="light"><i>' + descr + '</i></span>', '') ui_name,
      [name] id,
      case when [name] like '%admin' then '1' else '0' end + [name] sort1,
      [name] sort2
from @t;
