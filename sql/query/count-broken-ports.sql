-- returns the count of ports broken on some architectures

SELECT
	count(DISTINCT(origin)) AS Count
FROM
	broken

