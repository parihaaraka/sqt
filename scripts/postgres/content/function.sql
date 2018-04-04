select coalesce(
		case 
		when array_length(regexp_split_to_array(description, E'\n'), 1) > 1 then
			E'/*\n' || description || E'\n*/\n'
		else
			'-- ' || replace(description, E'\n', E'\n-- ') || E'\n\n'
		end, ''
	) || 
		rtrim(pg_get_functiondef($function.id$), E'\n') || ';' 
	as script
from (values (obj_description($function.id$, 'pg_proc'))) as v(description)