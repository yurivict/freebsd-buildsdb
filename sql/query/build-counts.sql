
SELECT
	m.name AS Masterbuild,
	b.name AS Build,
	datetime(b.started, 'unixepoch', 'localtime') AS Started,
	datetime(b.ended, 'unixepoch', 'localtime') AS Ended,
	(SELECT count(*) FROM built   WHERE build_id = b.id) AS Queued,
	(SELECT count(*) FROM built   WHERE build_id = b.id) AS Built,
	(SELECT count(*) FROM failed  WHERE build_id = b.id) AS Failed,
	(SELECT count(*) FROM ignored WHERE build_id = b.id) AS Ignored,
	(SELECT count(*) FROM skipped WHERE build_id = b.id) AS Skipped
FROM
	masterbuild m,
	build b
WHERE
	m.id = b.masterbuild_id
ORDER BY
	m.name,
	b.started
