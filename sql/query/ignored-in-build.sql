SELECT
	l.origin AS Port,
	l.pkgname AS Package,
	l.reason AS Reason
FROM
	masterbuild m,
	build b,
	ignored l
WHERE
	m.id = b.masterbuild_id
	AND
	m.enabled = '1'
	AND
	b.id = l.build_id
	AND
	m.name = '%s'
	AND
	b.name = '%s'
