-- query to return comparable dbms version for further work with
-- accordingly marked part of dependent script
-- (see README.txt for details)

select db.compatibility_level
/*
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
    else ' unknown'
    end
*/
from sys.databases db
where db.database_id = db_id()
