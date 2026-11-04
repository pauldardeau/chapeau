// Chapeau against a SQLite that was built WITHOUT threading support (H1a-12b).
//
//   chapeau_threadsafe0_probe <scenario>   run it; exit 0 if every CHECK passed,
//                                          1 if one failed, 2 for a usage error
//   chapeau_threadsafe0_probe list         print the scenario names
//
// This program is built only with -DCHAPEAU_THREADSAFE0_TEST=ON. CMake compiles
// a pinned SQLite amalgamation with -DSQLITE_THREADSAFE=0 and Chapeau's own
// sources, together, into this one program, so the SQLite that Chapeau runs on
// here is that one and not the system's. Nothing is mocked: sqlite3_threadsafe()
// really answers 0.
//
// What a refusal has to look like, and how it is observed:
//   * open() returns false, kind UnsupportedLibrary, code 0, and a message that
//     names SQLite and its version, says it has no threading support, and says
//     what to use instead;
//   * nothing was opened: a counting VFS wrapper (below) sees no xOpen at all,
//     no file is created, sqliteHandle() is null, and SQLite's allocated memory
//     is what it was before (the counter is shown to work by a raw
//     sqlite3_open of a different file, which it does count);
//   * the object stays valid: it can be asked again, closed, and destroyed;
//   * the failure is logged exactly once per refusal when setLogsErrors(true),
//     and nothing is written when it is off.
// LeakSanitizer (when the build uses it) covers "no leaks".

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sqlite3.h"

#include "process_setup.h"
#include "scoped_temp_dir.h"
#include "stdout_capture.h"
#include "SQLiteDatabase.h"

using namespace chapeau;

#ifndef EXPECTED_SQLITE_VERSION
#error "EXPECTED_SQLITE_VERSION must be defined by the build (the version of the pinned amalgamation)"
#endif

namespace {

int g_failures = 0;
std::string g_dir;

void check(const std::string& label, bool ok) {
   if (!ok) {
      ++g_failures;
   }
   std::printf("CHECK %s: %s\n", label.c_str(), ok ? "PASS" : "FAIL");
}

void evidence(const std::string& line) {
   std::printf("EVIDENCE %s\n", line.c_str());
}

std::string pathOf(const std::string& name) {
   return (std::filesystem::path(g_dir) / name).string();
}

bool contains(const std::string& text, const std::string& part) {
   return text.find(part) != std::string::npos;
}

// ---- seeing whether anything was opened ---------------------------------------

// A VFS that is SQLite's default one with xOpen counted. Every file SQLite opens
// for a database goes through it, so the count of xOpen calls is the count of
// connections that touched a file.
sqlite3_vfs g_counting;
sqlite3_vfs* g_real = nullptr;
int g_opens = 0;

int countingOpen(sqlite3_vfs*, const char* name, sqlite3_file* file, int flags, int* outFlags) {
   ++g_opens;
   return g_real->xOpen(g_real, name, file, flags, outFlags);
}

void installCountingVfs() {
   g_real = sqlite3_vfs_find(nullptr);
   g_counting = *g_real;
   g_counting.zName = "chapeau-counting";
   g_counting.xOpen = countingOpen;
   sqlite3_vfs_register(&g_counting, 1);
}

// ---- what the library writes to stdout (the logging contract) -----------------

// What the library writes to stdout while body() runs (stdout_capture.h: the same code on POSIX and Windows). A capture that cannot be made
// throws CaptureError, which main() reports as a failure; it is never an empty result, because the scenarios below read an empty
// result as "nothing was printed".
using chapeau_harness::captureStdout;

size_t lines(const std::string& text) {
   size_t n = 0;
   for (char c : text) {
      if (c == '\n') {
         ++n;
      }
   }
   return n;
}

// the whole outcome of a refused open() as the caller can see it
std::string snapshot(SQLiteDatabase& db) {
   return std::string(dbErrorKindName(db.lastErrorKind())) + "|" + std::to_string(db.lastErrorCode()) + "|" +
          db.lastErrorMessage();
}

// ---- the scenarios ------------------------------------------------------------

using Scenario = std::function<void()>;

std::map<std::string, Scenario> scenarios() {
   std::map<std::string, Scenario> s;

   // The library under test is what the build says it is: the pinned version, and no threading support.
   s["threadsafe0_library_is_what_the_build_says"] = [] {
      evidence(std::string("sqlite3_libversion()=") + sqlite3_libversion() + " sqlite3_sourceid()=" + sqlite3_sourceid());
      evidence("sqlite3_threadsafe()=" + std::to_string(sqlite3_threadsafe()));
      bool option = false;
      for (int i = 0;; ++i) {
         const char* name = sqlite3_compileoption_get(i);
         if (name == nullptr) {
            break;
         }
         if (!std::strncmp(name, "THREADSAFE", 10) || !std::strncmp(name, "MUTEX", 5) || !std::strncmp(name, "OMIT_LOAD", 9)) {
            evidence(std::string("compile option ") + name);
         }
         if (!std::strcmp(name, "THREADSAFE=0")) {
            option = true;
         }
      }
      check("sqlite3_threadsafe() is 0", sqlite3_threadsafe() == 0);
      check("the compile option THREADSAFE=0 is reported", option);
      check(std::string("it is SQLite ") + EXPECTED_SQLITE_VERSION, std::string(sqlite3_libversion()) == EXPECTED_SQLITE_VERSION);
      check("Chapeau reports the same version", SQLiteDatabase::sqliteLibVersion() == EXPECTED_SQLITE_VERSION);
   };

   // open() is refused: the error, and proof that nothing was opened.
   s["threadsafe0_open_is_refused"] = [] {
      installCountingVfs();
      check("sanity: sqlite3_threadsafe() is 0", sqlite3_threadsafe() == 0);

      // the instrument works: a plain sqlite3_open that really opens a file is counted, and the file appears
      {
         const std::string other = pathOf("instrument.sqlite3");
         const int before = g_opens;
         sqlite3* raw = nullptr;
         check("sanity: a raw sqlite3_open succeeds", sqlite3_open(other.c_str(), &raw) == SQLITE_OK);
         check("sanity: and executes", sqlite3_exec(raw, "create table x(a)", nullptr, nullptr, nullptr) == SQLITE_OK);
         sqlite3_close(raw);
         check("sanity: the counting VFS saw the open", g_opens > before);
         check("sanity: and the file exists", std::filesystem::exists(other));
      }

      const std::string path = pathOf("unsupported.sqlite3");
      const int opensBefore = g_opens;
      const sqlite3_int64 memoryBefore = sqlite3_memory_used();

      SQLiteDatabase db(path);
      db.setLogsErrors(false);
      const bool opened = db.open();

      check("open() returns false", !opened);
      check("the kind is UnsupportedLibrary", db.lastErrorKind() == DBErrorKind::UnsupportedLibrary);
      check("the code is 0", db.lastErrorCode() == 0);
      const std::string message = db.lastErrorMessage();
      evidence("message: " + message);
      check("the message says the library is unsupported", contains(message, "unsupported library"));
      check("and names SQLite", contains(message, "SQLite"));
      check(std::string("and its version (") + EXPECTED_SQLITE_VERSION + ")", contains(message, std::string("SQLite ") + EXPECTED_SQLITE_VERSION));
      check("and says it has no threading support", contains(message, "without threading support"));
      check("and that nothing was opened", contains(message, "nothing was opened"));
      check("and recommends a build with threading enabled", contains(message, "Use a SQLite build with threading enabled"));
      check("hadError() is true", db.hadError());

      check("no connection was created (sqliteHandle() is null)", db.sqliteHandle() == nullptr);
      check("no file was opened (the counting VFS saw nothing)", g_opens == opensBefore);
      check("no file was created", !std::filesystem::exists(path));
      check("SQLite allocated nothing that is still held", sqlite3_memory_used() == memoryBefore);

      // the object is an ordinary unopened one
      check("databasePath() is intact", db.databasePath() == path);
      check("goodConnection() is false", !db.goodConnection());
      unsigned long rows = 99;
      check("executeUpdate() reports that there is no connection", !db.executeUpdate("create table t(a)", rows) && rows == 0 &&
            db.lastErrorKind() == DBErrorKind::NoConnection);
      std::unique_ptr<DBResultSet> rs(db.executeQuery("select 1"));
      check("executeQuery() reports that there is no connection", rs == nullptr && db.lastErrorKind() == DBErrorKind::NoConnection);
      check("and still nothing was opened", g_opens == opensBefore && !std::filesystem::exists(path));
      db.close();
      check("close() is harmless and clears the error", db.lastErrorKind() == DBErrorKind::None);
   };

   // Asking again, many times, refuses the same way each time and changes nothing.
   s["threadsafe0_repeated_refusal_leaves_the_object_valid"] = [] {
      installCountingVfs();
      const std::string path = pathOf("again.sqlite3");
      const int opensBefore = g_opens;
      const sqlite3_int64 memoryBefore = sqlite3_memory_used();
      {
         SQLiteDatabase db(path);
         db.setLogsErrors(false);
         std::string first;
         for (int attempt = 1; attempt <= 5; ++attempt) {
            const std::string label = "attempt " + std::to_string(attempt) + ": ";
            check(label + "open() returns false", !db.open());
            check(label + "the kind is UnsupportedLibrary, not AlreadyOpen", db.lastErrorKind() == DBErrorKind::UnsupportedLibrary);
            if (attempt == 1) {
               first = snapshot(db);
            }
            check(label + "the snapshot is the same as the first", snapshot(db) == first);
            check(label + "still no connection", db.sqliteHandle() == nullptr);
            // other calls in between do not disturb it
            if (attempt == 2) {
               db.setBusyRetryTimeout(10);
               db.setShouldCacheStatements(false);
               db.close();
            }
         }
         check("nothing was opened in all of it", g_opens == opensBefore && !std::filesystem::exists(path));
      }
      check("the object was destroyed without trouble and SQLite holds nothing more", sqlite3_memory_used() == memoryBefore);

      // two objects, one path: both are refused, and neither is affected by the other
      SQLiteDatabase a(path);
      SQLiteDatabase b(path);
      a.setLogsErrors(false);
      b.setLogsErrors(false);
      check("two objects: the first is refused", !a.open());
      check("two objects: the second is refused", !b.open());
      check("two objects: both say UnsupportedLibrary", a.lastErrorKind() == DBErrorKind::UnsupportedLibrary &&
                                                        b.lastErrorKind() == DBErrorKind::UnsupportedLibrary);
      check("two objects: nothing was opened", g_opens == opensBefore && !std::filesystem::exists(path));
   };

   // One line per refusal when asked to log, none when not.
   s["threadsafe0_refusal_is_logged_once_and_silent_when_off"] = [] {
      installCountingVfs();
      const std::string path = pathOf("log.sqlite3");
      for (const bool logs : {true, false}) {
         SQLiteDatabase db(path);
         db.setLogsErrors(logs);
         std::string firstSnapshot;
         for (int attempt = 1; attempt <= 3; ++attempt) {
            const std::string label = std::string(logs ? "logging on" : "logging off") + ", attempt " + std::to_string(attempt) + ": ";
            bool opened = true;
            const std::string output = captureStdout([&] { opened = db.open(); });
            check(label + "refused", !opened);
            check(label + "recorded", db.lastErrorKind() == DBErrorKind::UnsupportedLibrary && db.lastErrorCode() == 0);
            if (attempt == 1 && logs) {
               firstSnapshot = snapshot(db);
            }
            if (!logs) {
               check(label + "nothing is written", output.empty());
               if (!output.empty()) {
                  evidence(label + "unexpected output was captured: " + output);
               }
               continue;
            }
            evidence(label + "output: " + output);
            check(label + "exactly one line", lines(output) == 1 && !output.empty() && output.back() == '\n');
            check(label + "it names the kind", output.find("DB Error [UnsupportedLibrary]: ") == 0);
            check(label + "it has no code= part (the code is 0)", !contains(output, " code="));
            check(label + "it holds the message", contains(output, db.lastErrorMessage()));
            check(label + "and the operation", contains(output, " in open: "));
            check(label + "and the database", contains(output, "(database: " + path + ")"));
            check(label + "the snapshot is the same on every attempt", snapshot(db) == firstSnapshot);
         }
      }

      // what is recorded and returned does not depend on logging
      SQLiteDatabase quiet(path);
      SQLiteDatabase loud(path);
      quiet.setLogsErrors(false);
      loud.setLogsErrors(true);
      bool quietOpened = true;
      bool loudOpened = true;
      captureStdout([&] { loudOpened = loud.open(); });
      quietOpened = quiet.open();
      check("logging does not change the return value", quietOpened == loudOpened);
      check("or the snapshot", snapshot(quiet) == snapshot(loud));
      check("and nothing was created either way", !std::filesystem::exists(path));
   };

   return s;
}

}


int main(int argc, char** argv) {
   // Windows: no dialogs, abort() fails fast and says so (nothing on other platforms)
   chapeau_harness::prepareProcess();

   // output must survive a crash or a kill
   std::setvbuf(stdout, nullptr, _IONBF, 0);

   if (argc < 2) {
      std::fprintf(stderr, "usage: chapeau_threadsafe0_probe <scenario> | list\n");
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

   chapeau_tests::ScopedTempDir dir("chapeau-threadsafe0");
   g_dir = dir.path().string();

   try {
      found->second();
   } catch (const chapeau_harness::CaptureError& e) {
      // a capture that could not be set up, read or restored fails the scenario; it is never taken as "nothing was printed"
      std::fprintf(stderr, "CAPTURE ERROR: %s\n", e.what());
      std::printf("CHECK stdout could be captured: FAIL (%s)\n", e.what());
      ++g_failures;
   }

   std::printf("SCENARIO %s finished, %d failed check(s)\n", argv[1], g_failures);
   return g_failures == 0 ? 0 : 1;
}
