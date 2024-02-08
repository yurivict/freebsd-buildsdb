SELECT
	l.origin AS Port,
	l.pkgname AS Package,
	l.elapsed AS Elapsed
FROM
	masterbuild m,
	build b,
	built l
WHERE
	m.id = b.masterbuild_id
	AND
	b.id = l.build_id
	AND
	m.name = '%s'
	AND
	b.name = '%s'
