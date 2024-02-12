// Copyright (C) 2024 by Yuri Victorovich. All rights reserved.


const char *dbSchema = R"(
	--
	-- Tables and indexes
	--

	CREATE TABLE IF NOT EXISTS server (
		id              INTEGER PRIMARY KEY AUTOINCREMENT,
		url             TEXT NOT NULL UNIQUE
	);
	CREATE TABLE IF NOT EXISTS masterbuild (
		id              INTEGER PRIMARY KEY AUTOINCREMENT,
		server_id       INTEGER NOT NULL,
		name            TEXT NOT NULL UNIQUE,
		enabled         INTEGER NOT NULL,
		FOREIGN KEY (server_id) REFERENCES server(id)
	);
	CREATE TABLE IF NOT EXISTS build (
		id              INTEGER PRIMARY KEY AUTOINCREMENT,
		masterbuild_id  INTEGER NOT NULL,
		name            TEXT NOT NULL,
		started         INTEGER NULL,
		ended           INTEGER NULL,
		status          TEXT NULL,
		last_modified   TEXT NOT NULL,
		FOREIGN KEY (masterbuild_id) REFERENCES masterbuild(id)
	);
	CREATE INDEX IF NOT EXISTS index_build_masterbuild_id ON build(masterbuild_id);
	--CREATE TABLE IF NOT EXISTS tobuild (
	--	build_id        INTEGER NOT NULL,
	--	origin          TEXT NOT NULL,
	--	pkgname         TEXT NULL,
	--	PRIMARY KEY     (build_id, origin, pkgname),
	--	FOREIGN KEY (build_id) REFERENCES build(id)
	--);
	CREATE TABLE IF NOT EXISTS queued (
		build_id        INTEGER NOT NULL,
		origin          TEXT NOT NULL,
		pkgname         TEXT NOT NULL,
		reason          TEXT NOT NULL,
		PRIMARY KEY     (build_id, origin, pkgname),
		FOREIGN KEY (build_id) REFERENCES build(id)
	);
	CREATE TABLE IF NOT EXISTS built (
		build_id        INTEGER NOT NULL,
		origin          TEXT NOT NULL,
		pkgname         TEXT NOT NULL,
		elapsed         INTEGER NOT NULL,
		PRIMARY KEY     (build_id, origin, pkgname),
		FOREIGN KEY (build_id) REFERENCES build(id)
	);
	CREATE INDEX IF NOT EXISTS index_built_origin ON built(origin);
	CREATE TABLE IF NOT EXISTS failed (
		build_id        INTEGER NOT NULL,
		origin          TEXT NOT NULL,
		pkgname         TEXT NOT NULL,
		phase           TEXT NOT NULL,
		errortype       TEXT NOT NULL,
		elapsed         INTEGER NOT NULL,
		PRIMARY KEY     (build_id, origin, pkgname),
		FOREIGN KEY (build_id) REFERENCES build(id)
	);
	CREATE INDEX IF NOT EXISTS index_failed_origin ON failed(origin);
	CREATE TABLE IF NOT EXISTS ignored (
		build_id        INTEGER NOT NULL,
		origin          TEXT NOT NULL,
		pkgname         TEXT NOT NULL,
		reason          TEXT NOT NULL,
		PRIMARY KEY     (build_id, origin, pkgname, reason),
		FOREIGN KEY (build_id) REFERENCES build(id)
	);
	CREATE TABLE IF NOT EXISTS skipped (
		build_id        INTEGER NOT NULL,
		origin          TEXT NOT NULL,
		pkgname         TEXT NOT NULL,
		depends         TEXT NOT NULL,
		PRIMARY KEY     (build_id, origin, pkgname, depends),
		FOREIGN KEY (build_id) REFERENCES build(id)
	);
	CREATE TABLE IF NOT EXISTS schema_version (
		version         INTEGER NOT NULL
	);

	--
	-- Views
	--

	CREATE VIEW IF NOT EXISTS server_masterbuild_build AS -- this view is for debugging purposes only
	SELECT
		s.id AS server_id,
		s.url AS server_url,
		m.id AS masterbuild_id,
		m.name AS masterbuild_name,
		m.enabled AS masterbuild_enabled,
		b.id AS build_id,
		b.name AS build_name,
		b.started AS build_started_raw,
		datetime(b.started, 'unixepoch', 'localtime') AS build_started_str,
		b.ended AS build_ended_raw,
		datetime(b.ended, 'unixepoch', 'localtime') AS build_ended_str,
		b.status AS build_status,
		b.last_modified AS build_last_modified,
		(SELECT count(*) FROM queued WHERE build_id = b.id) AS num_queued,
		(SELECT count(*) FROM built WHERE build_id = b.id) AS num_built,
		(SELECT count(*) FROM failed WHERE build_id = b.id) AS num_failed,
		(SELECT count(*) FROM skipped WHERE build_id = b.id) AS num_skipped,
		(SELECT count(*) FROM ignored WHERE build_id = b.id) AS num_ignored
	FROM
		server s,
		masterbuild m,
		build b
	WHERE
		s.id = m.server_id
		AND
		m.id = b.masterbuild_id
	;

	CREATE VIEW IF NOT EXISTS failed_last AS
	SELECT
		*
	FROM
		failed f
	WHERE
		f.build_id = (
			SELECT
				id
			FROM
				build
			WHERE
				masterbuild_id = (SELECT masterbuild_id FROM build WHERE id = f.build_id)
				AND
				EXISTS (SELECT * FROM failed WHERE origin = f.origin AND build_id = id)
			ORDER BY
				started DESC
			LIMIT 1
		)
	;
	CREATE VIEW IF NOT EXISTS built_last AS
	SELECT
		*
	FROM
		built s
	WHERE
		s.build_id = (
			SELECT
				id
			FROM
				build
			WHERE
				masterbuild_id = (SELECT masterbuild_id FROM build WHERE id = s.build_id)
				AND
				EXISTS (SELECT * FROM built WHERE origin = s.origin AND build_id = id)
			ORDER BY
				started DESC
			LIMIT 1
		)
	;
	CREATE VIEW IF NOT EXISTS ignored_last AS
	SELECT
		*
	FROM
		ignored i
	WHERE
		i.build_id = (
			SELECT
				id
			FROM
				build
			WHERE
				masterbuild_id = (SELECT masterbuild_id FROM build WHERE id = i.build_id)
				AND
				EXISTS (SELECT * FROM ignored WHERE origin = i.origin AND build_id = id)
			ORDER BY
				started DESC
			LIMIT 1
		)
	;
	CREATE VIEW IF NOT EXISTS skipped_last AS
	SELECT
		*
	FROM
		skipped s
	WHERE
		s.build_id = (
			SELECT
				id
			FROM
				build
			WHERE
				masterbuild_id = (SELECT masterbuild_id FROM build WHERE id = s.build_id)
				AND
				EXISTS (SELECT * FROM skipped WHERE origin = s.origin AND build_id = id)
			ORDER BY
				started DESC
			LIMIT 1
		)
	;
	CREATE VIEW IF NOT EXISTS broken AS
	SELECT
		m.id AS masterbuild_id,
		m.name AS masterbuild_name,
		bf.id AS build_id,
		bf.name AS build_name,
		f.origin AS origin,
		f.phase AS phase,
		f.errortype AS errortype,
		f.elapsed AS elapsed,
		bf.started AS last_failed,
		(SELECT started FROM build WHERE id = s.build_id) AS last_succeeded,
		(SELECT started FROM build WHERE id = i.build_id) AS last_ignored,
		(SELECT started FROM build WHERE id = k.build_id) AS last_skipped
	FROM
		masterbuild m,
		build bf,
		failed_last f
	LEFT JOIN
		built_last s
	ON
		s.build_id in (SELECT id FROM build WHERE masterbuild_id = m.id)
		AND
		s.origin = f.origin
	LEFT JOIN
		ignored_last i
	ON
		i.build_id in (SELECT id FROM build WHERE masterbuild_id = m.id)
		AND
		i.origin = f.origin
	LEFT JOIN
		skipped_last k
	ON
		k.build_id in (SELECT id FROM build WHERE masterbuild_id = m.id)
		AND
		k.origin = f.origin
	WHERE
		m.id = bf.masterbuild_id
		AND
		m.enabled = 1
		AND
		bf.id = f.build_id
		AND
		(
			(last_succeeded IS NULL AND last_ignored IS NULL)
			OR
			(last_succeeded IS NOT NULL AND last_ignored IS NULL AND last_failed > last_succeeded)
			OR
			(last_succeeded IS NULL AND last_ignored IS NOT NULL AND last_failed > last_ignored)
			OR
			(last_succeeded IS NOT NULL AND last_ignored IS NOT NULL AND last_failed > max(last_succeeded, last_ignored))
		)
	ORDER BY
		last_failed
	;
)";
