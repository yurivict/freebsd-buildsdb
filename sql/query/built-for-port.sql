
SELECT
	m.name AS Masterbuild,
	b.name AS Build,
	datetime(b.started, 'unixepoch', 'localtime') AS BuildStarted,
	datetime(b.ended, 'unixepoch', 'localtime') AS BuildEnded,
	s.origin AS Port,
	s.pkgname AS Package,
	s.elapsed AS Elapsed
FROM
	masterbuild m,
	build b
LEFT JOIN
	built s
ON
	b.id = s.build_id
WHERE
	m.id = b.masterbuild_id
	AND
	m.enabled = '1'
	AND
	s.origin = '%s'
ORDER BY
	BuildStarted,
	Masterbuild,
	Build
