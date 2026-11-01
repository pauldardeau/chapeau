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
//   characterize_*  prints what happens today without asserting anything
//               (CH-08; see below). Not registered as a test.
//
//   ch<nn>_*    KNOWN DEFECTS from the H1a investigation (CH-01 .. CH-07).
//               Each one describes what a fixed Chapeau must do and ends
//               with exit 0 only then. Today they end in a sanitizer report,
//               a crash, a hang (killed by the harness) or a failed CHECK.
//               They are registered only when CHAPEAU_KNOWN_DEFECTS=ON and
//               are NOT part of the default test set; see CMakeLists.txt.
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

   // ===================== CH-07: a busy limit of 0 retries forever =====================
   // Each of these must return. Today the first call named in the check
   // never returns; the harness kills the process and reports a timeout.

   s["ch07_busy_prepare_hang"] = [] {
      defect("CH-07", "executeQuery() retries forever while another connection holds an exclusive lock");
      SQLiteDatabase a(g_path);
      openDb(a);
      check("exclusive transaction", a.beginTransaction());
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      std::printf("executeQuery() with the default busy limit (%d)\n", b.busyRetryTimeout());
      std::unique_ptr<DBResultSet> rs(b.executeQuery("select * from t"));
      check("executeQuery returned", true);
      rs.reset();
      a.rollback();
   };

   s["ch07_busy_next_hang"] = [] {
      defect("CH-07", "next() retries forever while another connection holds an exclusive lock");
      SQLiteDatabase a(g_path);
      openDb(a);
      insert3(a);
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      {
         std::unique_ptr<DBResultSet> warm(b.executeQuery("select id from t"));
         check("statement is cached on the second connection", warm != nullptr);
      }
      check("exclusive transaction", a.beginTransaction());
      std::unique_ptr<DBResultSet> rs(b.executeQuery("select id from t"));
      check("a cursor was returned (the cached statement needs no prepare)", rs != nullptr);
      std::printf("next() while the other connection holds the lock\n");
      if (rs) {
         rs->next();
      }
      check("next() returned", true);
      rs.reset();
      a.rollback();
   };

   s["ch07_begin_blocked_by_open_cursor"] = [] {
      defect("CH-07", "beginTransaction() retries forever while a cursor on another connection holds a read lock");
      SQLiteDatabase a(g_path);
      openDb(a);
      insert3(a);
      std::unique_ptr<DBResultSet> rs(a.executeQuery("select id from t"));
      check("a row is read, so a read lock is held", rs && rs->next());
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      std::printf("beginTransaction() (BEGIN EXCLUSIVE) while a cursor is open elsewhere\n");
      b.beginTransaction();
      check("beginTransaction returned", true);
   };

   s["ch07_commit_blocked_by_reader"] = [] {
      defect("CH-07", "commit() retries forever while another connection is mid-read");
      SQLiteDatabase a(g_path);
      openDb(a);
      insert3(a);
      check("deferred transaction", a.beginDeferredTransaction());
      unsigned long n = 0;
      check("a write inside it", a.executeUpdate("insert into t(id,v) values (10,'w')", n));
      SQLiteDatabase b(g_path);
      check("second connection opens", b.open());
      std::unique_ptr<DBResultSet> rs(b.executeQuery("select id from t"));
      check("the reader holds a shared lock", rs && rs->next());
      std::printf("commit() while the other connection is mid-read\n");
      a.commit();
      check("commit returned", true);
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

   // ===================== CH-08: characterization only =====================
   // A BUSY error inside next() ends the iteration exactly like the end of the
   // data; DBResultSet has no way to say otherwise (lastErrorCode() on the
   // database does show SQLITE_BUSY). What a fixed Chapeau must do depends on
   // an error-reporting API that does not exist, so nothing is asserted: this
   // prints today's behaviour and is not registered as a test.

   s["characterize_ch08_busy_in_next"] = [] {
      defect("CH-08", "a BUSY error inside next() looks like the end of the data (characterization, not a test)");
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
      int rows = 0;
      while (rs && rs->next()) {
         ++rows;
      }
      // the table holds three rows but cannot be read right now
      std::printf("rows seen: %d (the table holds 3), lastErrorCode on the reader: %d\n",
                  rows, b.lastErrorCode());
      rs.reset();
      a.rollback();
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
