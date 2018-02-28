-- source: http://2smart4school.com/understand-use-and-modify-the-stored-procedure-sp_spaceused/

with i as
(
	-- XML Indexes or Fulltext Indexes which use internal tables tied user table
	select
		it.parent_id [object_id],
		sum(reserved_page_count) reservedpages,
		sum(used_page_count) usedpages
	from sys.internal_tables it
		inner join sys.dm_db_partition_stats p on it.object_id = p.object_id
	where it.internal_type in (202,204,211,212,213,214,215,216)
	group by it.parent_id
),
pt as
(
	select
		s.name [schema],
		o.name,
		o.object_id,
		sum(p.reserved_page_count) reservedpages,
		sum(p.used_page_count) usedpages,
		sum(
			case
				when (p.index_id < 2) then (p.in_row_data_page_count + p.lob_used_page_count + p.row_overflow_used_page_count)
				else p.lob_used_page_count + p.row_overflow_used_page_count
				end
			) [pages],
		sum(case	when (p.index_id < 2) then row_count else 0 end) [rowcount]
	from sys.objects o
		inner join sys.schemas s on o.schema_id = s.schema_id
		inner join sys.dm_db_partition_stats p on o.object_id = p.object_id
	where o.type = 'U'
	group by s.name, o.name, o.object_id
),
res as
(
	select
		pt.[schema],
		pt.name,
		pt.[rowcount] [rows],
		pt.reservedpages + isnull(i.reservedpages, 0) reservedpages,
		pt.usedpages + isnull(i.usedpages, 0) usedpages,
		pt.[pages]
	from pt
		left outer join i on pt.object_id = i.object_id
)
select
	[schema],
	name,
	[rows],
	convert(decimal(15,0), [reservedpages] * 8) reserved,
	convert(decimal(15,0), [pages] * 8) [data],
	convert(decimal(15,0), (case when usedpages > pages then (usedpages - [pages]) else 0 end) * 8) index_size,
	convert(decimal(15,0), (case when reservedpages > usedpages then (reservedpages - usedpages) else 0 end) * 8) unused,
	'kb' unit
from res
order by reserved desc
