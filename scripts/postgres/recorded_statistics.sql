/*sqt
{
    "charts": [
        {
            "name": "recorded statistics",
            "x": "ts",
            "y": { "f1": "#0c0" }
        }
    ]
}
*/

select 
	s.ts,
	1 + sin(extract(epoch from s.ts)/10) + 2 * random() f1
from generate_series(now(), now() + '20min'::interval, '1sec'::interval) as s(ts)