
--
-- broken.sql: returns a detailed list of currently broken ports by masterbuild
--

SELECT
	masterbuild_name AS Masterbuild,
	origin AS Port,
	phase AS Phase,
	errortype AS ErrorType,
	elapsed AS Elapsed,
	datetime(last_failed, 'unixepoch', 'localtime') AS LastFailed,
	datetime(last_succeeded, 'unixepoch', 'localtime') AS LastSucceeded
FROM
	broken
ORDER BY
	last_failed
;
