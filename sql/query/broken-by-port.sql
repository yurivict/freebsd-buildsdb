-- returns the list of currently broken packages grouped by port

SELECT
	origin AS Port,
	GROUP_CONCAT(DISTINCT masterbuild_name) AS Masterbuild,
	GROUP_CONCAT(DISTINCT phase) AS Phase,
	GROUP_CONCAT(DISTINCT errortype) AS ErrorType,
	datetime(max(last_failed), 'unixepoch', 'localtime') AS LastFailed,
	datetime(max(last_succeeded), 'unixepoch', 'localtime') AS LastSucceeded,
	datetime(max(last_skipped), 'unixepoch', 'localtime') AS LastSkipped
FROM
	broken
GROUP BY
	origin
ORDER BY
	origin
;
