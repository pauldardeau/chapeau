// Chapeau against a SQLite built with SQLITE_THREADSAFE=2 (multi-thread mode),
// a library that open() must NOT refuse (H1a-12b). Built, like
// threadsafe0_probe.cpp, only with -DCHAPEAU_THREADSAFE0_TEST=ON, from the same
// pinned amalgamation; Chapeau's sources are compiled into the program.
//
//   chapeau_threadsafe2_probe <scenario> | list
//
// Without this, the guard could be "refuse anything but serialized mode" and
// every other test would still pass, because the system SQLite is mode 1 and the
// other pinned build is mode 0.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "sqlite3.h"

#include "process_setup.h"
#include "scoped_temp_dir.h"
#include "SQLiteDatabase.h"
#include "SQLiteResultSet.h"

using namespace chapeau;

namespace {

int g_failures = 0;
std::string g_dir;

void check(const std::string& label, bool ok) {
   if (!ok) {
      ++g_failures;
   }
   std::printf("CHECK %s: %s\n", label.c_str(), ok ? "PASS" : "FAIL");
}

using Scenario = std::function<void()>;

std::map<std::string, Scenario> scenarios() {
   std::map<std::string, Scenario> s;

   s["threadsafe2_library_is_what_the_build_says"] = [] {
      std::printf("EVIDENCE sqlite3_libversion()=%s sqlite3_threadsafe()=%d\n", sqlite3_libversion(), sqlite3_threadsafe());
      check("sqlite3_threadsafe() is 2 (multi-thread)", sqlite3_threadsafe() == 2);
   };

   s["threadsafe2_open_is_allowed"] = [] {
      check("sanity: sqlite3_threadsafe() is 2", sqlite3_threadsafe() == 2);
      const std::string path = (std::filesystem::path(g_dir) / "multi.sqlite3").string();
      SQLiteDatabase db(path);
      db.setLogsErrors(false);
      check("open() is not refused", db.open());
      check("no error is recorded", db.lastErrorKind() == DBErrorKind::None && db.lastErrorCode() == 0 && db.lastErrorMessage().empty());
      check("there is a connection", db.sqliteHandle() != nullptr);
      unsigned long rows = 0;
      check("it works: create", db.executeUpdate("create table t(a integer)", rows));
      check("it works: insert", db.executeUpdate("insert into t(a) values (7)", rows) && rows == 1);
      std::unique_ptr<DBResultSet> rs(db.executeQuery("select a from t"));
      check("it works: query", rs && rs->next() && rs->longForColumnIndex(0) == 7);
      rs.reset();
      check("a second open() is still AlreadyOpen", !db.open() && db.lastErrorKind() == DBErrorKind::AlreadyOpen);
      db.close();
      check("close, and open again", db.open() && db.lastErrorKind() == DBErrorKind::None);
      db.close();
   };

   return s;
}

}


int main(int argc, char** argv) {
   // Windows: no dialogs, abort() fails fast and says so (nothing on other platforms)
   chapeau_harness::prepareProcess();

   std::setvbuf(stdout, nullptr, _IONBF, 0);

   if (argc < 2) {
      std::fprintf(stderr, "usage: chapeau_threadsafe2_probe <scenario> | list\n");
      return 2;
   }

   const std::map<std::string, Scenario> all = scenarios();
   if (!std::strcmp(argv[1], "list")) {
      for (const auto& entry : all) {
         std::printf("%s\n", entry.first.c_str());
      }
      return 0;
   }

   const auto found = all.find(argv[1]);
   if (found == all.end()) {
      std::fprintf(stderr, "unknown scenario '%s'\n", argv[1]);
      return 2;
   }

   chapeau_tests::ScopedTempDir dir("chapeau-threadsafe2");
   g_dir = dir.path().string();

   found->second();

   std::printf("SCENARIO %s finished, %d failed check(s)\n", argv[1], g_failures);
   return g_failures == 0 ? 0 : 1;
}
