-- returns the number of broken ports on Tier1 architectures

SELECT
	COUNT(DISTINCT(origin)) AS Count
FROM
	broken
WHERE
	masterbuild_name LIKE '%%amd64%%'
	OR
	masterbuild_name LIKE '%%arm64%%'

