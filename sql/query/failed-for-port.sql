
SELECT
	m.name AS Masterbuild,
	b.name AS Build,
	datetime(b.started, 'unixepoch', 'localtime') AS BuildStarted,
	datetime(b.ended, 'unixepoch', 'localtime') AS BuildEnded,
	f.origin AS Port,
	f.pkgname AS Package,
	f.phase AS Phase,
	f.errortype AS ErrorType,
	f.elapsed AS Elapsed
FROM
	masterbuild m,
	build b
LEFT JOIN
	failed f
ON
	b.id = f.build_id
WHERE
	m.id = b.masterbuild_id
	AND
	f.origin = '%s'
ORDER BY
	BuildStarted,
	Masterbuild,
	Build
