SELECT
	l.origin AS Port,
	l.pkgname AS Package,
	l.phase AS Phase,
	l.errortype AS ErrorType,
	l.elapsed AS Elapsed
FROM
	masterbuild m,
	build b,
	failed l
WHERE
	m.id = b.masterbuild_id
	AND
	m.enabled = 1
	AND
	b.id = l.build_id
	AND
	m.name = '%s'
	AND
	b.name = '%s'
