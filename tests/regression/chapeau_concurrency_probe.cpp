// Concurrency scenarios for the subprocess regression harness (H1a-12a). One
// scenario per process:
//
//   chapeau_concurrency_probe <scenario>   run it; exit 0 if every CHECK passed,
//                                          1 if one failed, 2 for a usage error
//   chapeau_concurrency_probe list         print the scenario names
//
// These scenarios exercise the patterns that Chapeau supports (see the comment
// of Database in Database.h) and nothing else:
//
//   * a connection, its cursors and its statements form one unit, used by one
//     thread at a time;
//   * different connections are used by different threads at the same time;
//   * a unit moves to another thread while it is idle, with synchronization
//     the application provides (here: a mutex and condition variable, or a
//     join).
//
// What is deliberately NOT here: any scenario in which two threads use one unit
// at the same time. That is not supported, so it is not tested as if it were.
// Under ThreadSanitizer these scenarios must run without a report; if one
// does, either the test broke the contract or Chapeau has a defect in a
// supported pattern, and the second is to be reported, not hidden.
//
// Rules for the scenarios themselves:
//   * worker threads never call check(): they record into a Report, which the
//     main thread prints after joining them (the join is what makes their
//     records visible to it);
//   * every wait has a limit, and a thread that does not finish ends the
//     process with a failed check instead of hanging it;
//   * Occupancy proves from inside the test that two threads were never in a
//     unit together (TSan proves the happens-before edges separately).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "sqlite3.h"

#include "scoped_temp_dir.h"
#include "SQLiteDatabase.h"
#include "SQLiteResultSet.h"
#include "DBStatementArgs.h"
#include "DBLong.h"

using namespace chapeau;

namespace {

int g_failures = 0;
std::string g_dir;

// the longest the scenarios wait for a thread or for a hand-off. Generous: it
// is a backstop against a hang, never the thing a scenario measures.
const int WAIT_LIMIT_MS = 60000;

void check(const std::string& label, bool ok) {
   if (!ok) {
      ++g_failures;
   }
   std::printf("CHECK %s: %s\n", label.c_str(), ok ? "PASS" : "FAIL");
}

void checkLong(const std::string& label, long long expected, long long actual) {
   const bool ok = (expected == actual);
   if (!ok) {
      ++g_failures;
   }
   std::printf("CHECK %s: %s (expected %lld, got %lld)\n",
               label.c_str(), ok ? "PASS" : "FAIL", expected, actual);
}

void evidence(const std::string& line) {
   std::printf("EVIDENCE %s\n", line.c_str());
}

long long nowMs() {
   return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string pathOf(const std::string& name) {
   return (std::filesystem::path(g_dir) / name).string();
}

std::string idOf(std::thread::id id) {
   return std::to_string(std::hash<std::thread::id>()(id) % 100000);
}

// ---- recording from worker threads ---------------------------------------------

// What one thread observed. Written by that thread only, read by the main
// thread after the join.
class Report {
public:
   void expect(const std::string& what, bool ok) {
      m_results.emplace_back(what, ok);
   }

   void expectLong(const std::string& what, long long expected, long long actual) {
      m_results.emplace_back(what + " (expected " + std::to_string(expected) +
                             ", got " + std::to_string(actual) + ")",
                             expected == actual);
   }

   void print(const std::string& prefix) const {
      for (const auto& result : m_results) {
         check(prefix + result.first, result.second);
      }
   }

   bool allPassed() const {
      for (const auto& result : m_results) {
         if (!result.second) {
            return false;
         }
      }
      return true;
   }

   size_t size() const {
      return m_results.size();
   }

private:
   std::vector<std::pair<std::string, bool>> m_results;
};

// ---- threads and synchronization ---------------------------------------------

// Waits on cv, for pred, for at most limitMs. The deadline is on the system
// clock on purpose: condition_variable::wait_for() counts on the steady clock,
// which libstdc++ turns into pthread_cond_clockwait(), and the ThreadSanitizer
// of GCC 11 does not know that call, so it loses track of the mutex and
// reports a "double lock" that is not there. wait_until() on the system clock
// ends in pthread_cond_timedwait(), which every ThreadSanitizer knows. The
// limit is only a backstop against a hang, so a jump of the wall clock does
// not matter.
template <class Predicate>
bool waitFor(std::condition_variable& cv, std::unique_lock<std::mutex>& lock, int limitMs, Predicate pred) {
   return cv.wait_until(lock, std::chrono::system_clock::now() + std::chrono::milliseconds(limitMs), pred);
}

// Starts threads and waits for all of them within a limit. A thread that does
// not finish cannot be joined, so then the process ends at once with a failure:
// a hang must be a visible failure and must not need the harness to kill it.
class Crew {
public:
   ~Crew() {
      for (auto& thread : m_threads) {
         if (thread.joinable()) {
            thread.join();
         }
      }
   }

   void start(std::function<void()> body) {
      {
         std::lock_guard<std::mutex> lock(m_mutex);
         ++m_running;
      }
      m_threads.emplace_back([this, body] {
         body();
         std::lock_guard<std::mutex> lock(m_mutex);
         --m_running;
         m_cv.notify_all();
      });
   }

   void joinAll(const std::string& what, int limitMs = WAIT_LIMIT_MS) {
      bool finished = false;
      {
         std::unique_lock<std::mutex> lock(m_mutex);
         finished = waitFor(m_cv, lock, limitMs,
                                  [this] { return m_running == 0; });
      }
      if (!finished) {
         std::printf("CHECK %s: the threads finish within %d ms: FAIL\n", what.c_str(), limitMs);
         std::printf("SCENARIO aborted: a thread did not finish\n");
         std::fflush(stdout);
         std::_Exit(1);
      }
      for (auto& thread : m_threads) {
         thread.join();
      }
      m_threads.clear();
      check(what + ": the threads finish within " + std::to_string(limitMs) + " ms", true);
   }

private:
   std::mutex m_mutex;
   std::condition_variable m_cv;
   int m_running = 0;
   std::vector<std::thread> m_threads;
};

// One-shot event: set() once, wait() until then (or the limit).
class Event {
public:
   void set() {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_set = true;
      m_cv.notify_all();
   }

   bool wait(int limitMs = WAIT_LIMIT_MS) {
      std::unique_lock<std::mutex> lock(m_mutex);
      return waitFor(m_cv, lock, limitMs, [this] { return m_set; });
   }

private:
   std::mutex m_mutex;
   std::condition_variable m_cv;
   bool m_set = false;
};

// All threads arrive, then all are released together.
class Rendezvous {
public:
   explicit Rendezvous(int parties) : m_parties(parties) {}

   bool arriveAndWait(int limitMs = WAIT_LIMIT_MS) {
      std::unique_lock<std::mutex> lock(m_mutex);
      if (++m_arrived >= m_parties) {
         m_cv.notify_all();
      }
      return waitFor(m_cv, lock, limitMs,
                           [this] { return m_arrived >= m_parties; });
   }

private:
   std::mutex m_mutex;
   std::condition_variable m_cv;
   int m_parties;
   int m_arrived = 0;
};

// A unit: the database and the cursors that came from it. It is only ever
// moved whole.
struct Unit {
   std::unique_ptr<SQLiteDatabase> db;
   std::vector<std::unique_ptr<DBResultSet>> cursors;
};

// Hands a unit from one thread to another. This mutex and condition variable
// are the whole synchronization of the hand-off: the giver does not touch the
// unit after give(), the taker does not touch it before take() returns.
class Baton {
public:
   void give(Unit&& unit) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_unit = std::move(unit);
      m_full = true;
      m_cv.notify_all();
   }

   bool take(Unit& into, int limitMs = WAIT_LIMIT_MS) {
      std::unique_lock<std::mutex> lock(m_mutex);
      if (!waitFor(m_cv, lock, limitMs, [this] { return m_full; })) {
         return false;
      }
      into = std::move(m_unit);
      m_full = false;
      return true;
   }

private:
   std::mutex m_mutex;
   std::condition_variable m_cv;
   Unit m_unit;
   bool m_full = false;
};

// Proof, from inside the test, that no two threads were in a unit together and
// that the unit was used by the threads in the intended order. Every stretch in
// which a thread uses the unit is wrapped in a Use.
class Occupancy {
public:
   class Use {
   public:
      Use(Occupancy& occupancy, const char* who) : m_occupancy(occupancy) {
         if (m_occupancy.m_inside.fetch_add(1) != 0) {
            m_occupancy.m_overlaps.fetch_add(1);
         }
         std::lock_guard<std::mutex> lock(m_occupancy.m_mutex);
         m_occupancy.m_order += who;
      }

      ~Use() {
         m_occupancy.m_inside.fetch_sub(1);
      }

      Use(const Use&) = delete;
      Use& operator=(const Use&) = delete;

   private:
      Occupancy& m_occupancy;
   };

   int overlaps() const {
      return m_overlaps.load();
   }

   std::string order() const {
      std::lock_guard<std::mutex> lock(m_mutex);
      return m_order;
   }

private:
   std::atomic<int> m_inside{0};
   std::atomic<int> m_overlaps{0};
   mutable std::mutex m_mutex;
   std::string m_order;
};

// ---- small helpers on a unit (call only from the thread that has it) ---------

bool snapshotClear(const SQLiteDatabase& db) {
   return db.lastErrorKind() == DBErrorKind::None && db.lastErrorCode() == 0 &&
          db.lastErrorMessage().empty();
}

bool exec(SQLiteDatabase& db, const std::string& sql) {
   unsigned long rows = 0;
   return db.executeUpdate(sql, rows);
}

long long scalar(SQLiteDatabase& db, const std::string& sql) {
   std::unique_ptr<DBResultSet> rs(db.executeQuery(sql));
   return (rs && rs->next()) ? rs->longForColumnIndex(0) : -999999;
}

std::string text(DBResultSet& rs, int column) {
   std::string value;
   rs.stringForColumnIndex(column, value);
   return value;
}

// the statements SQLite still holds for this connection, and how many of them
// are mid-execution (and so hold a lock)
struct Statements {
   int total = 0;
   int busy = 0;
};

Statements statementsOf(SQLiteDatabase& db) {
   Statements result;
   sqlite3* handle = db.sqliteHandle();
   for (sqlite3_stmt* s = sqlite3_next_stmt(handle, nullptr); s != nullptr;
        s = sqlite3_next_stmt(handle, s)) {
      ++result.total;
      if (sqlite3_stmt_busy(s)) {
         ++result.busy;
      }
   }
   return result;
}

// t(id, v) with the rows 1..count, one statement so only one is cached
void fillTable(SQLiteDatabase& db, Report& report, int count) {
   report.expect("table t is created", exec(db, "create table if not exists t(id integer primary key, v text)"));
   std::string sql = "insert into t(id, v) values ";
   for (int i = 1; i <= count; ++i) {
      sql += std::string(i > 1 ? "," : "") + "(" + std::to_string(i) + ",'row" + std::to_string(i) + "')";
   }
   report.expect("rows are inserted", exec(db, sql));
}

// the rows of t as seen through a connection of its own
std::vector<long long> idsInFile(const std::string& path) {
   std::vector<long long> ids;
   SQLiteDatabase db(path);
   if (db.open()) {
      std::unique_ptr<DBResultSet> rs(db.executeQuery("select id from t order by id"));
      while (rs && rs->next()) {
         ids.push_back(rs->longForColumnIndex(0));
      }
   }
   return ids;
}

bool threadSafeSqlite() {
   const bool ok = sqlite3_threadsafe() != 0;
   if (!ok) {
      check("SQLite is built thread safe (sqlite3_threadsafe() is not 0); the threaded scenarios need it", false);
   }
   return ok;
}

// ---- the scenarios ------------------------------------------------------------

using Scenario = std::function<void()>;

std::map<std::string, Scenario> scenarios() {
   std::map<std::string, Scenario> s;

   // How the SQLite library behind Chapeau is built, as to threads. Recorded, and
   // required to be a thread-safe build: every other scenario here uses threads.
   // Chapeau's open() does not look at this yet (H1a-12b).
   s["concurrency_sqlite_threading_configuration"] = [] {
      const int safe = sqlite3_threadsafe();
      evidence(std::string("sqlite3_libversion()=") + sqlite3_libversion() + " header SQLITE_VERSION=" + SQLITE_VERSION +
               " SQLiteDatabase::sqliteLibVersion()=" + SQLiteDatabase::sqliteLibVersion());
      evidence("sqlite3_threadsafe()=" + std::to_string(safe) + " (0 single-thread, 1 serialized, 2 multi-thread)");

      int option = -1;
      for (int i = 0;; ++i) {
         const char* name = sqlite3_compileoption_get(i);
         if (name == nullptr) {
            break;
         }
         if (!std::strncmp(name, "THREADSAFE", 10) || !std::strncmp(name, "MUTEX", 5)) {
            evidence(std::string("compile option ") + name);
         }
         if (!std::strncmp(name, "THREADSAFE=", 11)) {
            option = std::atoi(name + 11);
         }
      }

      check("sqlite3_threadsafe() is not 0: the threaded patterns need a thread-safe SQLite", safe != 0);
      if (option >= 0) {
         checkLong("the compile option THREADSAFE agrees with sqlite3_threadsafe()", option, safe);
      }

      // today a connection opens whatever the answer is; this records that
      SQLiteDatabase db(pathOf("config.sqlite3"));
      check("a Chapeau connection opens on this build", db.open());
      check("and answers a query", scalar(db, "select 41 + 1") == 42);
      db.close();
   };

   // Supported pattern A: several threads, each with a connection of its own, one
   // database file. The file locks and the busy timeout coordinate them. Every
   // insert that is reported as done is in the file, nothing else is.
   s["concurrency_separate_connections_same_file"] = [] {
      if (!threadSafeSqlite()) {
         return;
      }
      const int THREADS = 4;
      const int PER_THREAD = 40;
      const int BUSY_MS = 10000;
      const std::string path = pathOf("separate.sqlite3");

      {
         SQLiteDatabase setup(path);
         check("setup connection opens", setup.open());
         check("setup creates the table",
               exec(setup, "create table w(id integer primary key, owner integer not null, "
                           "seq integer not null, unique(owner, seq))"));
      }

      struct Worker {
         Report report;
         std::vector<long long> stored;   // seq of every insert reported as done
         int failed = 0;
         int rowsAffectedWrong = 0;
         long long worstCallMs = 0;
         std::string firstFailure;
         bool failuresAllBusy = true;
         std::vector<long long> seenThroughOwnCursor;
         std::string threadId;
      };
      std::vector<Worker> workers(THREADS);

      Rendezvous together(THREADS);
      Crew crew;
      const long long startedAt = nowMs();
      for (int i = 0; i < THREADS; ++i) {
         crew.start([&, i] {
            Worker& w = workers[i];
            w.threadId = idOf(std::this_thread::get_id());
            // this thread's own connection: created, used and destroyed here, shared with nobody
            SQLiteDatabase db(path);
            db.setBusyRetryTimeout(BUSY_MS);
            w.report.expect("the connection opens", db.open());
            w.report.expect("synchronous off (speed only)", exec(db, "pragma synchronous = off"));
            together.arriveAndWait();

            for (int seq = 1; seq <= PER_THREAD; ++seq) {
               DBStatementArgs args;
               args.add(new DBLong(i));
               args.add(new DBLong(seq));
               unsigned long rows = 0;
               const long long t0 = nowMs();
               const bool ok = db.executeUpdate("insert into w(owner, seq) values (?, ?)", args, rows);
               w.worstCallMs = std::max(w.worstCallMs, nowMs() - t0);
               if (ok) {
                  w.stored.push_back(seq);
                  if (rows != 1) {
                     ++w.rowsAffectedWrong;
                  }
               } else {
                  ++w.failed;
                  if (!(db.lastErrorKind() == DBErrorKind::Sqlite && db.lastErrorCode() == SQLITE_BUSY)) {
                     w.failuresAllBusy = false;
                  }
                  if (w.firstFailure.empty()) {
                     w.firstFailure = std::string(dbErrorKindName(db.lastErrorKind())) + "/" +
                                      std::to_string(db.lastErrorCode()) + " " + db.lastErrorMessage();
                  }
               }
            }

            // read its own rows back through a cursor of the same connection
            DBStatementArgs owner;
            owner.add(new DBLong(i));
            std::unique_ptr<DBResultSet> rs(db.executeQuery("select seq from w where owner = ? order by seq", owner));
            w.report.expect("the thread's own cursor opens", rs != nullptr);
            while (rs && rs->next()) {
               w.seenThroughOwnCursor.push_back(rs->longForColumnIndex(0));
            }
            rs.reset();
            db.close();
         });
      }
      crew.joinAll("separate connections");
      const long long elapsed = nowMs() - startedAt;

      long long reported = 0;
      std::vector<std::pair<long long, long long>> expected;   // (owner, seq) reported as stored
      std::set<std::string> ids;
      for (int i = 0; i < THREADS; ++i) {
         Worker& w = workers[i];
         const std::string who = "thread " + std::to_string(i) + ": ";
         w.report.print(who);
         checkLong(who + "every insert reported as done", PER_THREAD, (long long) w.stored.size());
         checkLong(who + "no insert failed (" + (w.firstFailure.empty() ? "none" : w.firstFailure) + ")", 0, w.failed);
         check(who + "a failure, if any, is the busy error", w.failuresAllBusy);
         checkLong(who + "each insert changed exactly one row", 0, w.rowsAffectedWrong);
         check(who + "no single call waited as long as the busy timeout (" + std::to_string(w.worstCallMs) + " ms worst)",
               w.worstCallMs < BUSY_MS);
         check(who + "its own cursor saw exactly the rows it inserted", w.seenThroughOwnCursor == w.stored);
         reported += (long long) w.stored.size();
         for (long long seq : w.stored) {
            expected.emplace_back(i, seq);
         }
         ids.insert(w.threadId);
      }
      evidence("threads=" + std::to_string(THREADS) + " distinct thread ids=" + std::to_string(ids.size()) +
               " inserts per thread=" + std::to_string(PER_THREAD) + " busy timeout=" + std::to_string(BUSY_MS) +
               " ms elapsed=" + std::to_string(elapsed) + " ms");
      check("each worker ran on its own thread", (int) ids.size() == THREADS);
      check("the whole run finished within the limit", elapsed < WAIT_LIMIT_MS);

      // what the file holds, through a connection that none of the workers used
      SQLiteDatabase reader(path);
      check("the reader opens", reader.open());
      checkLong("rows in the file equal the inserts reported as done", reported, scalar(reader, "select count(*) from w"));
      std::vector<std::pair<long long, long long>> inFile;
      std::unique_ptr<DBResultSet> rs(reader.executeQuery("select owner, seq from w order by owner, seq"));
      while (rs && rs->next()) {
         inFile.emplace_back(rs->longForColumnIndex(0), rs->longForColumnIndex(1));
      }
      check("and they are exactly the ones reported, none missing, none extra", inFile == expected);
      check("the reader saw no error", snapshotClear(reader));
   };

   // Supported pattern B: a connection and a live cursor are created on thread A,
   // handed to thread B through a mutex and condition variable, and B carries on
   // where A stopped: the cursor, more operations, the error snapshot, close.
   s["concurrency_handoff_cursor_continues"] = [] {
      if (!threadSafeSqlite()) {
         return;
      }
      for (const bool caching : {true, false}) {
         const std::string round = std::string(caching ? "cached" : "uncached") + ": ";
         const std::string path = pathOf(caching ? "handoff-cached.sqlite3" : "handoff-uncached.sqlite3");

         Report a;
         Report b;
         Occupancy occupancy;
         Baton baton;
         Event bFinished;
         std::string idA;
         std::string idB;
         int rowsAfterHandoff = 0;
         Crew crew;

         crew.start([&] {   // thread A: creates the unit, uses it, gives it away, never touches it again
            idA = idOf(std::this_thread::get_id());
            Unit unit;
            {
               Occupancy::Use use(occupancy, "A");
               unit.db = std::make_unique<SQLiteDatabase>(path);
               unit.db->setShouldCacheStatements(caching);
               a.expect("A: the connection opens", unit.db->open());
               fillTable(*unit.db, a, 10);
               unit.cursors.emplace_back(unit.db->executeQuery("select id, v from t order by id"));
               DBResultSet* rs = unit.cursors[0].get();
               a.expect("A: the cursor exists", rs != nullptr);
               for (int id = 1; rs != nullptr && id <= 3; ++id) {
                  a.expect("A: row " + std::to_string(id) + " is delivered", rs->next() && rs->longForColumnIndex(0) == id);
               }
               a.expect("A: the cursor is on a row", rs != nullptr && rs->status() == DBResultSetStatus::Row);
               a.expect("A: no error so far", snapshotClear(*unit.db));
            }
            baton.give(std::move(unit));   // from here on A does not touch the unit
            a.expect("A: B finished", bFinished.wait());
         });

         crew.start([&] {   // thread B: takes the unit and carries on
            idB = idOf(std::this_thread::get_id());
            Unit unit;
            if (!baton.take(unit)) {
               b.expect("B: the unit arrived", false);
               bFinished.set();
               return;
            }
            {
               Occupancy::Use use(occupancy, "B");
               SQLiteDatabase& db = *unit.db;
               b.expect("B: the unit arrived with its cursor", unit.cursors.size() == 1 && unit.cursors[0] != nullptr);
               if (unit.cursors.size() != 1 || unit.cursors[0] == nullptr) {
                  bFinished.set();
                  return;
               }
               DBResultSet& rs = *unit.cursors[0];

               b.expect("B: the error snapshot is clear on arrival", snapshotClear(db));
               b.expect("B: the cursor is still on its row (status Row)", rs.status() == DBResultSetStatus::Row);
               b.expectLong("B: the cursor is still on row 3", 3, rs.longForColumnIndex(0));
               b.expect("B: and its text is intact", text(rs, 1) == "row3");

               for (int id = 4; id <= 10; ++id) {
                  const bool more = rs.next();
                  b.expect("B: row " + std::to_string(id) + " is delivered (not ended early)", more);
                  if (!more) {
                     break;
                  }
                  ++rowsAfterHandoff;
                  b.expectLong("B: row " + std::to_string(id) + " has the right id", id, rs.longForColumnIndex(0));
                  b.expect("B: row " + std::to_string(id) + " has the right text", text(rs, 1) == "row" + std::to_string(id));
                  b.expect("B: status is Row, not Done or Invalidated", rs.status() == DBResultSetStatus::Row);
               }
               b.expect("B: after the last row next() is false", !rs.next());
               b.expect("B: the cursor is Done (not Invalidated, not Error)", rs.status() == DBResultSetStatus::Done);
               b.expect("B: it stays Done", !rs.next() && rs.status() == DBResultSetStatus::Done);
               b.expect("B: no error from finishing the cursor", snapshotClear(db) && rs.lastErrorCode() == 0);

               // more work on the moved connection
               b.expect("B: an insert works", exec(db, "insert into t(id, v) values (11, 'row11')"));
               b.expectLong("B: the new row is counted", 11, scalar(db, "select count(*) from t"));
               b.expect("B: a transaction works", db.beginTransaction() && db.inTransaction() &&
                                                  exec(db, "update t set v = 'changed' where id = 1") &&
                                                  db.commit() && !db.inTransaction());
               std::unique_ptr<DBResultSet> second(db.executeQuery("select id from t order by id desc"));
               b.expect("B: a new cursor opens", second != nullptr && second->next() && second->longForColumnIndex(0) == 11);
               second.reset();
               b.expect("B: the error snapshot is still clear", snapshotClear(db));
               b.expectLong("B: no statement is left mid-execution", 0, statementsOf(db).busy);

               unit.cursors.clear();   // the cursor is destroyed on B, which did not create it
               db.close();
               b.expect("B: after close() the connection is gone", !db.goodConnection());
               std::unique_ptr<DBResultSet> afterClose(db.executeQuery("select 1"));
               b.expect("B: a query after close() is refused (NoConnection)",
                        afterClose == nullptr && db.lastErrorKind() == DBErrorKind::NoConnection);
               unit.db.reset();
            }
            bFinished.set();
         });
         crew.joinAll(round + "hand-off");

         a.print(round);
         b.print(round);
         checkLong(round + "rows delivered by B after the hand-off", 7, rowsAfterHandoff);
         checkLong(round + "times two threads were in the unit together", 0, occupancy.overlaps());
         check(round + "the unit was used by A and then by B (" + occupancy.order() + ")", occupancy.order() == "AB");
         evidence(round + "thread A=" + idA + " thread B=" + idB + " order=" + occupancy.order() +
                  " overlaps=" + std::to_string(occupancy.overlaps()));

         const std::vector<long long> ids = idsInFile(path);
         checkLong(round + "the file holds the 11 rows", 11, (long long) ids.size());
      }
   };

   // The error snapshot and an open transaction belong to the unit, so they move
   // with it (rules 7 and 8): a failure on A is read on B, and B ends the
   // transaction that A began. The opener hands over by exiting (a join).
   s["concurrency_handoff_keeps_error_snapshot_and_transaction"] = [] {
      if (!threadSafeSqlite()) {
         return;
      }
      const std::string path = pathOf("snapshot.sqlite3");
      Report a;
      Report b;
      Unit unit;
      Crew crew;

      std::string failure;
      crew.start([&] {   // A
         unit.db = std::make_unique<SQLiteDatabase>(path);
         a.expect("A: opens", unit.db->open());
         a.expect("A: creates the table", exec(*unit.db, "create table t(id integer primary key, v text)"));
         a.expect("A: first insert", exec(*unit.db, "insert into t(id, v) values (1, 'one')"));
         a.expect("A: a duplicate key is refused", !exec(*unit.db, "insert into t(id, v) values (1, 'again')"));
         a.expect("A: the failure is recorded as a SQLite constraint error",
                  unit.db->lastErrorKind() == DBErrorKind::Sqlite && unit.db->lastErrorCode() == SQLITE_CONSTRAINT);
         a.expect("A: the message names the constraint",
                  unit.db->lastErrorMessage().find("UNIQUE constraint failed: t.id") != std::string::npos);
         // beginTransaction() clears the snapshot, so the failure that B must find is the one below,
         // inside the transaction
         a.expect("A: a transaction begins", unit.db->beginTransaction());
         a.expect("A: an insert in the transaction", exec(*unit.db, "insert into t(id, v) values (2, 'two')"));
         a.expect("A: a second duplicate key is refused (the snapshot is again a failure)",
                  !exec(*unit.db, "insert into t(id, v) values (2, 'dup')"));
         a.expect("A: still in the transaction after the failed statement", unit.db->inTransaction());
         failure = unit.db->lastErrorMessage();
      });
      crew.joinAll("A (the opener) exits");   // the join hands the unit over

      std::string idB;
      crew.start([&] {   // B
         idB = idOf(std::this_thread::get_id());
         SQLiteDatabase& db = *unit.db;
         b.expect("B: the snapshot kind came with the unit", db.lastErrorKind() == DBErrorKind::Sqlite);
         b.expectLong("B: and its code", SQLITE_CONSTRAINT, db.lastErrorCode());
         b.expect("B: and its message, unchanged", db.lastErrorMessage() == failure);
         b.expect("B: the transaction A began is open on the moved connection", db.inTransaction());

         // beginTransaction() is BEGIN EXCLUSIVE: the connection that holds it keeps every other
         // connection out, so that this is observable from outside as 'the transaction is still open'
         {
            SQLiteDatabase other(path);
            other.setBusyRetryTimeout(0);
            b.expect("B: an independent connection opens", other.open());
            b.expectLong("B: it is kept out while the moved transaction is open", -999999,
                         scalar(other, "select count(*) from t"));
            b.expect("B: with the busy error",
                     other.lastErrorKind() == DBErrorKind::Sqlite && other.lastErrorCode() == SQLITE_BUSY);
         }

         b.expectLong("B: a query on the moved connection works", 2, scalar(db, "select count(*) from t"));
         b.expect("B: and a success clears the snapshot", snapshotClear(db));
         b.expect("B: B can commit A's transaction", db.commit());
         b.expect("B: no transaction is left", !db.inTransaction());
         b.expect("B: the snapshot is clear after the commit", snapshotClear(db));

         {
            SQLiteDatabase other(path);
            b.expect("B: an independent connection opens again", other.open());
            b.expectLong("B: it now sees both rows", 2, scalar(other, "select count(*) from t"));
         }
         db.close();
         unit.db.reset();
      });
      crew.joinAll("B");

      a.print("A: ");
      b.print("B: ");
      evidence("thread B=" + idB + " (A had exited before B started)");
      const std::vector<long long> ids = idsInFile(path);
      check("the file holds rows 1 and 2", ids == std::vector<long long>({1, 2}));
   };

   // Supported pattern C: the thread that opened the connection exits, and the
   // connection is still usable. Nothing depends on the opening thread staying
   // alive, or on its std::thread::id staying unique: thread ids can be reused
   // once a thread has exited, and a unit that passes through many short-lived
   // threads in turn must work in every one of them.
   s["concurrency_opener_thread_exits"] = [] {
      if (!threadSafeSqlite()) {
         return;
      }

      // one hand-off by exit: A opens and fills, then ends; B continues
      {
         const std::string path = pathOf("opener.sqlite3");
         Report a;
         Report b;
         Unit unit;
         std::string idA;
         std::string idB;
         Crew crew;
         crew.start([&] {
            idA = idOf(std::this_thread::get_id());
            unit.db = std::make_unique<SQLiteDatabase>(path);
            a.expect("A: opens", unit.db->open());
            fillTable(*unit.db, a, 10);
            unit.cursors.emplace_back(unit.db->executeQuery("select id from t order by id"));
            for (int id = 1; id <= 4; ++id) {
               a.expect("A: row " + std::to_string(id), unit.cursors[0] && unit.cursors[0]->next() &&
                                                         unit.cursors[0]->longForColumnIndex(0) == id);
            }
         });
         crew.joinAll("the opener thread");
         check("the opener thread has ended; its unit is still here", unit.db != nullptr && unit.cursors.size() == 1);

         crew.start([&] {
            idB = idOf(std::this_thread::get_id());
            DBResultSet& rs = *unit.cursors[0];
            b.expect("B: the cursor is on row 4", rs.status() == DBResultSetStatus::Row && rs.longForColumnIndex(0) == 4);
            int rows = 0;
            while (rs.next()) {
               ++rows;
               b.expectLong("B: the next row is in order", 4 + rows, rs.longForColumnIndex(0));
            }
            b.expectLong("B: rows after the opener ended", 6, rows);
            b.expect("B: the cursor ended as Done", rs.status() == DBResultSetStatus::Done);
            b.expect("B: an insert works", exec(*unit.db, "insert into t(id, v) values (11, 'row11')"));
            b.expectLong("B: and a count", 11, scalar(*unit.db, "select count(*) from t"));
            b.expect("B: no error", snapshotClear(*unit.db));
            unit.cursors.clear();
            unit.db->close();
            unit.db.reset();
         });
         crew.joinAll("the second thread");
         a.print("opener A: ");
         b.print("opener B: ");
         evidence("opener thread id=" + idA + ", continuing thread id=" + idB + (idA == idB ? " (the same id was reused)" : " (different ids)"));
         check("the file holds the 11 rows", idsInFile(path).size() == 11);
      }

      // one unit passes through many threads in turn, each created after the last has ended
      {
         const int GENERATIONS = 24;
         const std::string path = pathOf("generations.sqlite3");
         Report setup;
         Unit unit;
         {
            Crew crew;
            crew.start([&] {
               unit.db = std::make_unique<SQLiteDatabase>(path);
               setup.expect("generation 0: opens", unit.db->open());
               setup.expect("generation 0: creates tables",
                            exec(*unit.db, "create table seq(n integer primary key)") &&
                            exec(*unit.db, "create table log(n integer primary key, gen integer)"));
               std::string sql = "insert into seq(n) values ";
               for (int i = 1; i <= GENERATIONS; ++i) {
                  sql += std::string(i > 1 ? "," : "") + "(" + std::to_string(i) + ")";
               }
               setup.expect("generation 0: fills the sequence", exec(*unit.db, sql));
               unit.cursors.emplace_back(unit.db->executeQuery("select n from seq order by n"));
               setup.expect("generation 0: opens the cursor", unit.cursors[0] != nullptr);
            });
            crew.joinAll("generation 0");
         }
         setup.print("");

         std::vector<std::string> ids;
         std::vector<Report> reports(GENERATIONS);
         for (int g = 1; g <= GENERATIONS; ++g) {
            Crew crew;
            crew.start([&, g] {
               Report& r = reports[g - 1];
               ids.push_back(idOf(std::this_thread::get_id()));
               DBResultSet& rs = *unit.cursors[0];
               r.expect("the cursor delivers the next row", rs.next());
               r.expectLong("and it is the one expected", g, rs.longForColumnIndex(0));
               r.expect("the cursor is on a row, not ended", rs.status() == DBResultSetStatus::Row);
               DBStatementArgs args;
               args.add(new DBLong(g));
               args.add(new DBLong(g));
               unsigned long rows = 0;
               r.expect("an insert works", unit.db->executeUpdate("insert into log(n, gen) values (?, ?)", args, rows) && rows == 1);
               r.expect("the snapshot is clear", snapshotClear(*unit.db));
            });
            crew.joinAll("generation " + std::to_string(g));
         }
         bool allPassed = true;
         for (size_t g = 0; g < reports.size(); ++g) {
            if (!reports[g].allPassed()) {
               allPassed = false;
               reports[g].print("generation " + std::to_string(g + 1) + ": ");
            }
         }
         check("every generation's thread found the unit working (" + std::to_string(GENERATIONS * 5) + " checks)", allPassed);

         Report last;
         {
            Crew crew;
            crew.start([&] {
               ids.push_back(idOf(std::this_thread::get_id()));
               DBResultSet& rs = *unit.cursors[0];
               last.expect("the cursor is exhausted after the last generation", !rs.next());
               last.expect("and Done, not Invalidated", rs.status() == DBResultSetStatus::Done);
               last.expectLong("the log holds one row per generation", GENERATIONS, scalar(*unit.db, "select count(*) from log"));
               unit.cursors.clear();
               unit.db->close();
               unit.db.reset();
            });
            crew.joinAll("the last thread");
         }
         last.print("");

         std::set<std::string> distinct(ids.begin(), ids.end());
         // informational: thread ids are reused on this platform or not; the checks above hold either way
         evidence("threads that used the unit=" + std::to_string(ids.size()) + " distinct std::thread::id values=" +
                  std::to_string(distinct.size()) + (distinct.size() < ids.size() ? " (ids were reused)" : " (no id was reused)"));
      }
   };

   // Supported pattern D: cursors created on one thread are destroyed on another,
   // after a synchronized hand-off. Observed from outside: while a cursor is
   // alive it holds a read lock, so a writer on another connection is refused;
   // when it is destroyed the statement is reset or finalized and the writer is
   // let in.
   s["concurrency_cursor_destroyed_after_handoff"] = [] {
      if (!threadSafeSqlite()) {
         return;
      }
      for (const bool caching : {true, false}) {
         const std::string round = std::string(caching ? "cached" : "uncached") + ": ";
         const std::string path = pathOf(caching ? "destroy-cached.sqlite3" : "destroy-uncached.sqlite3");
         const std::string selectIds = "select id from t order by id";

         Report a;
         Report b;
         Occupancy occupancy;
         Baton baton;
         Crew crew;
         int totalBefore = -1;
         int totalAfterLiveCursors = -1;

         crew.start([&] {   // A
            Unit unit;
            {
               Occupancy::Use use(occupancy, "A");
               unit.db = std::make_unique<SQLiteDatabase>(path);
               unit.db->setShouldCacheStatements(caching);
               a.expect("A: opens", unit.db->open());
               fillTable(*unit.db, a, 10);
               a.expect("A: side table", exec(*unit.db, "create table side(x integer)"));
               totalBefore = statementsOf(*unit.db).total;

               unit.cursors.emplace_back(unit.db->executeQuery(selectIds));          // 0: live, mid-iteration
               unit.cursors.emplace_back(unit.db->executeQuery("select count(*) from t"));   // 1: run to the end
               unit.cursors.emplace_back(unit.db->executeQuery(selectIds));          // 2: live, a separate statement
               a.expect("A: three cursors exist", unit.cursors[0] && unit.cursors[1] && unit.cursors[2]);
               a.expect("A: cursor 0 on its first row", unit.cursors[0]->next() && unit.cursors[0]->longForColumnIndex(0) == 1);
               a.expect("A: cursor 1 delivers its row and ends", unit.cursors[1]->next() && !unit.cursors[1]->next() &&
                                                                  unit.cursors[1]->status() == DBResultSetStatus::Done);
               a.expect("A: cursor 2 on its first row", unit.cursors[2]->next() && unit.cursors[2]->longForColumnIndex(0) == 1);
               totalAfterLiveCursors = statementsOf(*unit.db).total;
               a.expectLong("A: two statements are mid-execution", 2, statementsOf(*unit.db).busy);
            }
            baton.give(std::move(unit));
         });

         crew.start([&] {   // B
            Unit unit;
            if (!baton.take(unit)) {
               b.expect("B: the unit arrived", false);
               return;
            }
            Occupancy::Use use(occupancy, "B");
            SQLiteDatabase& db = *unit.db;
            SQLiteDatabase writer(path);   // a unit of its own, on this thread
            writer.setBusyRetryTimeout(0);
            b.expect("B: the independent writer opens", writer.open());

            b.expectLong("B: the statements are still mid-execution after the move", 2, statementsOf(db).busy);
            b.expect("B: a writer elsewhere is refused while the cursors are alive (they hold the read lock)",
                     !exec(writer, "insert into side(x) values (1)") && writer.lastErrorKind() == DBErrorKind::Sqlite &&
                     writer.lastErrorCode() == SQLITE_BUSY);

            unit.cursors[0].reset();   // destroyed on B
            b.expectLong("B: after destroying cursor 0 one statement is mid-execution", 1, statementsOf(db).busy);
            b.expect("B: the writer is still refused (cursor 2 holds the lock)", !exec(writer, "insert into side(x) values (2)"));

            unit.cursors[2].reset();
            const Statements after = statementsOf(db);
            b.expectLong("B: after destroying cursor 2 nothing is mid-execution", 0, after.busy);
            b.expect("B: the statement of an uncached cursor is finalized, a cached one only reset",
                     after.total == totalAfterLiveCursors - (caching ? 1 : 2));
            b.expect("B: the writer is let in", exec(writer, "insert into side(x) values (3)"));
            unit.cursors[1].reset();
            b.expectLong("B: still nothing mid-execution", 0, statementsOf(db).busy);

            // the statement that was reset runs again from the start
            std::unique_ptr<DBResultSet> again(db.executeQuery(selectIds));
            b.expect("B: the same SQL starts again at row 1", again && again->next() && again->longForColumnIndex(0) == 1);
            again.reset();
            b.expect("B: no error", snapshotClear(db));

            unit.cursors.clear();
            db.close();
            b.expect("B: closed", !db.goodConnection());
            b.expectLong("B: the file is free after close (a writer is let in)", 1, exec(writer, "insert into side(x) values (4)") ? 1 : 0);
            b.expect("B: the connection can be opened again", db.open() && scalar(db, "select count(*) from t") == 10);
            b.expect("B: no error after reopening", snapshotClear(db));
            db.close();
            unit.db.reset();
         });
         crew.joinAll(round + "hand-off");
         a.print(round);
         b.print(round);
         checkLong(round + "times two threads were in the unit together", 0, occupancy.overlaps());
         check(round + "order of use (" + occupancy.order() + ")", occupancy.order() == "AB");
         evidence(round + "statements held by the connection: before cursors=" + std::to_string(totalBefore) +
                  " with live cursors=" + std::to_string(totalAfterLiveCursors));
      }
   };

   // A cursor can outlive its database, also across threads: A creates both, B
   // closes or destroys the database (which invalidates the cursor), and the
   // cursor is destroyed on a third thread C after a second hand-off.
   s["concurrency_cursor_outlives_database_across_threads"] = [] {
      if (!threadSafeSqlite()) {
         return;
      }
      for (const bool explicitClose : {true, false}) {
         const std::string round = std::string(explicitClose ? "close() then destroy" : "destroy") + ": ";
         const std::string path = pathOf(explicitClose ? "outlive-close.sqlite3" : "outlive-destroy.sqlite3");

         Report a;
         Report b;
         Report c;
         Occupancy occupancy;
         Baton aToB;
         Baton bToC;
         Crew crew;

         crew.start([&] {   // A
            Unit unit;
            {
               Occupancy::Use use(occupancy, "A");
               unit.db = std::make_unique<SQLiteDatabase>(path);
               a.expect("A: opens", unit.db->open());
               fillTable(*unit.db, a, 10);
               a.expect("A: side table", exec(*unit.db, "create table side(x integer)"));
               unit.cursors.emplace_back(unit.db->executeQuery("select id from t order by id"));
               a.expect("A: the cursor is on row 1", unit.cursors[0] && unit.cursors[0]->next() &&
                                                      unit.cursors[0]->longForColumnIndex(0) == 1);
            }
            aToB.give(std::move(unit));
         });

         crew.start([&] {   // B: ends the database, passes the cursor on
            Unit unit;
            if (!aToB.take(unit)) {
               b.expect("B: the unit arrived", false);
               return;
            }
            Unit rest;
            {
               Occupancy::Use use(occupancy, "B");
               SQLiteDatabase writer(path);
               writer.setBusyRetryTimeout(0);
               b.expect("B: the independent writer opens", writer.open());
               b.expect("B: the writer is refused while the cursor is alive", !exec(writer, "insert into side(x) values (1)"));
               if (explicitClose) {
                  unit.db->close();
               }
               unit.db.reset();   // the database is destroyed on B; the cursor lives on
               b.expect("B: the cursor reports that its database went away",
                        unit.cursors[0]->status() == DBResultSetStatus::Invalidated);
               b.expect("B: the destroyed database let go of its lock", exec(writer, "insert into side(x) values (2)"));
               rest.cursors = std::move(unit.cursors);
            }
            bToC.give(std::move(rest));
         });

         crew.start([&] {   // C: destroys the cursor
            Unit unit;
            if (!bToC.take(unit)) {
               c.expect("C: the cursor arrived", false);
               return;
            }
            Occupancy::Use use(occupancy, "C");
            c.expect("C: the cursor arrived alone", unit.db == nullptr && unit.cursors.size() == 1);
            DBResultSet& rs = *unit.cursors[0];
            c.expect("C: the cursor is Invalidated, not Done and not Error", rs.status() == DBResultSetStatus::Invalidated);
            c.expect("C: next() is false", !rs.next());
            c.expect("C: and stays Invalidated", rs.status() == DBResultSetStatus::Invalidated && rs.lastErrorCode() == 0);
            rs.close();
            c.expect("C: close() on it is harmless and keeps Invalidated", rs.status() == DBResultSetStatus::Invalidated);
            unit.cursors.clear();   // destroyed on C, long after the database
         });
         crew.joinAll(round + "hand-offs");

         a.print(round);
         b.print(round);
         c.print(round);
         checkLong(round + "times two threads were in the unit together", 0, occupancy.overlaps());
         check(round + "order of use (" + occupancy.order() + ")", occupancy.order() == "ABC");
         check(round + "the file is intact (10 rows, writer rows kept)", idsInFile(path).size() == 10);
      }
   };

   return s;
}

}


int main(int argc, char** argv) {
   // output must survive a crash or a kill
   std::setvbuf(stdout, nullptr, _IONBF, 0);

   if (argc < 2) {
      std::fprintf(stderr, "usage: chapeau_concurrency_probe <scenario> | list\n");
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

   chapeau_tests::ScopedTempDir dir("chapeau-concurrency");
   g_dir = dir.path().string();

   found->second();

   std::printf("SCENARIO %s finished, %d failed check(s)\n", argv[1], g_failures);
   return g_failures == 0 ? 0 : 1;
}
