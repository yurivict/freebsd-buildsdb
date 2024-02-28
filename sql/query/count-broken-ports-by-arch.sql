-- returns counts of broken ports by architecture

SELECT
	replace(
		replace(
			replace(
				replace(
					replace(
						replace(
							replace(
								masterbuild_name,
								'main-', ''
							), '-default', ''
						), '-quarterly', ''
					), 'releng-', ''
				), '140', ''
			), '132', ''
		), '124', ''
	) AS Arch,
	count(DISTINCT(origin)) AS Count
FROM
	broken
GROUP BY
	Arch
ORDER BY
	Arch
