
--
-- broken-by-port.sql: returns a list of currently broken ports grouped by port
--

ATTACH 'ports.sqlite' AS ports;

SELECT
	origin AS Port,
	(SELECT MAINTAINER FROM ports.Port WHERE PKGORIGIN = origin) AS Maintainer,
	GROUP_CONCAT(DISTINCT masterbuild_name) AS Masterbuild,
	GROUP_CONCAT(DISTINCT phase) AS Phase,
	GROUP_CONCAT(DISTINCT errortype) AS ErrorType,
	datetime(max(last_failed), 'unixepoch', 'localtime') AS LastFailed,
	datetime(max(last_succeeded), 'unixepoch', 'localtime') AS LastSucceeded,
	datetime(max(last_ignored), 'unixepoch', 'localtime') AS LastIgnored,
	datetime(max(last_skipped), 'unixepoch', 'localtime') AS LastSkipped
FROM
	broken
GROUP BY
	origin
ORDER BY
	origin
;
