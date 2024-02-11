
--
-- broken-grouped-by-port.sql: returns a list of currently broken ports
--                             grouped by the port's origin
--

SELECT
	f.origin AS Port,
	GROUP_CONCAT(DISTINCT m.name) AS Masterbuild,
	GROUP_CONCAT(DISTINCT f.phase) AS Phase,
	GROUP_CONCAT(DISTINCT f.errortype) AS ErrorType,
	datetime(max(bf.started), 'unixepoch', 'localtime') AS LastFailed,
	datetime(max(bs.started), 'unixepoch', 'localtime') AS LastSuccessful
FROM
	masterbuild m
LEFT OUTER JOIN
	build bf,
	failed f
ON
	m.id = bf.masterbuild_id
LEFT JOIN
	build bs,
	built s
ON
	m.id = bs.masterbuild_id
	AND
	m.enabled = '1'
WHERE
	bf.id = f.build_id
	AND
	bs.id = s.build_id
	AND
	f.origin = s.origin
	AND
	m.name NOT LIKE '%%%s%%'
GROUP BY
	f.origin
HAVING
	LastFailed IS NOT NULL
	AND
	(
		LastSuccessful = NULL
		OR
		LastFailed >= LastSuccessful
	)
ORDER BY
	LastFailed
;
