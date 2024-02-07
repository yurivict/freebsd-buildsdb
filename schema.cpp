// Copyright (C) 2024 by Yuri Victorovich. All rights reserved.


const char *dbSchema = R"(
	CREATE TABLE IF NOT EXISTS server (
		id              INTEGER PRIMARY KEY AUTOINCREMENT,
		url             TEXT NOT NULL UNIQUE
	);
	CREATE TABLE IF NOT EXISTS masterbuild (
		id              INTEGER PRIMARY KEY AUTOINCREMENT,
		server_id       INTEGER NOT NULL,
		name            TEXT NOT NULL UNIQUE,
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
)";
