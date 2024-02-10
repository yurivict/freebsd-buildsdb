
--
-- broken-grouped-by-port.sql: returns a list of currently broken ports
--                             grouped by the port's origin
--

SELECT
	origin AS Port,
	GROUP_CONCAT(DISTINCT masterbuild_name) AS Masterbuild,
	GROUP_CONCAT(DISTINCT phase) AS Phase,
	GROUP_CONCAT(DISTINCT errortype) AS ErrorType,
	datetime(max(last_failed), 'unixepoch', 'localtime') AS LastFailed,
	datetime(max(last_succeeded), 'unixepoch', 'localtime') AS LastSucceeded
FROM
	broken
GROUP BY
	origin
ORDER BY
	origin
;
