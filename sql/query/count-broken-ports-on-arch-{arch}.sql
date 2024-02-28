-- returns the number of broken ports for a given architecture

SELECT
	COUNT(DISTINCT(origin)) AS Count
FROM
	broken
WHERE
	masterbuild_name LIKE '%%%s-%%'
