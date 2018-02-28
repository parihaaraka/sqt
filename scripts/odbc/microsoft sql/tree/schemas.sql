select
      'schema' node_type,
      [name] ui_name,
      schema_id id,
      quotename([name]) "name",
      'layers-small.png' icon
from sys.schemas
where schema_id between 5 and 16383 or schema_id = 1
