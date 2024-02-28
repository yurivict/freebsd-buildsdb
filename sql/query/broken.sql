-- returns the detailed list of currently broken ports by masterbuild

SELECT
	masterbuild_name AS Masterbuild,
	origin AS Port,
	phase AS Phase,
	errortype AS ErrorType,
	elapsed AS Elapsed,
	datetime(last_failed, 'unixepoch', 'localtime') AS LastFailed,
	datetime(last_succeeded, 'unixepoch', 'localtime') AS LastSucceeded,
	datetime(last_skipped, 'unixepoch', 'localtime') AS LastSkipped
FROM
	broken
ORDER BY
	last_failed
;
