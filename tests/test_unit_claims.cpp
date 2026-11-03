#include "test_unit_claims.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "sqlite3.h"

#include "Database.h"
#include "DBData.h"
#include "DBDate.h"
#include "DBLong.h"
#include "DBStatementArgs.h"
#include "SQLiteDatabase.h"
#include "SQLiteResultSet.h"
#include "UnitState.h"

using namespace std;
using namespace chapeau;


namespace {

// ---- a call that runs inside another call -------------------------------------------
//
// The SQL function cb() calls 'body' the first time SQLite evaluates it. It runs on
// the thread that is inside executeUpdate() or inside a step, so whatever body()
// calls on the database or on a cursor is made while the unit's claim is held by
// the call that is running: a re-entrant call, refused like any other.

struct Callback {
   function<void()> body;
   int calls = 0;
};

Callback* g_callback = nullptr;

void callbackFunction(sqlite3_context* context, int, sqlite3_value**) {
   Callback* callback = g_callback;
   ++callback->calls;
   if (callback->calls == 1 && callback->body) {
      callback->body();
   }
   sqlite3_result_int(context, 1);
}

bool installCallback(SQLiteDatabase& db, Callback& callback) {
   g_callback = &callback;
   return sqlite3_create_function(db.sqliteHandle(), "cb", 0, SQLITE_UTF8, nullptr,
                                  callbackFunction, nullptr, nullptr) == SQLITE_OK;
}

// what a callback saw; it records, the test checks afterwards (a failed require()
// is never to be raised from inside SQLite's call)
struct Notes {
   vector<pair<string, bool>> items;
   void note(const string& what, bool ok) { items.emplace_back(what, ok); }
};

bool exec(SQLiteDatabase& db, const string& sql) {
   unsigned long rows = 0;
   return db.executeUpdate(sql, rows);
}

bool createItems(SQLiteDatabase& db) {
   return exec(db, "create table items(id integer primary key, name text, qty integer)") &&
          exec(db, "insert into items(id, name, qty) values (1, 'apple', 10), (2, 'pear', 20), (3, 'plum', 30)");
}

SQLiteResultSet* openCursor(SQLiteDatabase& db, const string& sql) {
   return dynamic_cast<SQLiteResultSet*>(db.executeQuery(sql));
}

// id, name, qty, a NULL, a REAL, a BLOB
const char* const ROW_QUERY =
   "select id, name, qty, null as empty, 2.5 as ratio, x'0102' as bytes from items order by id";

// what the library writes to stdout while body() runs (POSIX; elsewhere not captured)
string captureStdout(const function<void()>& body, bool& captured) {
#ifndef _WIN32
   fflush(stdout);
   FILE* file = tmpfile();
   const int saved = ::dup(1);
   if (file == nullptr || saved < 0) {
      captured = false;
      body();
      return string();
   }
   ::dup2(::fileno(file), 1);
   body();
   fflush(stdout);
   ::dup2(saved, 1);
   ::close(saved);

   string out;
   rewind(file);
   char buffer[512];
   size_t got = 0;
   while ((got = fread(buffer, 1, sizeof buffer, file)) > 0) {
      out.append(buffer, got);
   }
   fclose(file);
   captured = true;
   return out;
#else
   captured = false;
   body();
   return string();
#endif
}

size_t occurrences(const string& text, const string& what) {
   size_t count = 0;
   for (size_t at = text.find(what); at != string::npos; at = text.find(what, at + what.size())) {
      ++count;
   }
   return count;
}


// ---- a call refused from inside an outer call: what the outer call leaves behind ------------------
//
// cbfail() is cb() that then raises an SQL error, so the statement that runs it fails
// after the callback has run.

void failingCallbackFunction(sqlite3_context* context, int, sqlite3_value**) {
   Callback* callback = g_callback;
   ++callback->calls;
   if (callback->calls == 1 && callback->body) {
      callback->body();
   }
   sqlite3_result_error(context, "cbfail raised", -1);
}

bool installCallbacks(SQLiteDatabase& db, Callback& callback) {
   g_callback = &callback;
   return sqlite3_create_function(db.sqliteHandle(), "cb", 0, SQLITE_UTF8, nullptr,
                                  callbackFunction, nullptr, nullptr) == SQLITE_OK &&
          sqlite3_create_function(db.sqliteHandle(), "cbfail", 0, SQLITE_UTF8, nullptr,
                                  failingCallbackFunction, nullptr, nullptr) == SQLITE_OK;
}

// SQLite calls the authorizer from inside sqlite3_prepare(), that is from inside
// executeQuery(), which runs no SQL function: the one deterministic callback there is
// inside a query that has not returned a cursor yet. 'verdict' is what the authorizer
// answers (SQLITE_DENY makes the prepare fail with SQLITE_AUTH).
struct Authorizer {
   function<void()> body;
   int calls = 0;
   int verdict = SQLITE_OK;
};

Authorizer* g_authorizer = nullptr;

int authorizerFunction(void*, int, const char*, const char*, const char*, const char*) {
   Authorizer* authorizer = g_authorizer;
   if (authorizer == nullptr) {
      return SQLITE_OK;
   }
   ++authorizer->calls;
   if (authorizer->calls == 1 && authorizer->body) {
      authorizer->body();
   }
   return authorizer->verdict;
}

void installAuthorizer(SQLiteDatabase& db, Authorizer& authorizer) {
   g_authorizer = &authorizer;
   sqlite3_set_authorizer(db.sqliteHandle(), authorizerFunction, nullptr);
}

void removeAuthorizer(SQLiteDatabase& db) {
   sqlite3_set_authorizer(db.sqliteHandle(), nullptr, nullptr);
   g_authorizer = nullptr;
}

// a commit hook runs inside COMMIT, that is inside commit(); a non-zero answer turns the
// commit into a rollback and makes it fail
struct CommitHook {
   function<void()> body;
   int calls = 0;
   int answer = 0;
};

CommitHook* g_commitHook = nullptr;

int commitHookFunction(void*) {
   CommitHook* hook = g_commitHook;
   ++hook->calls;
   if (hook->calls == 1 && hook->body) {
      hook->body();
   }
   return hook->answer;
}

}


TestUnitClaims::TestUnitClaims() :
   poivre::TestSuite("TestUnitClaims") {
}

void TestUnitClaims::requireLongEquals(long long expected, long long actual, const string& description) {
   require(expected == actual,
           description + " (expected " + to_string(expected) + ", got " + to_string(actual) + ")");
}

void TestUnitClaims::runTests() {
   test_ordinary_use_is_never_refused();
   test_every_database_entry_point_refuses_inside_a_call();
   test_every_cursor_entry_point_refuses_inside_a_call();
   test_a_refused_next_is_not_the_end_of_the_data();
   test_a_refusal_changes_nothing_of_the_running_call();
   test_reentrance_from_a_cursor_step_is_refused_for_the_database();
   test_inuse_is_a_probe();
   test_refusals_are_logged_at_power_of_two_counts();
   test_refusals_are_silent_when_logging_is_off();
   test_a_cursor_outlives_its_database_and_still_claims();
   test_sequential_transaction_interleaving_is_not_detected();
   test_nested_refusal_outer_query_succeeds();
   test_nested_refusal_outer_query_fails();
   test_nested_refusal_outer_update_succeeds();
   test_nested_refusal_outer_update_fails();
   test_nested_refusal_outer_next_succeeds();
   test_nested_refusal_outer_next_fails();
   test_nested_refusal_outer_transaction_calls();
   test_nested_refusal_does_not_hide_a_failure_of_an_earlier_call();
   test_nested_refusal_outer_good_connection_succeeds();
   test_a_note_of_a_nested_refusal_does_not_leak_into_a_later_call();
   test_cross_thread_refusals_never_touch_the_error_snapshot();
}

void TestUnitClaims::test_ordinary_use_is_never_refused() {
   TEST_CASE("test_ordinary_use_is_never_refused");
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();
   shared_ptr<UnitState> unit;
   unique_ptr<SQLiteResultSet> survivor;

   {
      SQLiteDatabase db(":memory:");
      db.setLogsErrors(false);
      unit = UnitStateAccess::of(db);
      require(db.open(), "open");
      require(createItems(db), "create and fill");

      // every kind of call, one after another, on one thread: the claim of each is
      // released before the next, and nothing a call does internally claims again
      DBStatementArgs update;
      update.add(new DBLong(77));
      update.add(new DBLong(1));
      unsigned long rows = 0;
      require(db.executeUpdate("update items set qty = ? where id = ?", update, rows) && rows == 1, "executeUpdate with arguments");

      DBStatementArgs query;
      query.add(new DBLong(2));
      unique_ptr<SQLiteResultSet> rs(dynamic_cast<SQLiteResultSet*>(db.executeQuery("select id, name, qty, null as empty, 2.5 as ratio, x'0102' as bytes from items where id >= ? order by id", query)));
      require(rs != nullptr, "executeQuery with arguments");
      require(rs->getColumnCount() == 6, "getColumnCount");
      int rowsRead = 0;
      while (rs->next()) {
         ++rowsRead;
         string text;
         int asInt = 0;
         unique_ptr<string> owned(rs->stringForColumnIndex(1));
         require(owned != nullptr, "stringForColumnIndex");
         require(rs->stringForColumnIndex(1, text) && !text.empty(), "stringForColumnIndex (checked)");
         require(rs->intForColumnIndex(0, asInt) && asInt >= 2, "intForColumnIndex (checked)");
         require(rs->getTypeForColumnIndex(0) == ValueTypeInteger, "getTypeForColumnIndex");
         require(rs->columnIndexForName("QTY") == 2, "columnIndexForName");
         require(rs->intForColumn("id") == asInt && rs->int64ForColumn("id") == asInt && rs->longForColumn("id") == asInt, "by name");
         require(rs->intForColumnIndex(0) == asInt && rs->int64ForColumnIndex(0) == asInt && rs->longForColumnIndex(0) == asInt, "by index");
         require(rs->boolForColumn("qty") && rs->boolForColumnIndex(2), "bool");
         require(rs->doubleForColumn("ratio") == 2.5 && rs->doubleForColumnIndex(4) == 2.5, "double");
         unique_ptr<DBData> data(rs->dataForColumn("bytes"));
         unique_ptr<DBData> dataByIndex(rs->dataForColumnIndex(5));
         require(data != nullptr && dataByIndex != nullptr, "data");
         unique_ptr<DBDate> date(rs->dateForColumnIndex(2));
         unique_ptr<DBDate> dateByName(rs->dateForColumn("qty"));
         unique_ptr<string> byName(rs->stringForColumn("name"));
         require(byName != nullptr && rs->stringForColumn("name", text) && rs->intForColumn("id", asInt), "by name, checked");
      }
      require(rowsRead == 2, "two rows");
      require(rs->status() == DBResultSetStatus::Done, "and the end");
      rs->close();
      rs.reset();

      require(db.goodConnection(), "goodConnection");
      require(db.beginTransaction() && db.inTransaction() && db.inUse(), "begin");
      require(exec(db, "insert into items(id, name, qty) values (4, 'fig', 40)") && db.lastInsertRowId() == 4, "insert and lastInsertRowId");
      require(db.rollback() && !db.inTransaction(), "rollback");
      require(db.beginDeferredTransaction() && db.commit(), "deferred and commit");
      db.setBusyRetryTimeout(250);
      db.setStatementCacheLimit(3);
      db.setShouldCacheStatements(true);
      require(exec(db, "update items set qty = qty"), "cached statement");
      db.clearCachedStatements();
      require(db.cachedStatementForQuery("select 1") == nullptr, "cachedStatementForQuery");
      require(!db.inUse(), "idle: not in use");

      // a cursor that ends after close(), and one that outlives the database
      unique_ptr<SQLiteResultSet> afterClose(openCursor(db, ROW_QUERY));
      require(afterClose && afterClose->next(), "a cursor that is live");
      db.close();
      require(afterClose->status() == DBResultSetStatus::Invalidated, "close() invalidated it");
      afterClose.reset();
      require(db.open(), "reopen");
      require(createItems(db), "and fill again");
      survivor.reset(openCursor(db, ROW_QUERY));
      require(survivor && survivor->next(), "a cursor that will outlive the database");
   }

   require(survivor->status() == DBResultSetStatus::Invalidated, "the database went away");
   require(!survivor->next() && survivor->longForColumnIndex(0) == 0, "and the cursor can still be used, harmlessly");
   survivor.reset();

   requireLongEquals(0, (long) unit->refusedCalls(), "nothing was refused on the unit");
   requireLongEquals(0, (long) (Database::refusedCallsOnThisThread() - threadBefore), "nothing was refused to this thread");
}

void TestUnitClaims::test_every_database_entry_point_refuses_inside_a_call() {
   TEST_CASE("test_every_database_entry_point_refuses_inside_a_call");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   db.setStatementCacheLimit(5);
   const int timeoutBefore = db.busyRetryTimeout();
   require(exec(db, "update items set qty = qty"), "something is cached");

   Callback callback;
   require(installCallback(db, callback), "a SQL function that calls the database");

   struct Seen {
      bool open = true, good = true, update = true, updateWithArgs = true, begin = true, beginDeferred = true;
      bool commit = true, rollback = true, inTransaction = true, inUse = false;
      DBResultSet* query = nullptr;
      DBResultSet* queryWithArgs = nullptr;
      const DBStatement* cached = nullptr;
      unsigned long rows = 99, rowsWithArgs = 99;
      long long rowId = 0;
      DBErrorKind kindAfterQuery = DBErrorKind::None;
      bool stillOpen = false;
   } seen;

   const uint64_t unitBefore = db.refusedCalls();
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();

   callback.body = [&] {
      seen.open = db.open();
      db.close();                     // must not close the connection under the running statement
      seen.stillOpen = (db.sqliteHandle() != nullptr);
      seen.good = db.goodConnection();
      seen.query = db.executeQuery("select 1");
      seen.kindAfterQuery = db.lastErrorKind();
      DBStatementArgs args;
      args.add(new DBLong(1));
      seen.queryWithArgs = db.executeQuery("select ?", args);
      seen.update = db.executeUpdate("update items set qty = 1", seen.rows);
      seen.updateWithArgs = db.executeUpdate("update items set qty = ?", args, seen.rowsWithArgs);
      seen.begin = db.beginTransaction();
      seen.beginDeferred = db.beginDeferredTransaction();
      seen.commit = db.commit();
      seen.rollback = db.rollback();
      seen.inTransaction = db.inTransaction();
      seen.rowId = db.lastInsertRowId();
      db.setBusyRetryTimeout(1234);
      db.setShouldCacheStatements(false);
      db.setStatementCacheLimit(1);
      db.clearCachedStatements();
      seen.cached = db.cachedStatementForQuery("update items set qty = qty");
      seen.inUse = db.inUse();        // a probe: true, and not a refusal
   };

   require(exec(db, "update items set qty = cb() where id = 1"), "the running update succeeds");
   require(callback.calls >= 1, "the function ran");

   require(!seen.open, "open() was refused");
   require(seen.stillOpen, "close() was refused: the connection is still there");
   require(!seen.good, "goodConnection() was refused");
   require(seen.query == nullptr && seen.queryWithArgs == nullptr, "both executeQuery() refused with null");
   require(!seen.update && seen.rows == 0, "executeUpdate() refused, count cleared");
   require(!seen.updateWithArgs && seen.rowsWithArgs == 0, "executeUpdate() with arguments refused, count cleared");
   require(!seen.begin && !seen.beginDeferred && !seen.commit && !seen.rollback, "the four transaction calls refused");
   require(!seen.inTransaction, "inTransaction() refused with false");
   requireLongEquals(-1, (long) seen.rowId, "lastInsertRowId() refused with -1");
   require(seen.cached == nullptr, "cachedStatementForQuery() refused with null");
   require(seen.inUse, "inUse() from inside the call says busy");
   require(seen.kindAfterQuery == DBErrorKind::InUse, "a database call made from inside a call of its own thread reports InUse, as ever");

   requireLongEquals(timeoutBefore, db.busyRetryTimeout(), "setBusyRetryTimeout() was refused: the timeout is unchanged");
   require(db.shouldCacheStatements(), "setShouldCacheStatements(false) was refused");
   requireLongEquals(5, (long) db.statementCacheLimit(), "setStatementCacheLimit() was refused");

   // 18 refused calls: open, close, goodConnection, 2 queries, 2 updates, 4 transaction calls,
   // inTransaction, lastInsertRowId, 4 cache/timeout calls, cachedStatementForQuery. (inUse() is a probe.)
   const long refused = 3 + 2 + 2 + 4 + 1 + 1 + 4 + 1;
   requireLongEquals(refused, (long) (db.refusedCalls() - unitBefore), "every one of them counted on the unit");
   requireLongEquals(refused, (long) (Database::refusedCallsOnThisThread() - threadBefore), "and on this thread");
   const RefusalRecord record = UnitState::threadRecord();
   require(string(record.operation) == "cachedStatementForQuery" && record.unitSerial == UnitStateAccess::of(db)->serial(),
           "the thread's record names the last one and the unit");

   // the running call was not disturbed, and the unit works afterwards
   require(db.lastErrorKind() == DBErrorKind::None, "the update's success cleared the snapshot");
   unique_ptr<SQLiteResultSet> rs(openCursor(db, "select qty from items where id = 1"));
   require(rs && rs->next() && rs->longForColumnIndex(0) == 1, "the update did its work (qty = cb() = 1)");
   rs.reset();
   unique_ptr<SQLiteResultSet> rest(openCursor(db, "select sum(qty) from items where id <> 1"));
   require(rest && rest->next() && rest->longForColumnIndex(0) == 50, "and the other rows were not touched by the refused updates");
   delete seen.query;
   delete seen.queryWithArgs;
}

void TestUnitClaims::test_every_cursor_entry_point_refuses_inside_a_call() {
   TEST_CASE("test_every_cursor_entry_point_refuses_inside_a_call");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallback(db, callback), "a SQL function");

   // a sibling cursor, on its first row, and the cursor that is being stepped
   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a live sibling on its first row");
   unique_ptr<SQLiteResultSet> stepped(openCursor(db, "select cb() as probe, id from items order by id"));
   require(stepped != nullptr, "the cursor to step");

   // an earlier failure is on the snapshot (stepping a cursor does not replace it)
   require(db.executeQuery("select * from no_such_table") == nullptr, "a failed query");
   const DBErrorKind snapshotKind = db.lastErrorKind();
   const int snapshotCode = db.lastErrorCode();
   const string snapshotMessage = db.lastErrorMessage();
   require(snapshotKind == DBErrorKind::Sqlite && snapshotMessage.find("no_such_table") != string::npos, "which the snapshot holds");

   Notes notes;
   long calls = 0;
   const uint64_t unitBefore = db.refusedCalls();
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();

   callback.body = [&] {
      // on the sibling AND on the cursor that is being stepped: both are the unit's
      SQLiteResultSet* targets[2] = {sibling.get(), stepped.get()};
      for (SQLiteResultSet* rs : targets) {
         const string who = (rs == sibling.get()) ? "sibling: " : "stepped: ";
         int i = 42;
         string s = "stale";

         ++calls; notes.note(who + "next() is false", !rs->next());
         ++calls; rs->close();
         notes.note(who + "close() did not end it", rs->status() != DBResultSetStatus::Done);
         ++calls; notes.note(who + "getColumnCount() is 0", rs->getColumnCount() == 0);
         ++calls; notes.note(who + "getTypeForColumnIndex() is null", rs->getTypeForColumnIndex(0) == ValueTypeNull);
         ++calls; notes.note(who + "columnIndexForName() is -1", rs->columnIndexForName("id") == -1);

         ++calls; notes.note(who + "intForColumn(name) is 0", rs->intForColumn("id") == 0);
         ++calls; notes.note(who + "intForColumnIndex() is 0", rs->intForColumnIndex(0) == 0);
         ++calls; notes.note(who + "int64ForColumn() is 0", rs->int64ForColumn("id") == 0);
         ++calls; notes.note(who + "int64ForColumnIndex() is 0", rs->int64ForColumnIndex(0) == 0);
         ++calls; notes.note(who + "intForColumn(name, out) is false and clears out", !rs->intForColumn("id", i) && i == 0);
         i = 42;
         ++calls; notes.note(who + "intForColumnIndex(i, out) is false and clears out", !rs->intForColumnIndex(0, i) && i == 0);
         ++calls; notes.note(who + "longForColumn() is 0", rs->longForColumn("id") == 0);
         ++calls; notes.note(who + "longForColumnIndex() is 0", rs->longForColumnIndex(0) == 0);
         ++calls; notes.note(who + "boolForColumn() is false", !rs->boolForColumn("id"));
         ++calls; notes.note(who + "boolForColumnIndex() is false", !rs->boolForColumnIndex(0));
         ++calls; notes.note(who + "doubleForColumn() is 0", rs->doubleForColumn("ratio") == 0.0);
         ++calls; notes.note(who + "doubleForColumnIndex() is 0", rs->doubleForColumnIndex(4) == 0.0);

         ++calls; string* byName = rs->stringForColumn("name");
         notes.note(who + "stringForColumn(name) is null", byName == nullptr);
         delete byName;
         ++calls; string* byIndex = rs->stringForColumnIndex(1);
         notes.note(who + "stringForColumnIndex() is null", byIndex == nullptr);
         delete byIndex;
         ++calls; notes.note(who + "stringForColumn(name, out) is false and clears out", !rs->stringForColumn("name", s) && s.empty());
         s = "stale";
         ++calls; notes.note(who + "stringForColumnIndex(i, out) is false and clears out", !rs->stringForColumnIndex(1, s) && s.empty());

         ++calls; DBDate* date = rs->dateForColumn("qty");
         notes.note(who + "dateForColumn() is null", date == nullptr);
         delete date;
         ++calls; date = rs->dateForColumnIndex(2);
         notes.note(who + "dateForColumnIndex() is null", date == nullptr);
         delete date;
         ++calls; DBData* data = rs->dataForColumn("bytes");
         notes.note(who + "dataForColumn() is null", data == nullptr);
         delete data;
         ++calls; data = rs->dataForColumnIndex(5);
         notes.note(who + "dataForColumnIndex() is null", data == nullptr);
         delete data;
      }
   };

   require(stepped->next(), "the step that runs the function goes through");
   require(callback.calls >= 1, "the function ran");
   for (const auto& item : notes.items) {
      require(item.second, item.first);
   }
   requireLongEquals(50, calls, "50 calls were made from inside the step (25 on each cursor)");
   requireLongEquals(calls, (long) (db.refusedCalls() - unitBefore), "all of them were refused and counted on the unit");
   requireLongEquals(calls, (long) (Database::refusedCallsOnThisThread() - threadBefore), "and on the thread");
   require(db.lastErrorKind() == snapshotKind && db.lastErrorCode() == snapshotCode && db.lastErrorMessage() == snapshotMessage,
           "a refused cursor call writes nothing to the database's snapshot: not InUse, not a clear; the earlier failure is still there");

   // nothing of either cursor was changed by the refusals
   require(stepped->status() == DBResultSetStatus::Row, "the stepped cursor is on its row");
   requireLongEquals(1, (long) stepped->longForColumnIndex(1), "and reads it");
   require(sibling->status() == DBResultSetStatus::Row, "the sibling is still on its row");
   requireLongEquals(1, (long) sibling->longForColumnIndex(0), "id of the sibling's row");
   unique_ptr<string> name(sibling->stringForColumn("name"));
   require(name && *name == "apple", "name of the sibling's row");
   require(sibling->next() && sibling->longForColumnIndex(0) == 2, "and it advances to the second row, none skipped");
   require(stepped->next() && stepped->longForColumnIndex(1) == 2, "so does the other");
}

void TestUnitClaims::test_a_refused_next_is_not_the_end_of_the_data() {
   TEST_CASE("test_a_refused_next_is_not_the_end_of_the_data");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallback(db, callback), "a SQL function");

   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "the sibling is on its first row");
   unique_ptr<SQLiteResultSet> stepped(openCursor(db, "select cb() as probe, id from items order by id"));
   require(stepped != nullptr, "a cursor to step");

   bool refusedNext = true;
   DBResultSetStatus statusAfter = DBResultSetStatus::Done;
   uint64_t threadDuring = 0;
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();
   callback.body = [&] {
      refusedNext = sibling->next();
      statusAfter = sibling->status();
      threadDuring = Database::refusedCallsOnThisThread() - threadBefore;
   };
   require(stepped->next(), "the step goes through");

   require(!refusedNext, "the refused next() returned false, as a finished cursor would");
   require(statusAfter == DBResultSetStatus::Row, "but the cursor is not finished: its status is still Row, not Done");
   requireLongEquals(1, (long) threadDuring, "and the thread's refusal count rose, which is how a caller tells the two apart");

   // a genuine end of data: false, Done, and no refusal
   const uint64_t before = Database::refusedCallsOnThisThread();
   unique_ptr<SQLiteResultSet> single(openCursor(db, "select 1"));
   require(single && single->next(), "a row");
   require(!single->next(), "then the end");
   require(single->status() == DBResultSetStatus::Done, "which is Done");
   requireLongEquals(0, (long) (Database::refusedCallsOnThisThread() - before), "and is not a refusal");

   // the refused next() consumed nothing
   require(sibling->longForColumnIndex(0) == 1 && sibling->next() && sibling->longForColumnIndex(0) == 2, "the sibling's rows came in order");
}

void TestUnitClaims::test_a_refusal_changes_nothing_of_the_running_call() {
   TEST_CASE("test_a_refusal_changes_nothing_of_the_running_call");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallback(db, callback), "a SQL function");

   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a sibling on its first row");

   // 1. the running call is a cursor step that FAILS after the function returns;
   //    the refusals made meanwhile must not appear in what the cursor reports
   unique_ptr<SQLiteResultSet> failing(openCursor(db, "select cb(), abs(-9223372036854775807 - 1 - 0 * id) from items order by id"));
   require(failing != nullptr, "a cursor whose step will fail");
   callback.body = [&] {
      sibling->longForColumnIndex(0);
      sibling->next();
      sibling->close();
      failing->longForColumnIndex(0);
   };
   require(!failing->next(), "the step fails");
   require(callback.calls >= 1, "after the function ran");
   require(failing->status() == DBResultSetStatus::Error, "the cursor is in Error");
   requireLongEquals(SQLITE_ERROR, failing->lastErrorCode(), "with SQLite's code");
   require(failing->lastErrorMessage().find("integer overflow") != string::npos, "and SQLite's message, not a refusal's");
   require(db.lastErrorKind() == DBErrorKind::Sqlite && db.lastErrorMessage().find("integer overflow") != string::npos,
           "the database's snapshot is the failure of the step, not of a refusal");
   require(sibling->status() == DBResultSetStatus::Row && sibling->longForColumnIndex(0) == 1, "the sibling is untouched");

   // 2. the running call is an update that fails after the function returns; the
   //    refusals made meanwhile include database calls, which report InUse, and the
   //    failure of the update is what stays
   callback.calls = 0;   // the function stays installed (it cannot be replaced while statements are live)
   callback.body = [&] {
      unique_ptr<DBResultSet> nested(db.executeQuery("select 1"));
      sibling->close();
   };
   unsigned long rows = 0;
   require(!db.executeUpdate("insert into items(id, name, qty) values (cb(), 'dup', 1)", rows), "the insert fails (id 1 exists)");
   require(db.lastErrorKind() == DBErrorKind::Sqlite, "its error is the snapshot, over the InUse of the refused query");
   requireLongEquals(SQLITE_CONSTRAINT, db.lastErrorCode(), "with SQLite's code");
   require(sibling->status() == DBResultSetStatus::Row, "and the sibling is still on its row");
   require(sibling->next() && sibling->longForColumnIndex(0) == 2, "which goes on");
}

void TestUnitClaims::test_reentrance_from_a_cursor_step_is_refused_for_the_database() {
   TEST_CASE("test_reentrance_from_a_cursor_step_is_refused_for_the_database");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallback(db, callback), "a SQL function");
   unique_ptr<SQLiteResultSet> stepped(openCursor(db, "select cb() as probe, id from items order by id"));
   require(stepped != nullptr, "a cursor");

   DBResultSet* nested = reinterpret_cast<DBResultSet*>(1);
   DBErrorKind kind = DBErrorKind::None;
   callback.body = [&] {
      nested = db.executeQuery("select 1");
      kind = db.lastErrorKind();
   };
   // Until H1a-12g the step did not set the in-use marker, so this nested query ran
   // inside the step. The unit is inside next(); the query is refused, with InUse.
   require(stepped->next(), "the step goes through");
   require(nested == nullptr, "the nested query was refused");
   require(kind == DBErrorKind::InUse, "with the in-use error, as for a call from inside an update");
   require(stepped->status() == DBResultSetStatus::Row && stepped->longForColumnIndex(1) == 1, "and the cursor carries on");
   unique_ptr<DBResultSet> after(db.executeQuery("select 2"));
   require(after != nullptr, "the database works afterwards");
}

void TestUnitClaims::test_inuse_is_a_probe() {
   TEST_CASE("test_inuse_is_a_probe");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(true);   // a probe must not write even then
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallback(db, callback), "a SQL function");

   require(!db.inUse(), "idle: not in use");
   db.setInUse(true);
   require(db.inUse(), "the compatibility marker still counts when the unit is free");
   db.setInUse(false);
   require(db.beginTransaction(), "begin");
   require(db.inUse(), "a transaction is in use, as ever");
   require(db.rollback() && !db.inUse(), "and not after it");

   bool busy = false;
   DBErrorKind kind = DBErrorKind::Sqlite;
   const uint64_t unitBefore = db.refusedCalls();
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();
   callback.body = [&] {
      busy = db.inUse();
      busy = db.inUse() && busy;
      kind = db.lastErrorKind();
   };
   bool captured = false;
   const string output = captureStdout([&] { exec(db, "update items set qty = cb() where id = 1"); }, captured);

   require(busy, "from inside a call it says busy");
   require(kind == DBErrorKind::None, "and wrote nothing to the snapshot");
   requireLongEquals(0, (long) (db.refusedCalls() - unitBefore), "no refusal was counted on the unit");
   requireLongEquals(0, (long) (Database::refusedCallsOnThisThread() - threadBefore), "nor on the thread");
   if (captured) {
      require(output.find("ConcurrentUse") == string::npos, "and nothing was logged (output: '" + output + "')");
   }
   require(!db.inUse(), "afterwards: not in use");

   // Inside a cursor STEP the in-use marker is not set (only executeQuery(), executeUpdate() and
   // lastInsertRowId() set it), so only the claim can tell the probe that the unit is busy.
   unique_ptr<SQLiteResultSet> stepped(openCursor(db, "select cb() as probe, id from items order by id"));
   require(stepped != nullptr, "a cursor to step");
   bool busyInStep = false;
   bool ranInStep = false;
   callback.calls = 0;
   callback.body = [&] {
      ranInStep = true;
      busyInStep = db.inUse();
   };
   require(stepped->next(), "the step goes through");
   require(busyInStep, "inUse() from inside a cursor step says busy, which only the claim knows");
   require(ranInStep, "the callback ran");
   requireLongEquals(0, (long) (db.refusedCalls() - unitBefore), "and still counted nothing");
   require(!db.inUse(), "idle again");
}

void TestUnitClaims::test_refusals_are_logged_at_power_of_two_counts() {
   TEST_CASE("test_refusals_are_logged_at_power_of_two_counts");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(true);
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallback(db, callback), "a SQL function");
   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a sibling");
   unique_ptr<SQLiteResultSet> stepped(openCursor(db, "select cb() as probe, id from items order by id"));
   require(stepped != nullptr, "a cursor to step");

   callback.body = [&] {
      for (int i = 0; i < 9; ++i) {
         sibling->longForColumnIndex(0);
      }
   };
   bool captured = false;
   const string output = captureStdout([&] { stepped->next(); }, captured);
   requireLongEquals(9, (long) db.refusedCalls(), "nine refusals");
   if (captured) {
      requireLongEquals(4, (long) occurrences(output, "DB Warning [ConcurrentUse]"), "four lines: the 1st, 2nd, 4th and 8th (output: '" + output + "')");
      for (const char* wanted : {"refusal 1 on", "refusal 2 on", "refusal 4 on", "refusal 8 on"}) {
         require(output.find(wanted) != string::npos, string("a line for ") + wanted);
      }
      for (const char* unwanted : {"refusal 3 on", "refusal 5 on", "refusal 6 on", "refusal 7 on", "refusal 9 on"}) {
         require(output.find(unwanted) == string::npos, string("no line for ") + unwanted);
      }
      require(output.find("longForColumnIndex") != string::npos, "the line names the operation");
      require(output.find("unit " + to_string(UnitStateAccess::of(db)->serial()) + ",") != string::npos, "and the unit");
   }
}

void TestUnitClaims::test_refusals_are_silent_when_logging_is_off() {
   TEST_CASE("test_refusals_are_silent_when_logging_is_off");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallback(db, callback), "a SQL function");
   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a sibling");
   unique_ptr<SQLiteResultSet> stepped(openCursor(db, "select cb() as probe, id from items order by id"));
   require(stepped != nullptr, "a cursor to step");

   callback.body = [&] {
      for (int i = 0; i < 5; ++i) {
         sibling->longForColumnIndex(0);
      }
      unique_ptr<DBResultSet> nested(db.executeQuery("select 1"));
   };
   bool captured = false;
   const string output = captureStdout([&] { stepped->next(); }, captured);
   requireLongEquals(6, (long) db.refusedCalls(), "the refusals are counted all the same");
   if (captured) {
      require(output.empty(), "and nothing is written (output: '" + output + "')");
   }
}

void TestUnitClaims::test_a_cursor_outlives_its_database_and_still_claims() {
   TEST_CASE("test_a_cursor_outlives_its_database_and_still_claims");
   unique_ptr<SQLiteResultSet> cursor;
   shared_ptr<UnitState> unit;
   {
      SQLiteDatabase db(":memory:");
      db.setLogsErrors(false);
      require(db.open() && createItems(db), "open and fill");
      cursor.reset(openCursor(db, ROW_QUERY));
      require(cursor && cursor->next(), "a live cursor");
      unit = UnitStateAccess::of(*cursor);
   }
   require(cursor->status() == DBResultSetStatus::Invalidated, "the database is gone");

   {
      // somebody holds the unit (here: the test, as a stand-in for a call that is running)
      UnitClaim holder(unit.get(), "test holder");
      require(holder.ok(), "the holder has the claim");
      const uint64_t before = Database::refusedCallsOnThisThread();
      require(!cursor->next() && cursor->longForColumnIndex(0) == 0, "calls on the cursor are refused");
      cursor->close();
      requireLongEquals(3, (long) unit->refusedCalls(), "and counted on the unit, which the cursor still reaches");
      requireLongEquals(3, (long) (Database::refusedCallsOnThisThread() - before), "and on the thread");
      require(cursor->status() == DBResultSetStatus::Invalidated, "the cursor is as it was");
   }

   require(!cursor->next(), "afterwards it is used as before (and has nothing to give)");
   requireLongEquals(3, (long) unit->refusedCalls(), "without another refusal");
   cursor.reset();
   require(unit.use_count() == 1, "the cursor let go of the unit state");
}

void TestUnitClaims::test_sequential_transaction_interleaving_is_not_detected() {
   TEST_CASE("test_sequential_transaction_interleaving_is_not_detected");
   // The claim protects each CALL, not a transaction that spans calls: one thread
   // may begin a transaction and, after the unit has been handed over (idle, with the
   // synchronization the caller provides: here a join), another thread may carry it
   // on. Nothing is refused, and nothing could be: that is the supported hand-off.
   // What two callers do to one transaction between calls is the application's.
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   const uint64_t unitBefore = db.refusedCalls();

   require(db.beginTransaction(), "thread 1 begins a transaction");
   require(exec(db, "update items set qty = 1 where id = 1"), "and updates");

   bool committed = false;
   bool stillIn = false;
   thread other([&] {
      stillIn = db.inTransaction();
      exec(db, "update items set qty = 2 where id = 2");
      committed = db.commit();
   });
   other.join();

   require(stillIn, "thread 2 finds the transaction open");
   require(committed, "and commits it, with the first thread's update inside");
   requireLongEquals(0, (long) (db.refusedCalls() - unitBefore), "nothing was refused");
   unique_ptr<SQLiteResultSet> rs(openCursor(db, "select sum(qty) from items where id in (1, 2)"));
   require(rs && rs->next() && rs->longForColumnIndex(0) == 3, "both updates are in");
}

// ---- H1a-12g final hardening: a call refused from inside an outer call -----------------------------
//
// An outer call (a query, an update, a step, a commit) runs a callback of SQLite's; the
// callback calls the unit again, on the same thread. Those calls are refused. The outer call
// goes on according to SQLite's semantics, and when it returns, the error snapshot is ITS
// result: None if it succeeded, SQLite's own error if it failed -- never the InUse that a
// refused call reported meanwhile. Cursors report their own outcome the same way.

void TestUnitClaims::NestedCalls::run(SQLiteDatabase& db, SQLiteResultSet* sibling) {
   update = db.executeUpdate("insert into items(id, name, qty) values (99, 'nested', 1)", rows);
   query = db.executeQuery("select 99");
   kindInside = db.lastErrorKind();
   cursor = (sibling != nullptr) ? sibling->longForColumnIndex(0) : 0;
}

void TestUnitClaims::requireNestedCallsRefused(const NestedCalls& seen, SQLiteDatabase& db,
                                               uint64_t unitBefore, uint64_t threadBefore,
                                               const string& scenario) {
   require(!seen.update && seen.rows == 0, scenario + ": the nested update was refused, its count cleared");
   require(seen.query == nullptr, scenario + ": the nested query was refused");
   requireLongEquals(0, seen.cursor, scenario + ": the nested cursor read was refused (the row it would have read is 1)");
   require(seen.kindInside == DBErrorKind::InUse,
           scenario + ": while the outer call was running, the refused database call reported InUse, as ever");
   requireLongEquals(NESTED_REFUSALS, (long) (db.refusedCalls() - unitBefore), scenario + ": counted on the unit");
   requireLongEquals(NESTED_REFUSALS, (long) (Database::refusedCallsOnThisThread() - threadBefore), scenario + ": and on the thread");
   require(!db.inUse(), scenario + ": the unit is idle afterwards");

   // the refused update did not run
   unique_ptr<SQLiteResultSet> check(openCursor(db, "select count(*) from items where id = 99"));
   require(check && check->next() && check->longForColumnIndex(0) == 0, scenario + ": the nested insert did not run");
}

void TestUnitClaims::test_nested_refusal_outer_query_succeeds() {
   TEST_CASE("test_nested_refusal_outer_query_succeeds");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a sibling on its first row");

   for (int overload = 0; overload < 2; ++overload) {
      const string scenario = overload == 0 ? "executeQuery(sql)" : "executeQuery(sql, args)";
      Authorizer authorizer;
      NestedCalls seen;
      authorizer.body = [&] { seen.run(db, sibling.get()); };
      installAuthorizer(db, authorizer);
      const uint64_t unitBefore = db.refusedCalls();
      const uint64_t threadBefore = Database::refusedCallsOnThisThread();

      unique_ptr<DBResultSet> result;
      if (overload == 0) {
         result.reset(db.executeQuery("select id from items where id = 2 order by id"));
      } else {
         DBStatementArgs args;
         args.add(new DBLong(3));
         result.reset(db.executeQuery("select id from items where id = ? order by id", args));
      }
      removeAuthorizer(db);

      require(authorizer.calls >= 1, scenario + ": the authorizer ran inside the query");
      require(result != nullptr, scenario + ": the outer query succeeded");
      require(db.lastErrorKind() == DBErrorKind::None, scenario + ": a successful query leaves no error (not the InUse of the nested call)");
      requireLongEquals(0, db.lastErrorCode(), scenario + ": with no code");
      require(db.lastErrorMessage().empty(), scenario + ": and no message");
      require(!db.hadError(), scenario + ": hadError() is false");
      SQLiteResultSet* cursor = dynamic_cast<SQLiteResultSet*>(result.get());
      require(cursor != nullptr && cursor->next(), scenario + ": its cursor steps");
      requireLongEquals(overload == 0 ? 2 : 3, cursor->longForColumnIndex(0), scenario + ": to the right row");
      require(cursor->status() == DBResultSetStatus::Row, scenario + ": status Row");
      require(sibling->status() == DBResultSetStatus::Row, scenario + ": the sibling is untouched");
      requireNestedCallsRefused(seen, db, unitBefore, threadBefore, scenario);
   }
}

void TestUnitClaims::test_nested_refusal_outer_query_fails() {
   TEST_CASE("test_nested_refusal_outer_query_fails");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a sibling on its first row");

   for (int overload = 0; overload < 2; ++overload) {
      const string scenario = overload == 0 ? "executeQuery(sql)" : "executeQuery(sql, args)";
      Authorizer authorizer;
      authorizer.verdict = SQLITE_DENY;
      NestedCalls seen;
      authorizer.body = [&] { seen.run(db, sibling.get()); };
      installAuthorizer(db, authorizer);
      const uint64_t unitBefore = db.refusedCalls();
      const uint64_t threadBefore = Database::refusedCallsOnThisThread();

      unique_ptr<DBResultSet> result;
      if (overload == 0) {
         result.reset(db.executeQuery("select id from items where id = 2 order by id"));
      } else {
         DBStatementArgs args;
         args.add(new DBLong(3));
         result.reset(db.executeQuery("select id from items where id = ? order by id", args));
      }
      removeAuthorizer(db);

      require(authorizer.calls >= 1, scenario + ": the authorizer ran inside the query");
      require(result == nullptr, scenario + ": the outer query failed");
      require(db.lastErrorKind() == DBErrorKind::Sqlite, scenario + ": its error is SQLite's own, not InUse");
      requireLongEquals(SQLITE_AUTH, db.lastErrorCode(), scenario + ": with SQLite's code");
      require(db.lastErrorMessage().find("not authorized") != string::npos, scenario + ": and SQLite's message: " + db.lastErrorMessage());
      require(sibling->status() == DBResultSetStatus::Row, scenario + ": the sibling is untouched");
      requireNestedCallsRefused(seen, db, unitBefore, threadBefore, scenario);
   }
}

void TestUnitClaims::test_nested_refusal_outer_update_succeeds() {
   TEST_CASE("test_nested_refusal_outer_update_succeeds");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallbacks(db, callback), "SQL functions");
   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a sibling on its first row");

   NestedCalls seen;
   callback.body = [&] { seen.run(db, sibling.get()); };
   const uint64_t unitBefore = db.refusedCalls();
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();

   unsigned long rows = 0;
   require(db.executeUpdate("update items set qty = cb() where id = 2", rows), "the outer update succeeds");
   requireLongEquals(1, (long) rows, "it changed one row");
   require(callback.calls >= 1, "the function ran inside it");
   require(db.lastErrorKind() == DBErrorKind::None, "a successful update leaves no error (not the InUse of the nested call)");
   requireLongEquals(0, db.lastErrorCode(), "with no code");
   require(db.lastErrorMessage().empty(), "and no message");
   require(!db.hadError(), "hadError() is false");
   require(sibling->status() == DBResultSetStatus::Row, "the sibling is untouched");
   requireNestedCallsRefused(seen, db, unitBefore, threadBefore, "executeUpdate");
   unique_ptr<SQLiteResultSet> rs(openCursor(db, "select qty from items where id = 2"));
   require(rs && rs->next() && rs->longForColumnIndex(0) == 1, "the outer update did its work");
}

void TestUnitClaims::test_nested_refusal_outer_update_fails() {
   TEST_CASE("test_nested_refusal_outer_update_fails");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallbacks(db, callback), "SQL functions");
   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a sibling on its first row");

   NestedCalls seen;
   callback.body = [&] { seen.run(db, sibling.get()); };
   const uint64_t unitBefore = db.refusedCalls();
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();

   unsigned long rows = 77;
   require(!db.executeUpdate("update items set qty = cbfail() where id = 2", rows), "the outer update fails");
   requireLongEquals(0, (long) rows, "with no rows counted");
   require(callback.calls >= 1, "the function ran inside it");
   require(db.lastErrorKind() == DBErrorKind::Sqlite, "its error is SQLite's own, not InUse");
   requireLongEquals(SQLITE_ERROR, db.lastErrorCode(), "with SQLite's code");
   require(db.lastErrorMessage().find("cbfail raised") != string::npos, "and the message the function raised: " + db.lastErrorMessage());
   require(sibling->status() == DBResultSetStatus::Row, "the sibling is untouched");
   requireNestedCallsRefused(seen, db, unitBefore, threadBefore, "executeUpdate");
   unique_ptr<SQLiteResultSet> rs(openCursor(db, "select qty from items where id = 2"));
   require(rs && rs->next() && rs->longForColumnIndex(0) == 20, "the failed update changed nothing");
}

void TestUnitClaims::test_nested_refusal_outer_next_succeeds() {
   TEST_CASE("test_nested_refusal_outer_next_succeeds");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallbacks(db, callback), "SQL functions");
   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a sibling on its first row");
   unique_ptr<SQLiteResultSet> stepped(openCursor(db, "select cb() as probe, id from items order by id"));
   require(stepped != nullptr, "the cursor to step");
   require(db.lastErrorKind() == DBErrorKind::None, "no error before the step");

   NestedCalls seen;
   callback.body = [&] { seen.run(db, sibling.get()); };
   const uint64_t unitBefore = db.refusedCalls();
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();

   require(stepped->next(), "the outer step succeeds");
   require(callback.calls >= 1, "the function ran inside it");
   require(stepped->status() == DBResultSetStatus::Row, "the cursor is on a row, not in Error");
   requireLongEquals(0, stepped->lastErrorCode(), "with no error code of its own");
   require(stepped->lastErrorMessage().empty(), "and no message");
   requireLongEquals(1, stepped->longForColumnIndex(1), "on the first row");
   require(db.lastErrorKind() == DBErrorKind::None, "the step left no error behind (not the InUse of the nested call)");
   requireLongEquals(0, db.lastErrorCode(), "with no code");
   require(db.lastErrorMessage().empty(), "and no message");
   require(!db.hadError(), "hadError() is false");
   require(sibling->status() == DBResultSetStatus::Row, "the sibling is untouched");
   requireNestedCallsRefused(seen, db, unitBefore, threadBefore, "next");
   require(stepped->next() && stepped->longForColumnIndex(1) == 2, "and the cursor goes on");
}

void TestUnitClaims::test_nested_refusal_outer_next_fails() {
   TEST_CASE("test_nested_refusal_outer_next_fails");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallbacks(db, callback), "SQL functions");
   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a sibling on its first row");
   unique_ptr<SQLiteResultSet> stepped(openCursor(db, "select cbfail() as probe, id from items order by id"));
   require(stepped != nullptr, "the cursor to step");

   NestedCalls seen;
   callback.body = [&] { seen.run(db, sibling.get()); };
   const uint64_t unitBefore = db.refusedCalls();
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();

   require(!stepped->next(), "the outer step fails");
   require(callback.calls >= 1, "the function ran inside it");
   require(stepped->status() == DBResultSetStatus::Error, "the cursor is in Error");
   requireLongEquals(SQLITE_ERROR, stepped->lastErrorCode(), "with SQLite's code, not a refusal's");
   require(stepped->lastErrorMessage().find("cbfail raised") != string::npos, "and the message the function raised: " + stepped->lastErrorMessage());
   require(db.lastErrorKind() == DBErrorKind::Sqlite, "the database's snapshot is the failure of the step, not InUse");
   requireLongEquals(SQLITE_ERROR, db.lastErrorCode(), "with the same code");
   require(db.lastErrorMessage().find("cbfail raised") != string::npos, "and message");
   require(sibling->status() == DBResultSetStatus::Row, "the sibling is untouched");
   requireNestedCallsRefused(seen, db, unitBefore, threadBefore, "next");
}

void TestUnitClaims::test_nested_refusal_outer_transaction_calls() {
   TEST_CASE("test_nested_refusal_outer_transaction_calls");
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a sibling on its first row");

   for (int outcome = 0; outcome < 2; ++outcome) {
      const bool succeeds = (outcome == 0);
      const string scenario = succeeds ? "commit() that succeeds" : "commit() that fails";
      require(db.beginTransaction(), scenario + ": a transaction");
      require(exec(db, "update items set qty = qty + 1 where id = 3"), scenario + ": with a change in it");

      CommitHook hook;
      hook.answer = succeeds ? 0 : 1;   // non-zero: SQLite turns the commit into a rollback
      NestedCalls seen;
      hook.body = [&] { seen.run(db, sibling.get()); };
      g_commitHook = &hook;
      sqlite3_commit_hook(db.sqliteHandle(), commitHookFunction, nullptr);
      const uint64_t unitBefore = db.refusedCalls();
      const uint64_t threadBefore = Database::refusedCallsOnThisThread();

      const bool committed = db.commit();
      sqlite3_commit_hook(db.sqliteHandle(), nullptr, nullptr);
      g_commitHook = nullptr;

      require(hook.calls >= 1, scenario + ": the hook ran inside it");
      require(committed == succeeds, scenario + ": the outer commit did what SQLite decided");
      if (succeeds) {
         require(db.lastErrorKind() == DBErrorKind::None, scenario + ": no error is left (not the InUse of the nested call)");
         require(!db.hadError(), scenario + ": hadError() is false");
      } else {
         require(db.lastErrorKind() == DBErrorKind::Sqlite, scenario + ": the error is SQLite's own, not InUse");
         requireLongEquals(SQLITE_CONSTRAINT, db.lastErrorCode() & 0xff, scenario + ": with SQLite's code");
      }
      require(sibling->status() == DBResultSetStatus::Row, scenario + ": the sibling is untouched");
      requireNestedCallsRefused(seen, db, unitBefore, threadBefore, scenario);
   }
}

void TestUnitClaims::test_nested_refusal_does_not_hide_a_failure_of_an_earlier_call() {
   TEST_CASE("test_nested_refusal_does_not_hide_a_failure_of_an_earlier_call");
   // Stepping a cursor does not replace the database's snapshot (see
   // test_every_cursor_entry_point_refuses_inside_a_call). A step that succeeds while a
   // refused call went by therefore leaves the snapshot as it was BEFORE the step -- an
   // earlier, unrelated failure included -- and neither the InUse of the refused call nor
   // a clean slate.
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallbacks(db, callback), "SQL functions");
   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a sibling on its first row");
   unique_ptr<SQLiteResultSet> stepped(openCursor(db, "select cb() as probe, id from items order by id"));
   require(stepped != nullptr, "the cursor to step");

   require(db.executeQuery("select * from no_such_table") == nullptr, "an unrelated failure");
   const DBErrorKind kind = db.lastErrorKind();
   const int code = db.lastErrorCode();
   const string message = db.lastErrorMessage();
   require(kind == DBErrorKind::Sqlite && message.find("no_such_table") != string::npos, "is on the snapshot");

   NestedCalls seen;
   callback.body = [&] { seen.run(db, sibling.get()); };
   const uint64_t unitBefore = db.refusedCalls();
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();

   require(stepped->next(), "the step succeeds");
   require(db.lastErrorKind() == kind && db.lastErrorCode() == code && db.lastErrorMessage() == message,
           "the snapshot is the earlier failure, exactly: not InUse, not cleared");
   require(stepped->status() == DBResultSetStatus::Row, "the cursor is on a row");
   requireNestedCallsRefused(seen, db, unitBefore, threadBefore, "next");
}

void TestUnitClaims::test_nested_refusal_outer_good_connection_succeeds() {
   TEST_CASE("test_nested_refusal_outer_good_connection_succeeds");
   // goodConnection() runs a query of its own (through the same code as executeQuery)
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   unique_ptr<SQLiteResultSet> sibling(openCursor(db, ROW_QUERY));
   require(sibling && sibling->next(), "a sibling on its first row");

   Authorizer authorizer;
   NestedCalls seen;
   authorizer.body = [&] { seen.run(db, sibling.get()); };
   installAuthorizer(db, authorizer);
   const uint64_t unitBefore = db.refusedCalls();
   const uint64_t threadBefore = Database::refusedCallsOnThisThread();

   const bool good = db.goodConnection();
   removeAuthorizer(db);

   require(authorizer.calls >= 1, "the authorizer ran inside the check");
   require(good, "the connection is good");
   require(db.lastErrorKind() == DBErrorKind::None, "and the check left no error (not the InUse of the nested call)");
   require(!db.hadError(), "hadError() is false");
   requireNestedCallsRefused(seen, db, unitBefore, threadBefore, "goodConnection");
}

void TestUnitClaims::test_a_note_of_a_nested_refusal_does_not_leak_into_a_later_call() {
   TEST_CASE("test_a_note_of_a_nested_refusal_does_not_leak_into_a_later_call");
   // An update takes no NestedRefusalScope (its own success clears the snapshot and its own
   // failure writes it), so the note of a refusal made inside it is never settled. It must
   // not be applied by a LATER call: here a query that fails with a real InUse (the
   // database's in-use marker, set by the application as the compatibility test does)
   // must keep that error.
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   Callback callback;
   require(installCallbacks(db, callback), "SQL functions");
   callback.body = [&] {
      NestedCalls nested;
      nested.run(db, nullptr);
   };
   unsigned long rows = 0;
   require(db.executeUpdate("update items set qty = cb() where id = 1", rows), "an update with a refusal inside it");
   require(db.lastErrorKind() == DBErrorKind::None, "which succeeds, and leaves no error");

   db.setInUse(true);
   DBResultSet* refused = db.executeQuery("select 1");
   db.setInUse(false);
   require(refused == nullptr, "the query is refused by the in-use marker");
   require(db.lastErrorKind() == DBErrorKind::InUse, "with InUse, which is the call's own error and stays");
   unique_ptr<DBResultSet> after(db.executeQuery("select 2"));
   require(after != nullptr && db.lastErrorKind() == DBErrorKind::None, "and the next query is clean");
}

void TestUnitClaims::test_cross_thread_refusals_never_touch_the_error_snapshot() {
   TEST_CASE("test_cross_thread_refusals_never_touch_the_error_snapshot");
   // The rule that the nested-refusal handling must not disturb: a database call refused
   // because ANOTHER thread is inside the unit reports nothing to the snapshot -- not InUse,
   // not a clear. Only a call from inside a call of its own thread (re-entrance) reports InUse.
   // The test thread holds the claim as a stand-in for a call that is running; every database
   // entry point is then called from a second thread.
   SQLiteDatabase db(":memory:");
   db.setLogsErrors(false);
   require(db.open() && createItems(db), "open and fill");
   require(db.executeQuery("select * from no_such_table") == nullptr, "an earlier failure");
   const DBErrorKind kind = db.lastErrorKind();
   const int code = db.lastErrorCode();
   const string message = db.lastErrorMessage();
   require(kind == DBErrorKind::Sqlite && message.find("no_such_table") != string::npos, "is on the snapshot");

   shared_ptr<UnitState> unit = UnitStateAccess::of(db);
   const uint64_t unitBefore = db.refusedCalls();
   long threadRefusals = -1;
   bool anyGranted = false;
   DBErrorKind kindInside = DBErrorKind::None;
   {
      UnitClaim holder(unit.get(), "test holder");
      require(holder.ok(), "the stand-in holds the claim");
      thread other([&] {
         const uint64_t threadBefore = Database::refusedCallsOnThisThread();
         unsigned long rows = 99;
         DBStatementArgs args;
         args.add(new DBLong(1));
         bool granted = false;
         granted = db.open() || granted;
         db.close();
         granted = db.goodConnection() || granted;
         DBResultSet* q1 = db.executeQuery("select 1");
         DBResultSet* q2 = db.executeQuery("select ?", args);
         granted = (q1 != nullptr) || (q2 != nullptr) || granted;
         granted = db.executeUpdate("update items set qty = 1", rows) || granted;
         granted = db.executeUpdate("update items set qty = ?", args, rows) || granted;
         granted = db.beginTransaction() || db.beginDeferredTransaction() || db.commit() || db.rollback() || granted;
         granted = db.inTransaction() || granted;
         granted = (db.lastInsertRowId() != -1) || granted;
         db.setBusyRetryTimeout(1234);
         db.setShouldCacheStatements(false);
         db.setStatementCacheLimit(1);
         db.clearCachedStatements();
         granted = (db.cachedStatementForQuery("update items set qty = qty") != nullptr) || granted;
         kindInside = db.lastErrorKind();
         anyGranted = granted;
         threadRefusals = (long) (Database::refusedCallsOnThisThread() - threadBefore);
      });
      other.join();
   }

   require(!anyGranted, "no call of the other thread was granted or returned anything");
   requireLongEquals(18, threadRefusals, "all 18 were refused and counted on that thread");
   requireLongEquals(18, (long) (db.refusedCalls() - unitBefore), "and on the unit");
   require(kindInside == kind, "the other thread saw the earlier failure, not InUse");
   require(db.lastErrorKind() == kind && db.lastErrorCode() == code && db.lastErrorMessage() == message,
           "and the snapshot is exactly as it was: a cross-thread refusal reports nothing");
   require(db.busyRetryTimeout() != 1234 && db.shouldCacheStatements(), "the setters did nothing");
}
