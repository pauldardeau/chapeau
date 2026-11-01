// Scenarios for the subprocess regression harness. One scenario per process:
//
//   chapeau_probe <scenario>     run it; exit 0 if every CHECK passed,
//                                1 if one failed, 2 for a usage error
//   chapeau_probe list           print the scenario names
//
// Three kinds of scenario live here:
//
//   control_*   behaviour that works today. They run as ordinary regression
//               cases (expected: success) in every build.
//
//   busy_*      the busy timeout and lock contention (bounded waits, errors,
//               recovery). Also ordinary regression cases.
//
//   ch<nn>_*    reproductions of the H1a defects (CH-01 .. CH-08).
//               Each one describes what a fixed Chapeau must do and ends
//               with exit 0 only then. Today they end in a sanitizer report,
//               a crash, a hang (killed by the harness) or a failed CHECK.
//               While a defect exists they are registered only when
//               CHAPEAU_KNOWN_DEFECTS=ON and are NOT in the default test set;
//               once fixed they move to CHAPEAU_FIXED_TABLE in
//               known_defects.cmake and run by default.
//
// The memory-safety defects (CH-01, CH-02) read freed memory. In a plain
// build that may go unnoticed, crash, or return garbage; they are only
// detected reliably in an AddressSanitizer build. Nothing here is written to
// need undefined behaviour to "work": a scenario that ends with exit 0 in a
// plain build is not evidence that its defect is fixed.
//
// The expected-correct behaviour is deliberately the weakest one that is
// still correct (no memory error, no hang, no wrong data), so that the
// eventual fix is free to choose its own API contract.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "scoped_temp_dir.h"
#include "SQLiteDatabase.h"
#include "SQLiteResultSet.h"
#include "DBStatementArgs.h"
#include "DBLong.h"
#include "DBString.h"

using namespace chapeau;

namespace {

int g_failures = 0;
std::string g_path;

void check(const char* label, bool ok) {
   if (!ok) {
      ++g_failures;
   }
   std::printf("CHECK %s: %s\n", label, ok ? "PASS" : "FAIL");
}

void checkLong(const char* label, long long expected, long long actual) {
   const bool ok = (expected == actual);
   if (!ok) {
      ++g_failures;
   }
   std::printf("CHECK %s: %s (expected %lld, got %lld)\n",
               label, ok ? "PASS" : "FAIL", expected, actual);
}

// milliseconds on a clock that does not jump, for 'it waited, but not forever' checks
long long nowMs() {
   return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

void defect(const char* id, const char* summary) {
   std::printf("KNOWN DEFECT %s: %s\n", id, summary);
}

// opens g_path and makes the table t(id, v)
void openDb(SQLiteDatabase& db, bool cacheStatements = true) {
   check("database opens", db.open());
   db.setShouldCacheStatements(cacheStatements);
   unsigned long n = 0;
   check("table is created",
         db.executeUpdate("create table if not exists t(id integer primary key, v text)", n));
}

void insert3(SQLiteDatabase& db) {
   for (int i = 1; i <= 3; ++i) {
      unsigned long n = 0;
      const std::string sql = "insert into t(id,v) values (" + std::to_string(i) +
                              ",'row" + std::to_string(i) + "')";
      if (!db.executeUpdate(sql, n)) {
         check("sample row is inserted", false);
      }
   }
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

using Scenario = std::function<void()>;

std::map<std::string, Scenario> scenarios() {
   std::map<std::string, Scenario> s;

   // ===================== controls: work today =====================

   s["control_interleaved_cursors_different_sql"] = [] {
      SQLiteDatabase db(g_path);
      openDb(db);
      insert3(db);
      std::unique_ptr<DBResultSet> r1(db.executeQuery("select id from t order by id"));
      std::unique_ptr<DBResultSet> r2(db.executeQuery("select id*10 from t order by id"));
      check("both cursors exist", r1 && r2);
      if (r1 && r2) {
         r1->next(); r2->next(); r1->next(); r2->next();
         checkLong("first cursor, second row", 2, r1->intForColumnIndex(0));
         checkLong("second cursor, second row", 20, r2->intForColumnIndex(0));
      }
   };

   s["control_reuse_after_partial_iteration"] = [] {
      SQLiteDatabase db(g_path);
      openDb(db);
      insert3(db);
      {
         std::unique_ptr<DBResultSet> rs(db.executeQuery("select id from t order by id"));
         check("one row is read", rs && rs->next());
      }   // dropped before reaching the end
      std::unique_ptr<DBResultSet> rs(db.executeQuery("select id from t order by id"));
      check("a new cursor has rows", rs && rs->next());
      if (rs) {
         checkLong("a new cursor starts at the first row", 1, rs->intForColumnIndex(0));
      }
   };

   s["control_zero_rows_then_rows"] = [] {
      SQLiteDatabase db(g_path);
      openDb(db);
      insert3(db);
      {
         DBStatementArgs none;
         none.add(new DBLong(99));
         std::unique_ptr<DBResultSet> rs(db.executeQuery("select v from t where id = ?", none));
         check("a query that matches nothing returns a cursor", rs != nullptr);
         check("and the cursor has no rows", rs && !rs->next());
      }
      DBStatementArgs some;
      some.add(new DBLong(2));
      std::unique_ptr<DBResultSet> rs(db.executeQuery("select v from t where id = ?", some));
      check("the same SQL later matches a row", rs && rs->next());
      if (rs) {
         check("with the right value", text(*rs, 0) == "row2");
      }
   };

   s["control_reuse_after_constraint_error"] = [] {
      SQLiteDatabase db(g_path);
      db.setLogsErrors(false);
      openDb(db);
      unsigned long n = 0;
      DBStatementArgs first;
      first.add(new DBLong(1));
      first.add(new DBString("first"));
      check("first insert", db.executeUpdate("insert into t(id,v) values (?,?)", first, n));
      DBStatementArgs duplicate;
      duplicate.add(new DBLong(1));
      duplicate.add(new DBString("duplicate"));
      check("duplicate key is rejected",
            !db.executeUpdate("insert into t(id,v) values (?,?)", duplicate, n));
      DBStatementArgs second;
      second.add(new DBLong(2));
      second.add(new DBString("second"));
      check("the statement works again",
            db.executeUpdate("insert into t(id,v) values (?,?)", second, n));
      checkLong("two rows are stored", 2, scalar(db, "select count(*) from t"));
   };

   s["control_changing_params_same_sql"] = [] {
      SQLiteDatabase db(g_path);
      openDb(db);
      insert3(db);
      for (int id = 3; id >= 1; --id) {
         DBStatementArgs args;
         args.add(new DBLong(id));
         std::unique_ptr<DBResultSet> rs(db.executeQuery("select v from t where id = ?", args));
         check("a row for the parameter", rs && rs->next());
         if (rs) {
            check("it is the row for that parameter", text(*rs, 0) == "row" + std::to_string(id));
         }
      }
   };

   // a nonzero busy limit makes a blocked call give up instead of hanging
   s["control_busy_limit_gives_up"] = [] {
      SQLiteDatabase a(g_path);
      openDb(a);
      check("exclusive transaction", a.beginTransaction());
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      b.setBusyRetryTimeout(200);
      std::unique_ptr<DBResultSet> rs(b.executeQuery("select * from t"));
      check("the blocked query gives up and returns no result set", rs == nullptr);
      check("rollback", a.rollback());
   };

   // ===================== CH-01: cursor outlives its owner =====================

   s["ch01_cursor_after_close"] = [] {
      defect("CH-01", "a result set is used after the database was close()d");
      auto db = std::make_unique<SQLiteDatabase>(g_path);
      openDb(*db);
      insert3(*db);
      std::unique_ptr<DBResultSet> rs(db->executeQuery("select id from t"));
      db->close();
      check("next() on the cursor of a closed database returns false", !rs->next());
      rs.reset();
   };

   s["ch01_cursor_after_database_destroyed"] = [] {
      defect("CH-01", "a result set is destroyed after its database object");
      auto db = std::make_unique<SQLiteDatabase>(g_path);
      openDb(*db);
      insert3(*db);
      std::unique_ptr<DBResultSet> rs(db->executeQuery("select id from t"));
      db.reset();
      rs.reset();
      check("the cursor can be destroyed after its database", true);
   };

   s["ch01_cursor_after_cache_disabled"] = [] {
      defect("CH-01", "switching the statement cache off finalizes statements live cursors use");
      SQLiteDatabase db(g_path);
      openDb(db);
      insert3(db);
      std::unique_ptr<DBResultSet> rs(db.executeQuery("select id from t"));
      db.setShouldCacheStatements(false);
      rs->next();
      rs.reset();
      check("the cursor survived the cache being switched off", true);
   };

   // ===================== CH-02: bound arguments =====================

   s["ch02_bound_args_destroyed_before_step"] = [] {
      defect("CH-02", "parameters are bound without copying and the arguments are destroyed before stepping");
      SQLiteDatabase db(g_path);
      openDb(db);
      const std::string value(64, 'q');
      auto args = std::make_unique<DBStatementArgs>();
      args->add(new DBString(value));
      std::unique_ptr<DBResultSet> rs(db.executeQuery("select ?", *args));
      args.reset();
      check("the cursor has a row", rs && rs->next());
      if (rs) {
         check("the bound value is intact", text(*rs, 0) == value);
      }
   };

   // ===================== CH-03: no-arg overload and the cache =====================

   s["ch03_noarg_overload_invalidates_cached_statement"] = [] {
      defect("CH-03", "executeQuery(sql) on parameterised SQL finalizes the statement the cache still holds");
      SQLiteDatabase db(g_path);
      openDb(db);
      {
         DBStatementArgs args;
         args.add(new DBLong(7));
         std::unique_ptr<DBResultSet> rs(db.executeQuery("select ?", args));
         check("first parameterised call works", rs && rs->next());
      }
      std::unique_ptr<DBResultSet> bad(db.executeQuery("select ?"));
      check("the no-argument call on SQL that needs a parameter is rejected", bad == nullptr);
      DBStatementArgs again;
      again.add(new DBLong(9));
      std::unique_ptr<DBResultSet> rs(db.executeQuery("select ?", again));
      check("the cached SQL still works afterwards", rs && rs->next());
      if (rs) {
         checkLong("with the new parameter", 9, rs->longForColumnIndex(0));
      }
   };

   // ===================== CH-04: two cursors, one statement =====================

   s["ch04_two_cursors_same_sql_independent"] = [] {
      defect("CH-04", "two live cursors over the same SQL share one statement");
      SQLiteDatabase db(g_path);
      openDb(db);
      insert3(db);
      std::unique_ptr<DBResultSet> r1(db.executeQuery("select id from t order by id"));
      std::unique_ptr<DBResultSet> r2(db.executeQuery("select id from t order by id"));
      check("both cursors exist", r1 && r2);
      if (r1 && r2) {
         r1->next();
         checkLong("first cursor, first row", 1, r1->intForColumnIndex(0));
         r2->next();
         checkLong("second cursor, first row", 1, r2->intForColumnIndex(0));
         r1->next();
         checkLong("first cursor, second row", 2, r1->intForColumnIndex(0));
      }
   };

   // ===================== CH-05: next() after the end =====================

   s["ch05_next_after_end_cached"] = [] {
      defect("CH-05", "next() after it returned false restarts the query (cached statement)");
      SQLiteDatabase db(g_path);
      openDb(db, true);
      insert3(db);
      std::unique_ptr<DBResultSet> rs(db.executeQuery("select id from t order by id"));
      int rows = 0;
      while (rs->next()) {
         ++rows;
      }
      checkLong("the first pass sees three rows", 3, rows);
      check("next() after the end stays false", !rs->next());
   };

   s["ch05_next_after_end_uncached"] = [] {
      defect("CH-05", "next() after it returned false dereferences a null statement (uncached)");
      SQLiteDatabase db(g_path);
      openDb(db, false);
      insert3(db);
      std::unique_ptr<DBResultSet> rs(db.executeQuery("select id from t order by id"));
      while (rs->next()) {
      }
      check("next() after the end stays false", !rs->next());
   };

   // ===================== CH-06: schema change =====================

   s["ch06_schema_change_cached_update"] = [] {
      defect("CH-06", "a cached statement prepared with sqlite3_prepare fails permanently after a schema change");
      SQLiteDatabase db(g_path);
      openDb(db);
      unsigned long n = 0;
      DBStatementArgs before;
      before.add(new DBString("before"));
      check("insert before the schema change",
            db.executeUpdate("insert into t(v) values (?)", before, n));
      check("alter table", db.executeUpdate("alter table t add column extra integer", n));
      DBStatementArgs after;
      after.add(new DBString("after"));
      check("the same insert after the schema change",
            db.executeUpdate("insert into t(v) values (?)", after, n));
      DBStatementArgs once_more;
      once_more.add(new DBString("after again"));
      check("and once more",
            db.executeUpdate("insert into t(v) values (?)", once_more, n));
      checkLong("three rows are stored", 3, scalar(db, "select count(*) from t"));
   };

   s["ch06_schema_change_cached_query"] = [] {
      defect("CH-06", "a cached parameterised query fails after a schema change");
      SQLiteDatabase db(g_path);
      openDb(db);
      insert3(db);
      unsigned long n = 0;
      {
         DBStatementArgs args;
         args.add(new DBLong(2));
         std::unique_ptr<DBResultSet> rs(db.executeQuery("select v from t where id = ?", args));
         check("a row before the schema change", rs && rs->next());
      }
      check("create index", db.executeUpdate("create index ix_t_v on t(v)", n));
      DBStatementArgs args;
      args.add(new DBLong(2));
      std::unique_ptr<DBResultSet> rs(db.executeQuery("select v from t where id = ?", args));
      check("a row after the schema change", rs && rs->next());
      if (rs) {
         check("with the right value", text(*rs, 0) == "row2");
      }
   };

   // ===================== CH-07: lock contention is bounded =====================
   // Another connection holds a lock. A call that needs it waits for the busy
   // timeout (default 5000 ms; the scenarios that do not test the default use
   // a short one, to stay fast) and then fails with SQLITE_BUSY. None may wait
   // forever, and none may be mistaken for success.

   s["ch07_busy_prepare_hang"] = [] {
      defect("CH-07", "executeQuery() retried forever while another connection held an exclusive lock");
      SQLiteDatabase a(g_path);
      openDb(a);
      check("exclusive transaction", a.beginTransaction());
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      checkLong("the default busy timeout is 5000 ms", 5000, b.busyRetryTimeout());

      const long long start = nowMs();
      std::unique_ptr<DBResultSet> rs(b.executeQuery("select * from t"));
      const long long waited = nowMs() - start;
      check("executeQuery fails: no result set", rs == nullptr);
      checkLong("and the failure is SQLITE_BUSY", SQLITE_BUSY, b.lastErrorCode());
      check("it waited for the lock first (at least 2 s of the 5 s)", waited >= 2000);
      check("and did not wait forever (under 30 s)", waited < 30000);

      check("rollback", a.rollback());
      rs.reset(b.executeQuery("select id from t"));
      check("once the lock is released the same call works", rs != nullptr);
   };

   s["ch07_busy_next_hang"] = [] {
      defect("CH-07", "next() retried forever while another connection held an exclusive lock");
      SQLiteDatabase a(g_path);
      openDb(a);
      insert3(a);
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      b.setBusyRetryTimeout(500);
      {
         std::unique_ptr<DBResultSet> warm(b.executeQuery("select id from t"));
         check("statement is cached on the second connection", warm != nullptr);
      }
      check("exclusive transaction", a.beginTransaction());
      std::unique_ptr<DBResultSet> rs(b.executeQuery("select id from t"));
      check("a cursor is returned (the cached statement needs no prepare)", rs != nullptr);
      if (rs) {
         check("a new result set is Ready", rs->status() == DBResultSetStatus::Ready);
         const long long start = nowMs();
         const bool row = rs->next();
         const long long waited = nowMs() - start;
         check("next() fails", !row);
         check("the result set is in the Error state", rs->status() == DBResultSetStatus::Error);
         checkLong("with SQLITE_BUSY as the error", SQLITE_BUSY, rs->lastErrorCode());
         check("after waiting for the lock (at least 250 ms of the 500)", waited >= 250);
         check("and not forever (under 30 s)", waited < 30000);
         check("next() stays false", !rs->next());
         check("the state stays Error", rs->status() == DBResultSetStatus::Error);
         checkLong("and the error code is kept", SQLITE_BUSY, rs->lastErrorCode());
      }
      check("rollback", a.rollback());

      // contention gone: a new query works; the failed result set stays failed
      std::unique_ptr<DBResultSet> again(b.executeQuery("select id from t order by id"));
      int rows = 0;
      while (again && again->next()) {
         ++rows;
      }
      checkLong("a new query reads all three rows", 3, rows);
      check("it ended normally: Done", again && again->status() == DBResultSetStatus::Done);
      check("the failed result set is not restarted", rs && !rs->next());
      check("and is still Error", rs && rs->status() == DBResultSetStatus::Error);
   };

   s["ch07_begin_blocked_by_open_cursor"] = [] {
      defect("CH-07", "beginTransaction() retried forever while a cursor on another connection held a read lock");
      SQLiteDatabase a(g_path);
      openDb(a);
      insert3(a);
      std::unique_ptr<DBResultSet> rs(a.executeQuery("select id from t"));
      check("a row is read, so a read lock is held", rs && rs->next());
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      b.setBusyRetryTimeout(500);

      const long long start = nowMs();
      const bool began = b.beginTransaction();   // BEGIN EXCLUSIVE
      const long long waited = nowMs() - start;
      check("beginTransaction fails", !began);
      checkLong("with SQLITE_BUSY", SQLITE_BUSY, b.lastErrorCode());
      check("it is not recorded as a transaction", !b.inTransaction());
      check("after waiting for the lock", waited >= 250);
      check("and not forever", waited < 30000);

      rs.reset();   // the read lock goes away
      check("once the cursor is gone the same call works", b.beginTransaction());
      check("and the transaction is recorded", b.inTransaction());
      check("rollback", b.rollback());
   };

   s["ch07_commit_blocked_by_reader"] = [] {
      defect("CH-07", "commit() retried forever while another connection was mid-read");
      SQLiteDatabase a(g_path);
      openDb(a);
      insert3(a);
      a.setBusyRetryTimeout(500);
      check("deferred transaction", a.beginDeferredTransaction());
      unsigned long n = 0;
      check("a write inside it", a.executeUpdate("insert into t(id,v) values (10,'w')", n));
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      std::unique_ptr<DBResultSet> rs(b.executeQuery("select id from t"));
      check("the reader holds a shared lock", rs && rs->next());

      const long long start = nowMs();
      const bool committed = a.commit();
      const long long waited = nowMs() - start;
      check("commit fails", !committed);
      checkLong("with SQLITE_BUSY", SQLITE_BUSY, a.lastErrorCode());
      check("the transaction is still open", a.inTransaction());
      check("after waiting for the reader", waited >= 250);
      check("and not forever", waited < 30000);

      rs.reset();   // the reader lets go
      check("once the reader is gone the commit succeeds", a.commit());
      check("and the transaction is closed", !a.inTransaction());
      checkLong("the committed row is visible to the other connection", 4,
                scalar(b, "select count(*) from t"));
   };

   s["ch07_close_with_live_cursor"] = [] {
      defect("CH-07", "close() retries forever while an uncached cursor still holds a statement");
      SQLiteDatabase db(g_path);
      openDb(db, false);
      insert3(db);
      DBResultSet* rs = db.executeQuery("select id from t");
      check("a cursor exists", rs != nullptr);
      std::printf("close() with a live, uncached cursor\n");
      db.close();
      check("close returned", true);
      delete rs;
   };

   s["ch07_destructor_with_live_cursor"] = [] {
      defect("CH-07", "the destructor retries forever while an uncached cursor still holds a statement");
      auto db = std::make_unique<SQLiteDatabase>(g_path);
      openDb(*db, false);
      insert3(*db);
      DBResultSet* rs = db->executeQuery("select id from t");
      check("a cursor exists", rs != nullptr);
      std::printf("destroying the database with a live, uncached cursor\n");
      db.reset();
      check("the destructor returned", true);
      delete rs;
   };

   // ===================== CH-08: a failed step is an error, not the end =====================

   s["ch08_busy_in_next_is_an_error"] = [] {
      defect("CH-08", "a BUSY error inside next() looked like the end of the data");
      SQLiteDatabase a(g_path);
      openDb(a);
      insert3(a);
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      {
         std::unique_ptr<DBResultSet> warm(b.executeQuery("select id from t"));
         check("statement is cached on the second connection", warm != nullptr);
      }
      b.setBusyRetryTimeout(50);
      check("exclusive transaction", a.beginTransaction());
      std::unique_ptr<DBResultSet> rs(b.executeQuery("select id from t"));
      check("a result set is returned", rs != nullptr);
      int rows = 0;
      while (rs && rs->next()) {
         ++rows;
      }
      // the table holds three rows but cannot be read right now
      checkLong("no rows were returned", 0, rows);
      check("but the result set does not claim the data is exhausted",
            rs && rs->status() != DBResultSetStatus::Done);
      check("it says it failed", rs && rs->status() == DBResultSetStatus::Error);
      checkLong("because the database is busy", SQLITE_BUSY, rs ? rs->lastErrorCode() : -1);

      check("rollback", a.rollback());
      std::unique_ptr<DBResultSet> again(b.executeQuery("select id from t"));
      rows = 0;
      while (again && again->next()) {
         ++rows;
      }
      checkLong("the same query succeeds once the lock is gone", 3, rows);
      check("and ends normally", again && again->status() == DBResultSetStatus::Done);
   };

   // ===================== the busy timeout, and other operations under contention =====================

   s["busy_zero_timeout_fails_at_once"] = [] {
      SQLiteDatabase a(g_path);
      openDb(a);
      check("exclusive transaction", a.beginTransaction());
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      b.setBusyRetryTimeout(0);
      checkLong("the timeout reads back as 0", 0, b.busyRetryTimeout());

      const long long start = nowMs();
      std::unique_ptr<DBResultSet> rs(b.executeQuery("select * from t"));
      const long long waited = nowMs() - start;
      check("the query fails", rs == nullptr);
      checkLong("with SQLITE_BUSY", SQLITE_BUSY, b.lastErrorCode());
      check("without waiting (under 2 s; the default would wait 5 s)", waited < 2000);

      check("a transaction does not wait either", !b.beginTransaction());
      check("rollback", a.rollback());
   };

   s["busy_positive_timeout_waits_then_fails"] = [] {
      SQLiteDatabase a(g_path);
      openDb(a);
      check("exclusive transaction", a.beginTransaction());
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      b.setBusyRetryTimeout(300);
      checkLong("the timeout reads back as 300", 300, b.busyRetryTimeout());

      const long long start = nowMs();
      std::unique_ptr<DBResultSet> rs(b.executeQuery("select * from t"));
      const long long waited = nowMs() - start;
      check("the query fails", rs == nullptr);
      checkLong("with SQLITE_BUSY", SQLITE_BUSY, b.lastErrorCode());
      check("after waiting for the lock (at least 150 ms of the 300)", waited >= 150);
      check("and not for long (under 4 s)", waited < 4000);
      check("rollback", a.rollback());
   };

   s["busy_timeout_before_and_after_open"] = [] {
      SQLiteDatabase a(g_path);
      openDb(a);
      check("exclusive transaction", a.beginTransaction());

      // set before open(): applied when the connection is opened
      SQLiteDatabase b(g_path);
      b.setBusyRetryTimeout(300);
      check("second connection opens", b.open());
      long long start = nowMs();
      std::unique_ptr<DBResultSet> rs(b.executeQuery("select * from t"));
      long long waited = nowMs() - start;
      check("set before open: the query fails", rs == nullptr);
      check("set before open: it waited (at least 150 ms)", waited >= 150);
      check("set before open: and not for long", waited < 4000);

      // changed after open(): takes effect at once
      b.setBusyRetryTimeout(0);
      start = nowMs();
      rs.reset(b.executeQuery("select * from t"));
      waited = nowMs() - start;
      check("changed to 0 after open: the query fails", rs == nullptr);
      check("changed to 0 after open: without waiting", waited < 2000);

      b.setBusyRetryTimeout(400);
      start = nowMs();
      rs.reset(b.executeQuery("select * from t"));
      waited = nowMs() - start;
      check("changed to 400 after open: the query fails", rs == nullptr);
      check("changed to 400 after open: it waited (at least 200 ms)", waited >= 200);

      // and a closed and reopened connection keeps the setting
      b.close();
      check("reopen", b.open());
      checkLong("the timeout survives close() and open()", 400, b.busyRetryTimeout());
      start = nowMs();
      rs.reset(b.executeQuery("select * from t"));
      waited = nowMs() - start;
      check("after reopening: the query fails", rs == nullptr);
      check("after reopening: it waited (at least 200 ms)", waited >= 200);
      check("rollback", a.rollback());
   };

   s["busy_update_fails_then_recovers"] = [] {
      SQLiteDatabase a(g_path);
      openDb(a);
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      b.setBusyRetryTimeout(200);
      unsigned long n = 0;
      DBStatementArgs first;
      first.add(new DBLong(1));
      first.add(new DBString("one"));
      check("an insert works while nothing is locked",
            b.executeUpdate("insert into t(id,v) values (?,?)", first, n));

      check("exclusive transaction on the other connection", a.beginTransaction());
      DBStatementArgs blocked;
      blocked.add(new DBLong(2));
      blocked.add(new DBString("two"));
      const long long start = nowMs();
      check("the same insert fails while the other connection holds the lock",
            !b.executeUpdate("insert into t(id,v) values (?,?)", blocked, n));
      checkLong("with SQLITE_BUSY", SQLITE_BUSY, b.lastErrorCode());
      check("after waiting a little", nowMs() - start >= 100);
      checkLong("and reports no affected rows", 0, static_cast<long long>(n));

      check("rollback", a.rollback());
      DBStatementArgs retry;
      retry.add(new DBLong(2));
      retry.add(new DBString("two"));
      check("the same statement works once the lock is gone",
            b.executeUpdate("insert into t(id,v) values (?,?)", retry, n));
      checkLong("exactly the two successful inserts are stored", 2,
                scalar(b, "select count(*) from t"));
   };

   s["busy_close_under_contention"] = [] {
      SQLiteDatabase a(g_path);
      openDb(a);
      insert3(a);
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      {
         std::unique_ptr<DBResultSet> warm(b.executeQuery("select id from t"));
         check("b has a cached statement", warm != nullptr);
      }
      check("exclusive transaction on a", a.beginTransaction());

      // closing the connection that is locked out neither waits nor hangs
      const long long start = nowMs();
      b.close();
      check("b closes at once, however busy the database is", nowMs() - start < 2000);
      check("and really closed", b.sqliteHandle() == nullptr);

      // closing the connection that holds the lock, with a result set active,
      // gives the lock up
      std::unique_ptr<DBResultSet> rs(a.executeQuery("select id from t"));
      check("a result set is active on a", rs && rs->next());
      a.close();
      check("a closes", a.sqliteHandle() == nullptr);
      check("its result set is Invalidated, not Done",
            rs && rs->status() == DBResultSetStatus::Invalidated);
      check("the rolled-back transaction released the lock: b reopens and reads",
            b.open() && scalar(b, "select count(*) from t") == 3);
   };

   // the messages printed when sqlite3_step() fails: SQLite's own code and
   // message, never "Unknown error" for a code SQLite defines (checked by the
   // MATCH / REJECT patterns of the regression case)
   s["diagnostics_step_error_messages"] = [] {
      SQLiteDatabase db(g_path);
      db.setLogsErrors(false);
      openDb(db);
      unsigned long n = 0;
      check("first row", db.executeUpdate("insert into t(id,v) values (1,'a')", n));

      check("a duplicate key through executeUpdate(sql) fails",
            !db.executeUpdate("insert into t(id,v) values (1,'dup')", n));
      DBStatementArgs args;
      args.add(new DBLong(1));
      args.add(new DBString("dup"));
      check("and through executeUpdate(sql, args)",
            !db.executeUpdate("insert into t(id,v) values (?,?)", args, n));
      checkLong("the error code is the constraint code", SQLITE_CONSTRAINT, db.lastErrorCode() & 0xff);

      std::unique_ptr<DBResultSet> dup(db.executeQuery("insert into t(id,v) values (1,'dup')"));
      check("a duplicate key through a result set fails", dup && !dup->next());
      check("the result set reports it", dup && dup->status() == DBResultSetStatus::Error);
      std::unique_ptr<DBResultSet> overflow(
         db.executeQuery("select abs(-9223372036854775807 - 1)"));
      check("an SQL runtime error through a result set fails", overflow && !overflow->next());

      // a busy database is reported as busy
      SQLiteDatabase locker(g_path);
      check("a second connection opens", locker.open());
      check("it takes the exclusive lock", locker.beginTransaction());
      db.setBusyRetryTimeout(100);
      check("an update that cannot get the lock fails",
            !db.executeUpdate("insert into t(id,v) values (2,'b')", n));
      checkLong("with SQLITE_BUSY", SQLITE_BUSY, db.lastErrorCode());
      check("rollback", locker.rollback());
   };

   return s;
}

}


int main(int argc, char** argv) {
   // output must survive a crash or a kill
   std::setvbuf(stdout, nullptr, _IONBF, 0);

   if (argc < 2) {
      std::fprintf(stderr, "usage: chapeau_probe <scenario> | list\n");
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

   chapeau_tests::ScopedTempDir dir("chapeau-probe");
   g_path = dir.file("probe.sqlite3");

   found->second();

   std::printf("SCENARIO %s finished, %d failed check(s)\n", argv[1], g_failures);
   return g_failures == 0 ? 0 : 1;
}
