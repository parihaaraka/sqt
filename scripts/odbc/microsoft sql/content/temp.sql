-- Get all of the dependency information
SELECT OBJECT_NAME(sed.referencing_id) AS referencing_entity_name,
    o.type_desc AS referencing_desciption,
    COALESCE(COL_NAME(sed.referencing_id, sed.referencing_minor_id), '(n/a)') AS referencing_minor_id,
    sed.referencing_class_desc, sed.referenced_class_desc,
    sed.referenced_server_name, sed.referenced_database_name, sed.referenced_schema_name,
    sed.referenced_entity_name,
    COALESCE(COL_NAME(sed.referenced_id, sed.referenced_minor_id), '(n/a)') AS referenced_column_name,
    sed.is_caller_dependent, sed.is_ambiguous
-- from the two system tables sys.sql_expression_dependencies and sys.object
FROM sys.sql_expression_dependencies AS sed
INNER JOIN sys.objects AS o ON sed.referencing_id = o.object_id
-- on the function dbo.ufnGetProductDealerPrice
WHERE sed.referencing_id = OBJECT_ID('dbo.ufnGetProductDealerPrice');
