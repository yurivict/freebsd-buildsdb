// Copyright (C) 2024 by Yuri Victorovich. All rights reserved.


#include <array>
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>
#include <taskflow/taskflow.hpp>

// type shortcuts
using json = nlohmann::json;
namespace fs = std::filesystem;


//
// useful macros
//

#define __STR__(msg...) \
	([&]() { \
	  std::ostringstream __ss__; \
	  __ss__ << msg; \
	  return __ss__.str(); \
	}())
#define STR(msg...) __STR__(msg)
#define CSTR(msg...) (STR(msg).c_str())
#define __PRINT__(stream, msg...) \
	{ \
		stream << __STR__(msg << std::endl); \
		stream.flush(); \
	}
#define PRINT(msg...) __PRINT__(std::cout, msg)
#define PRINTe(msg...) __PRINT__(std::cerr, msg)
#define FAIL(msg...) throw std::runtime_error(__STR__(msg));
#define SQL_STMT(var, sql) \
	static SQLite::Statement var(db, sql); \
	var.reset();

#define MSG(msg...)     PRINT(timestamp() << ": " << msg) // user message
#define WARNING(msg...) MSG("warning: " << msg)
#define DEBUG(msg...) // PRINT("DEBUG: " << msg) // disable this macro for production

//
// consts
//

enum YesNoAny {Yes, No, Any};

//
// extern declarations
//

extern const char *dbSchema;

//
// global variables
//

static std::string argv0;

//
// types
//

typedef uint32_t  Time;
typedef uint32_t  Count;

//
// utility functions
//

static std::string timestamp() {
    // Get the current time
    time_t current_time = time(NULL);

    // Convert the time to a string using the desired format
    char buf[100];
    ::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ::localtime(&current_time));

    return buf;

}

static bool contains(const std::string &str, char chr) {
	return str.find(chr) != std::string::npos;
}

static bool contains(const std::string &str, const char *small) {
	return str.find(small) != std::string::npos;
}

static bool equals(const char *str, const char *other) {
	return ::strcmp(str, other) == 0;
}

static std::string execCommand(const char* cmd) {
	std::string result;
	std::array<char, 128> buffer;
	FILE *pipe = ::popen(cmd, "r");
	if (!pipe)
		throw std::runtime_error("popen() failed to execute the command");

	// read the output
	while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
        	result += buffer.data();

	// close the pipe and check process exit code
	if (WEXITSTATUS(::pclose(pipe)) != EXIT_SUCCESS)
		FAIL("the external process has failed")

	return result;
}

static std::vector<std::string> splitString(const std::string &str, char sep) {
	std::vector<std::string> v;
	std::string parsed;
	std::stringstream is(str);

	while (std::getline(is, parsed, sep))
		v.push_back(parsed);

	return v;
}

static size_t writeData(void *ptr, size_t size, size_t nmemb, std::string *str) {
	auto off = str->size();
	// change capacity aggressively
	if (str->capacity() < off + size*nmemb)
		str->reserve((off + size*nmemb)*10);
	// append
	str->resize(off + size*nmemb);
	std::memcpy(&(*str)[off], (const char*)ptr, size*nmemb);
	//
	return nmemb;
}

static void writeFile(const std::string &fileName, const std::string &content) {
	std::ofstream myfile;
	myfile.open(fileName);
	myfile << content;
	myfile.close();
}

static std::tuple<bool/*waived*/,std::string/*content*/> fetchDataFromURL(
	const std::string &url,
	const std::string *knownLastModified = nullptr,
	std::string *needLastModified = nullptr
) {
	// helpers
	auto getOneHeader = [](CURL *curl, const char *header_name) -> std::string {
		struct curl_header *prev = nullptr;
		while (auto h = curl_easy_nextheader(curl, CURLH_HEADER, 0, prev)) {
			if (equals(h->name, header_name)) {
				return h->value;
			}
			prev = h;
		}
		WARNING("no Last-Modified field is present in the server response")
		return "";
	};

	// run
	if (auto curl = curl_easy_init()) {
		// set general options
		if (::getenv("HTTP_PROXY"))
			curl_easy_setopt(curl, CURLOPT_PROXY, ::getenv("HTTP_PROXY")); // ex. "socks5://localhost:9050"
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

		// is same Last-Modified?
		if (knownLastModified && !knownLastModified->empty()) {
			// set options for the HEADER request
			curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

			// perform the HEADER request
			curl_easy_perform(curl);

			// get the header
			auto newLastModified = getOneHeader(curl, "Last-Modified");

			// restore options
			curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);

			// see if it is the same
			if (!newLastModified.empty() && newLastModified == *knownLastModified) {
				curl_easy_cleanup(curl);
				//MSG("FETCH: YES waived request for URL=" << url)
				return {true/*waived*/, ""};
			}
			//MSG("FETCH: NOT waived request for URL=" << url << ": knownLastModified=" << *knownLastModified << " != newLastModified=" << newLastModified)
		}

		// string buffer
		std::string str;
		str.reserve(1024*10);

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeData); // fn
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &str); // for fn

		// run request
		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK)
			FAIL("failed to fetch from a URL: " << curl_easy_strerror(res))

		// get HTTP Last-Modified if requested
		if (needLastModified) {
			*needLastModified = getOneHeader(curl, "Last-Modified");
		}

		// cleanup
		curl_easy_cleanup(curl);

		// dump files if requested
		if (::getenv("BUILDSDB_DUMP_DOWNLOADED_FILES")) {
			static unsigned fileNo = 1;

			// generate debug file number
			unsigned fno;
			if (::getenv("BUILDSDB_SEQUENTIAL"))
				fno = fileNo++;
			else {
				static std::mutex mutex;
				std::lock_guard<std::mutex> guard(mutex);
				fno = fileNo++;
			}

			// write debug files
			writeFile(STR("debug." << fno << ".url.txt"), url);
			writeFile(STR("debug." << fno << ".content.json"), str);
		}

		return {false/*not waived*/, str};
	} else {
		throw std::runtime_error(STR("failed to initialize CURL for the URL '" << url << "'"));
	}
}

static json F(json j, const char *name) { // get JSON field
	// checks
	if (!j.is_object())
		FAIL("JSON is not an object while looking for the field '" << name << "'")
	if (!j.contains(name))
		FAIL("JSON object doesn't contain the field '" << name << "'")

	// return field
	return j[name];
}

static bool HAS(json j, const char *name) {
	// checks
	if (!j.is_object())
		FAIL("JSON is not an object while looking for the field '" << name << "'")

	// return
	return j.contains(name);
}

static unsigned S2U(const std::string &str) {
	return (unsigned)std::stoul(str);
}

static json fixupReplaceEmptyWithZero(const json &j) {
	return j.empty() ? j : json("0");
}

static std::string dbPath() {
	return ::getenv("BUILDSDB_DATABASE") ? ::getenv("BUILDSDB_DATABASE") : "builds.sqlite";
}


//
// structures
//

struct BuildInfo {
	struct Base {
		std::string    origin;
		std::string    pkgname;
	};
	//struct ToBuild : Base {
	//};
	struct Queued : Base {
		std::string    reason;
	};
	struct Built : Base {
		Time           elapsed;
	};
	struct Failed : Base {
		std::string    phase; // XXX should this be FK index?
		std::string    errortype;
		Time           elapsed;
	};
	struct Ignored : Base {
		std::string    reason;
	};
	struct Skipped : Base {
		std::string    depends;
	};

	BuildInfo()
	: waived(false)
	{ }

	// varous fields
	bool            waived; // no need to fetch since the DB already has the same version
	std::string     last_modified;

	// build summary info
	std::string     buildname;
	std::string     jailname;
	Time            started;
	Time            ended;
	std::string     status;

	// details
	//std::vector<ToBuild>   tobuild;
	std::vector<Queued>    queued;
	std::vector<Built>     built;
	std::vector<Failed>    failed;
	std::vector<Ignored>   ignored;
	std::vector<Skipped>   skipped;
};

typedef std::shared_ptr<BuildInfo> BuildInfoPtr;

typedef std::map<std::string/*server*/, std::map<std::string/*masterbuild*/, std::vector<BuildInfoPtr>>> BuildInfos;

struct Parser {
	static std::vector<std::string> parseServerMasterBuilds(const json &j) { // assumes json to be an object
		std::vector<std::string> mbs;

		// check
		if (!j.is_object())
			FAIL("JSON isn't an object")

		// extract data
		for (auto i = j.begin(); i != j.end(); i++)
			mbs.push_back(i.key());

		return mbs;
	}
	static BuildInfoPtr parseBuildSummary(const json &j, const std::string &masterbuild, const std::string &buildname) { // assumes json to be an object
		auto bi = std::make_shared<BuildInfo>();

		bi->buildname     = F(j, "buildname");
		bi->jailname      = F(j, "jailname");
		bi->started       = S2U(F(j, "started"));
		bi->ended         = HAS(j, "ended") ? Time(S2U(F(j, "ended"))) : 0;
		bi->status        = F(j, "status");

		return bi;
	}
	static std::vector<BuildInfoPtr> parseBuildSummaries(const json &j, const std::string &masterbuild) { // assumes json to be an object
		std::vector<BuildInfoPtr> bis;

		// check
		if (!j.is_object())
			FAIL("JSON isn't an object")

		// progress message
		MSG("... ... there are " << j.size() << " builds to fetch for masterbuild=" << masterbuild)

		// extract data
		for (auto i = j.begin(); i != j.end(); i++)
			if (i.key() != "latest")
				bis.push_back(parseBuildSummary(i.value(), masterbuild, (std::string)i.key()));
			else
				{/*TODO*/}

		return bis;
	}
	static void parseBuildDetails(const json &j, BuildInfo &bi, const std::string &mastername) {
		typedef BuildInfo BI;

		// checks
		if (!j.is_object())
			FAIL("JSON isn't an object")
		if (F(j, "mastername") != mastername)
			FAIL("invalid record: mastername mismatches")
		if (F(j, "buildname") != bi.buildname)
			FAIL("invalid record: buildname mismatches")
		if (F(j, "jailname") != bi.jailname)
			FAIL("invalid record: jailname mismatches")

		// individual parsers

		if (!j.contains("ports"))
			return;
		auto jPorts = F(j, "ports");
		for (auto i = jPorts.begin(); i != jPorts.end(); i++)
			if (i.key() == "tobuild")
#if 0
				parseArray<BI::ToBuild>(i.value(), bi.tobuild, [](const json &j) {
					if (!j.is_object())
						FAIL("JSON isn't an object")
					return BI::ToBuild{
						BI::Base{
							F(j, "origin"),
							F(j, "pkgname")
						}
					};
				});
#endif
				{ }
			else if (i.key() == "queued")
				parseArray<BI::Queued>(i.value(), bi.queued, [](const json &j) {
					if (!j.is_object())
						FAIL("JSON isn't an object")
					return BI::Queued{
						BI::Base{
							F(j, "origin"),
							F(j, "pkgname")
						},
						F(j, "reason")
					};
				});
			else if (i.key() == "built")
				parseArray<BI::Built>(i.value(), bi.built, [](const json &j) {
					if (!j.is_object())
						FAIL("JSON isn't an object")
					return BI::Built{
						BI::Base{
							F(j, "origin"),
							F(j, "pkgname")
						},
						S2U(F(j, "elapsed"))
					};
				});
			else if (i.key() == "failed")
				parseArray<BI::Failed>(i.value(), bi.failed, [](const json &j) {
					if (!j.is_object())
						FAIL("JSON isn't an object")
					return BI::Failed{
						BI::Base{
							F(j, "origin"),
							F(j, "pkgname")
						},
						F(j, "phase"),
						F(j, "errortype"),
						S2U(fixupReplaceEmptyWithZero(F(j, "elapsed"))) // 'elapsed' can be empty when it fails in the 'starting' phase
					};
				});
			else if (i.key() == "ignored")
				parseArray<BI::Ignored>(i.value(), bi.ignored, [](const json &j) {
					if (!j.is_object())
						FAIL("JSON isn't an object")
					return BI::Ignored{
						BI::Base{
							F(j, "origin"),
							F(j, "pkgname")
						},
						F(j, "reason")
					};
				});
			else if (i.key() == "skipped")
				parseArray<BI::Skipped>(i.value(), bi.skipped, [](const json &j) {
					if (!j.is_object())
						FAIL("JSON isn't an object")
					return BI::Skipped{
						BI::Base{
							F(j, "origin"),
							F(j, "pkgname")
						},
						F(j, "depends")
					};
				});
			else
				FAIL("unknown key '" << i.key() << "' found in the details record")
	}
	template<typename T>
	static void parseArray(const json &j, std::vector<T> &arr, std::function<T(const json &j)> one) {
		// check
		if (!j.is_array())
			FAIL("JSON isn't an array")

		// reserve arr entries
		arr.reserve(j.size()); // assume that it's size=0

		for (unsigned i = 0; i < j.size(); i++)
			arr.push_back(one(j[i]));
	}
};

struct Database : SQLite::Database {
	Database(bool create)
	: SQLite::Database(
		dbPath().c_str(),
		SQLite::OPEN_READWRITE|(create ? SQLite::OPEN_CREATE : 0)
	) { }

	static bool canOpenExistingDB() {
		try {
			Database(false);
			return true;
		} catch(...) {
			return false;
		}
	}
};

//
// main procedures
//

static std::vector<std::string> fetchServerList() {
	std::vector<std::string> servers;

	// retrieve the list of servers
	auto serversStr = execCommand(
		"set -euo pipefail\n"
		"fetch -qo - \"https://pkg-status.freebsd.org/api/1/builds?type=package\" |"
		"  jq -r \".. | select(.started? > $(date -v -2w +%s)) | .server\" | sort -u"
	);
	DEBUG("servers: " << serversStr)

	//serversStr = serversStr + "foul1\nfoul2";

	// decorate their names
	for (auto s : splitString(serversStr, '\n'))
	//for (auto s : {"beefy8"})
		//servers.push_back(STR("http://" << s << ".nyi.freebsd.org")); // via IPv6
		servers.push_back(STR("https://pkg-status.freebsd.org/" << s)); // via IPv4

	// check
	if (servers.empty())
		FAIL("failed to fetch the build server list: it is empty")

	MSG("found " << servers.size() << " build servers")

	return servers;
}

static void fetchBuildInfo(
	const std::vector<std::string> &servers,
	BuildInfos &buildInfos,
	Database &db // only to retrieve lastModified
) {
	// retrieve the build.last_modified field from DB so that we can skip builds that weren't changed
	std::map<std::string/*masterbuild*/, std::map<std::string/*buildname*/, std::string/*last_modified*/>> lastModifiedInDB;
	{
		SQLite::Statement stmt(db, "SELECT m.name, b.name, b.last_modified FROM masterbuild m, build b WHERE m.id = b.masterbuild_id");
		while (stmt.executeStep())
			lastModifiedInDB[stmt.getColumn(0)][stmt.getColumn(1)] = (std::string)stmt.getColumn(2);
	}
	auto getLastModifiedInDB = [&lastModifiedInDB](const std::string &mastername, const std::string &buildname) -> const std::string* {
		auto im = lastModifiedInDB.find(mastername);
		if (im == lastModifiedInDB.end())
			return nullptr;
		auto ib = im->second.find(buildname);
		if (ib != im->second.end())
			return &ib->second;
		else
			return nullptr;
	};

	// run
	if (::getenv("BUILDSDB_SEQUENTIAL")) {
		MSG("sequential run")
		// by server
		for (auto &server : servers) {
			MSG("fetching data from the server " << server)

			// info for this server
			auto &buildInfo = buildInfos[server];

			auto [waived, str] = fetchDataFromURL(STR(server << "/data/.data.json"));
			DEBUG("JSON-STRING(server=" << server << ")=" << str)

			// for each master build on this server
			for (auto &mastername : Parser::parseServerMasterBuilds(F(json::parse(str), "masternames"))) {
				MSG("... fetching builds for " << mastername << " from the server " << server)

				// fetch data
				auto [waived, str] = fetchDataFromURL(STR(server << "/data/" << mastername << "/.data.json"));
				DEBUG("JSON-STRING(server=" << server << " mastername=" << mastername << ")=" << str)

				// check
				if (str.rfind("<html>", 0) == 0)
					continue; // skip the blank record

				// parse JSON with build summary info
				for (auto &bi : buildInfo[mastername] = Parser::parseBuildSummaries(F(json::parse(str), "builds"), mastername)) {
					// fetch data
					auto [waived, str] = fetchDataFromURL(
						STR(server << "/data/" << mastername << "/" << bi->buildname << "/.data.json"),
						getLastModifiedInDB(mastername, bi->buildname),
						&bi->last_modified
					);
					DEBUG("JSON-STRING(server=" << server << " mastername=" << mastername << " buildname=" << bi->buildname << ")=" << str)

					// parse JSON with build details
					if (!(bi->waived = waived))
						Parser::parseBuildDetails(json::parse(str), *bi, mastername);
				}
			}
		}
	} else { // PARALLELIZED
		MSG("parallel run")

		tf::Executor executor(32); // the bottleneck is mostly due to network fetch speed, not CPU, so choose a high number here
		tf::Taskflow taskflow;

		std::mutex buildInfosMutex;

		for (auto &server : servers)
			taskflow.emplace([server,&buildInfos,&buildInfosMutex,&getLastModifiedInDB](tf::Subflow &subflow) {
				// fetch data
				auto [waived, str] = fetchDataFromURL(STR(server << "/data/.data.json"));

				// parse JSON with masterbuilds for this server
				for (auto &mastername : Parser::parseServerMasterBuilds(F(json::parse(str), "masternames")))
					subflow.emplace([mastername,server,&buildInfos,&buildInfosMutex,&getLastModifiedInDB](tf::Subflow &subflow) {
						// fetch data
						auto [waived, str] = fetchDataFromURL(STR(server << "/data/" << mastername << "/.data.json"));

						// check
						if (str.rfind("<html>", 0) == 0)
							return; // skip the blank record

						// parse JSON with build summary info
						auto bis = Parser::parseBuildSummaries(F(json::parse(str), "builds"), mastername);

						{ // save build info objects into buildInfos
							std::lock_guard<std::mutex> guard(buildInfosMutex);
							buildInfos[server][mastername] = bis;
						}

						// process all builds in this masterbuild
						for (auto &bi : bis)
							subflow.emplace([bi,server,mastername,&getLastModifiedInDB]() {
								// fetch data
								auto [waived, str] = fetchDataFromURL(
									STR(server << "/data/" << mastername << "/" << bi->buildname << "/.data.json"),
									getLastModifiedInDB(mastername, bi->buildname),
									&bi->last_modified
								);

								// parse JSON with build details
								if (!(bi->waived = waived))
									Parser::parseBuildDetails(json::parse(str), *bi, mastername);
							});
					});
			});

		// run
		executor.run(taskflow).wait();
	}
}

static unsigned writeBuildInfoToDB(const BuildInfos &buildInfos, Database &db) {
	MSG("saving builds into the database")

	// helpers
	auto enableInitially = [](const std::string &masterbuild) {
		bool disabled =
			contains(masterbuild, "124") // obsolete
			||
			contains(masterbuild, "powerpc") // too many failures on powerpc compared to other archs
			;

		return !disabled;
	};

	// enable foreign keys
	db.exec("PRAGMA foreign_keys = ON"); // this doesn't cause performance problem practically, otheriwse PRAGMA foreign_key_check; should be run in the end

	// update the server table
	for (auto &i : buildInfos) {
		auto const &server = i.first;

		SQL_STMT(stmtInsertServer, "INSERT INTO server(url) VALUES(?)")
		SQL_STMT(stmtSelectServer, "SELECT id FROM server WHERE url=?")
		stmtSelectServer.bind(1, server);
		if (!stmtSelectServer.executeStep()) {
			stmtInsertServer.bind(1, server);
			stmtInsertServer.exec();
		}
	}

	// global vars
	unsigned bno = 0;

	unsigned numberBuildsToSave = 0;
	for (auto s : buildInfos)
		for (auto &m : s.second)
			for (auto &bi : m.second)
				if (!bi->waived)
					numberBuildsToSave++;

	// update masterbuild, build, and further tables
	for (auto &s : buildInfos) {
		// get server_id
		SQL_STMT(stmtSelectServer, "SELECT id FROM server WHERE url=?")
		stmtSelectServer.bind(1, s.first);
		stmtSelectServer.executeStep();
		const unsigned server_id = stmtSelectServer.getColumn(0);

		// by masterbuilds on this server
		for (auto &m : s.second) {
			SQL_STMT(stmtInsertMasterbuild, "INSERT INTO masterbuild(server_id,name,enabled) VALUES(?,?,?)")
			SQL_STMT(stmtSelectMasterbuild, "SELECT id FROM masterbuild WHERE server_id=? AND name=?")

			stmtSelectMasterbuild.bind(1, server_id);
			stmtSelectMasterbuild.bind(2, m.first/*masterbuild*/);
			if (!stmtSelectMasterbuild.executeStep()) {
				stmtInsertMasterbuild.bind(1, server_id);
				stmtInsertMasterbuild.bind(2, m.first/*masterbuild_name*/);
				stmtInsertMasterbuild.bind(3, enableInitially(m.first) ? 1 : 0 /*enabled*/);
				stmtInsertMasterbuild.exec();
				stmtSelectMasterbuild.reset();
				stmtSelectMasterbuild.bind(1, server_id);
				stmtSelectMasterbuild.bind(2, m.first/*masterbuild*/);
				(void)stmtSelectMasterbuild.executeStep();
			}
			const unsigned masterbuild_id = stmtSelectMasterbuild.getColumn(0);

			// by builds for this masterbuild
			for (auto &bi : m.second)
				if (!bi->waived) {
					MSG("... saving the build #" << ++bno << " of " << numberBuildsToSave << ": " << m.first << "/" << bi->buildname)

					SQL_STMT(stmtSelectBuild, "SELECT id, ended FROM build WHERE masterbuild_id=? AND name=?")
					SQL_STMT(stmtInsertBuild, "INSERT INTO build(masterbuild_id,name,started,ended,status,last_modified) VALUES(?,?,?,?,?,?)")
					SQL_STMT(stmtUpdateBuild, "UPDATE build SET ended=?, status=?, last_modified=? WHERE id=?")

					stmtSelectBuild.bind(1, masterbuild_id);
					stmtSelectBuild.bind(2, bi->buildname);
					if (!stmtSelectBuild.executeStep()) { // need to insert
						stmtInsertBuild.bind(1, masterbuild_id);
						stmtInsertBuild.bind(2, bi->buildname);
						stmtInsertBuild.bind(3, bi->started);
						if (bi->ended != 0)
							stmtInsertBuild.bind(4, bi->ended);
						stmtInsertBuild.bind(5, bi->status);
						stmtInsertBuild.bind(6, bi->last_modified);
						stmtInsertBuild.exec();
						stmtSelectBuild.reset();
						stmtSelectBuild.bind(1, masterbuild_id);
						stmtSelectBuild.bind(2, bi->buildname);
						(void)stmtSelectBuild.executeStep();
					} else { // potentially need to update
						if (bi->ended != 0)
							stmtUpdateBuild.bind(1, bi->ended);
						stmtUpdateBuild.bind(2, bi->status);
						stmtUpdateBuild.bind(3, bi->last_modified);
						stmtUpdateBuild.bind(4, (unsigned)stmtSelectBuild.getColumn(0));
						stmtUpdateBuild.exec();
					}
					const unsigned build_id = stmtSelectBuild.getColumn(0);

					// delete old and insert new records per-port

					SQLite::Transaction transaction(db);

					// delete old records
					//SQL_STMT(stmtDeleteTobuild, "DELETE FROM tobuild WHERE build_id=?")
					SQL_STMT(stmtDeleteQueued,  "DELETE FROM queued WHERE build_id=?")
					SQL_STMT(stmtDeleteBuilt,   "DELETE FROM built WHERE build_id=?")
					SQL_STMT(stmtDeleteFailed,  "DELETE FROM failed WHERE build_id=?")
					SQL_STMT(stmtDeleteIgnored, "DELETE FROM ignored WHERE build_id=?")
					SQL_STMT(stmtDeleteSkipped, "DELETE FROM skipped WHERE build_id=?")
					for (auto stmt : {/*&stmtDeleteTobuild,*/ &stmtDeleteQueued, &stmtDeleteBuilt, &stmtDeleteFailed, &stmtDeleteIgnored, &stmtDeleteSkipped}) {
						stmt->bind(1, build_id);
						stmt->exec();
					}

					// insert new records
					//for (auto &tobuild : bi->tobuild) {
					//	SQL_STMT(stmtInsertTobuild, "INSERT INTO tobuild VALUES(?,?,?)")
					//	stmtInsertTobuild.bind(1, build_id);
					//	stmtInsertTobuild.bind(2, tobuild.origin);
					//	stmtInsertTobuild.bind(3, tobuild.pkgname);
					//	stmtInsertTobuild.exec();
					//}
					for (auto &queued : bi->queued) {
						SQL_STMT(stmtInsertQueued, "INSERT INTO queued VALUES(?,?,?,?)")
						stmtInsertQueued.bind(1, build_id);
						stmtInsertQueued.bind(2, queued.origin);
						stmtInsertQueued.bind(3, queued.pkgname);
						stmtInsertQueued.bind(4, queued.reason);
						stmtInsertQueued.exec();
					}
					for (auto &built : bi->built) {
						SQL_STMT(stmtInsertBuilt, "INSERT INTO built VALUES(?,?,?,?)")
						stmtInsertBuilt.bind(1, build_id);
						stmtInsertBuilt.bind(2, built.origin);
						stmtInsertBuilt.bind(3, built.pkgname);
						stmtInsertBuilt.bind(4, built.elapsed);
						stmtInsertBuilt.exec();
					}
					for (auto &failed : bi->failed) {
						SQL_STMT(stmtInsertFailed, "INSERT INTO failed VALUES(?,?,?,?,?,?)")
						stmtInsertFailed.bind(1, build_id);
						stmtInsertFailed.bind(2, failed.origin);
						stmtInsertFailed.bind(3, failed.pkgname);
						stmtInsertFailed.bind(4, failed.phase);
						stmtInsertFailed.bind(5, failed.errortype);
						stmtInsertFailed.bind(6, failed.elapsed);
						stmtInsertFailed.exec();
					}
					for (auto &ignored : bi->ignored) {
						SQL_STMT(stmtInsertIgnored, "INSERT INTO ignored VALUES(?,?,?,?)")
						stmtInsertIgnored.bind(1, build_id);
						stmtInsertIgnored.bind(2, ignored.origin);
						stmtInsertIgnored.bind(3, ignored.pkgname);
						stmtInsertIgnored.bind(4, ignored.reason);
						stmtInsertIgnored.exec();
					}
					for (auto &skipped : bi->skipped) {
						SQL_STMT(stmtInsertSkipped, "INSERT INTO skipped VALUES(?,?,?,?)")
						stmtInsertSkipped.bind(1, build_id);
						stmtInsertSkipped.bind(2, skipped.origin);
						stmtInsertSkipped.bind(3, skipped.pkgname);
						stmtInsertSkipped.bind(4, skipped.depends);
						stmtInsertSkipped.exec();
					}

					// commit
					transaction.commit();
				} else {
					//MSG("DB: YES waived masterbuild=" << m.first << "/" << bi->buildname)
				}
		}
	}

	return numberBuildsToSave; // number of saved builds
}

static bool checkDbIsPresentWithMessage(const std::string &op) {
	if (!Database::canOpenExistingDB()) {
		PRINT("the '" << op << "' operation requires DB to be present, please run 'buildsdb fetch' first")
		return false;
	}

	return true;
}

static void printSelectResult(const std::string &selectSql) {
	auto res = ::system(CSTR(
		"(echo .mode table; echo '" << selectSql << "') | sqlite3 " << dbPath()
	));
	if (res != 0)
		FAIL("SQL query failed to execute")
}

//
// main action functions
//

static int doFetch() {
	// DB object
	Database db(true/*create*/);

	// create schema
	db.exec(dbSchema);

	// fetch the build server list
	auto servers = fetchServerList();

	// we parse all data into this structure first
	BuildInfos buildInfos; // [by-server][by-masterbuild]

	// fetch build info
	fetchBuildInfo(servers, buildInfos, db);

	// write build info to DB
	auto numBuilds = writeBuildInfoToDB(buildInfos, db);

	MSG("successfully imported " << numBuilds << " build(s) from " << servers.size() << " server(s)")

	return EXIT_SUCCESS;
}

static int usage(bool fail) {
	PRINT("usage:")
	PRINT("   buildsdb fetch")
	PRINT("   or")
	PRINT("   buildsdb query {query-name} {args...}")
	PRINT("   or")
	PRINT("   buildsdb stats")
	PRINT("   or")
	PRINT("   buildsdb show-masterbuilds {|enable|disable}")
	PRINT("   or")
	PRINT("   buildsdb enable-masterbuilds {masterbuild pattern, or tier1, or tier2}")
	PRINT("   or")
	PRINT("   buildsdb disable-masterbuilds {masterbuild pattern, or tier1, or tier2}")
	PRINT("   or")
	PRINT("   buildsdb help")

	return fail ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int doStats() {
	return EXIT_SUCCESS;
}

static int doEnableMasterbuilds(const std::vector<std::string> &masterbuild_patterns, bool enable) {
	// checks
	if (!checkDbIsPresentWithMessage(enable ? "enable-masterbuilds" : "disable-masterbuilds"))
		return EXIT_FAILURE;
	for (auto &pattern : masterbuild_patterns) {
		if (pattern.empty())
			FAIL("masterbuld pattern can't be empty")
		if (contains(pattern, '\''))
			FAIL("masterbuld pattern can't contain the ' (quote) character")
	}

	// expand patterns
	std::vector<std::string> masterbuild_patterns_expanded;
	for (auto &pattern : masterbuild_patterns)
		if (pattern == "tier1")
			for (auto p : {"amd64", "arm64"})
				masterbuild_patterns_expanded.push_back(p);
		else if (pattern == "tier2")
			for (auto p : {"i386", "armv7", "powerpc"})
				masterbuild_patterns_expanded.push_back(p);
		else
			masterbuild_patterns_expanded.push_back(pattern);

	{ // execute queries
		Database db(false/*not create*/);
		SQLite::Transaction transaction(db);
		for (auto &pattern : masterbuild_patterns_expanded) {
			SQLite::Statement(
				db,
				STR("UPDATE masterbuild SET enabled=" << (enable ? '1' : '0') << " WHERE name LIKE '%" << pattern << "%'")
			).exec();
			PRINT("Masterbuilds *" << pattern << "* were " << (enable ? "enabled" : "disabled") << ".")
		}
		transaction.commit();
	}

	// show enabled
	PRINT("The list of currently 'enabled' flags for masterbuilds is:")
	printSelectResult("SELECT name AS Masterbuild, enabled AS Enabled FROM masterbuild ORDER BY name");

	return EXIT_SUCCESS;
}

static int doShowMasterbuilds(YesNoAny yna) {
	// checks
	if (!checkDbIsPresentWithMessage("show-masterbuilds"))
		return EXIT_FAILURE;

	// print
	if (yna == Any)
		printSelectResult("SELECT name AS Masterbuild, enabled AS Enabled FROM masterbuild ORDER BY name");
	else
		printSelectResult(STR("SELECT name AS Masterbuild, enabled AS Enabled FROM masterbuild WHERE enabled=" << (yna == Yes ? '1' : '0') << " ORDER BY name"));

	return EXIT_SUCCESS;
}

static int doQuery(const std::string &query, const std::vector<std::string> &args) {
	// checks
	if (!checkDbIsPresentWithMessage("query"))
		return EXIT_FAILURE;

	// process the 'help' query
	if (query == "help") {
		// find path to the queryies directory
		std::string queriesDir;
		for (auto d : {"sql", PREFIX "/share/buildsdb/sql"}) {
			auto dirPath = fs::path(d) / "query";
			if (fs::exists(dirPath)) {
				queriesDir = dirPath;
				break;
			}
		}
		if (queriesDir.empty())
			FAIL("can't find queries directory")

		// show all scripts
		PRINT("available queries are:")
		::system(CSTR("(cd " << queriesDir << " && ls | sed -e \"s|^|• |; s|\\.sql$||\") | sort"));

		return EXIT_SUCCESS;
	}

	// find path to the query SQL
	std::string sqlScriptPath;
	for (auto d : {"sql", PREFIX "/share/buildsdb/sql"}) {
		auto dirPath = fs::path(d) / "query" / STR(query << ".sql");
		if (fs::exists(dirPath)) {
			sqlScriptPath = dirPath;
			break;
		}
	}
	if (sqlScriptPath.empty())
		FAIL("query '" << query << "' doesn't exist, execute '" << argv0 << " query help' for the list of available queries")

	// convert arguments
	std::string sargs;
	for (auto &arg : args)
		sargs += STR(" " << arg);

	// run SQL
	auto res = ::system(CSTR(
		"(echo .mode table; SQL=$(cat " << sqlScriptPath << "); SQL=\"$(printf \"$SQL\"" << sargs << ")\"; echo \"$SQL\") | sqlite3 " << dbPath()
	));
	if (res != 0)
		FAIL("SQL query failed to execute")

	// try to resolve 
	return EXIT_SUCCESS;
}

//
// MAIN
//

static int mainGuarded(int argc, char* argv[]) {
	// save argv0
	argv0 = argv[0];

	// process arguments
	if (argc <= 1)
		return usage(true);
	else if (argc == 2) {
		if (equals(argv[1], "fetch"))
			return doFetch();
		else if (equals(argv[1], "stats"))
			return doStats();
		else if (equals(argv[1], "show-masterbuilds"))
			return doShowMasterbuilds(Any); // no additional args => Any
		else if (equals(argv[1], "help"))
			return usage(false);
		else
			return usage(true);
	} else {
		if (argc == 3) {
			if (equals(argv[1], "enable-masterbuilds"))
				return doEnableMasterbuilds({std::string(argv[2])}, true);
			else if (equals(argv[1], "disable-masterbuilds"))
				return doEnableMasterbuilds({std::string(argv[2])}, false);
			else if (equals(argv[1], "show-masterbuilds") && equals(argv[2], "enabled"))
				return doShowMasterbuilds(Yes);
			else if (equals(argv[1], "show-masterbuilds") && equals(argv[2], "disabled"))
				return doShowMasterbuilds(No);
			else
				{ } // fallthrough
		}
		if (equals(argv[1], "query"))
			return doQuery(argv[2], std::vector<std::string>(argv + 3, argv + argc));

		// fail to parse arguments
		return usage(true);
	}
}

int main(int argc, char* argv[]) {
	try {
		return mainGuarded(argc, argv);
	} catch (std::runtime_error &e) {
		PRINTe("error: " << e.what())
		return EXIT_FAILURE;
	} catch (json::parse_error &e) { // JSON parse error
		PRINTe("error: JSON parsing failed: " << e.what())
		return EXIT_FAILURE;
	} catch (json::exception &e) { // JSON error
		PRINTe("error: JSON error: " << e.what())
		return EXIT_FAILURE;
	} catch (std::exception &e) {
		PRINTe("error: general exception: " << e.what())
		return EXIT_FAILURE;
	}
}
