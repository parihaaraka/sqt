with tmp as
(
    select 
	pg_get_userbyid(db.datdba) datdba, pg_encoding_to_char(db.encoding) "encoding", 
	db.datcollate, db.datctype, db.datallowconn, db.datconnlimit, db.datlastsysoid, 
	db.datfrozenxid, db.datminmxid, db.dattablespace, db.datacl,
	s.*, c.*
    from pg_database db
	join pg_stat_database s on db.oid = s.datid
	join pg_stat_database_conflicts c on db.oid = c.datid
    where db.oid = $database.id$
)
select * from jsonb_each_text(to_jsonb((select tmp from tmp)))
order by 1
