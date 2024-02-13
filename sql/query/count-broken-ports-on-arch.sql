
SELECT
	count(DISTINCT(origin)) AS Count
FROM
	broken
WHERE
	masterbuild_name LIKE '%%%s-%%'
