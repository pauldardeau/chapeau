#include "test_sqlite_foundation.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "scoped_temp_dir.h"
#include "SQLiteDatabase.h"
#include "SQLiteResultSet.h"
#include "DBStatementArgs.h"
#include "DBBool.h"
#include "DBData.h"
#include "DBDate.h"
#include "DBDouble.h"
#include "DBFloat.h"
#include "DBInt.h"
#include "DBLong.h"
#include "DBNull.h"
#include "DBString.h"

using namespace std;
using namespace chapeau;

namespace fs = std::filesystem;


namespace {

const char* const ITEMS_SCHEMA =
   "create table items("
   "id integer primary key, name text not null, qty integer, price real)";

// three rows whose prices are exactly representable as doubles, so values
// can be compared exactly
const char* const ITEMS_ROWS[] = {
   "insert into items(id,name,qty,price) values (1,'apple',3,0.5)",
   "insert into items(id,name,qty,price) values (2,'banana',12,1.25)",
   "insert into items(id,name,qty,price) values (3,'cherry',0,2.75)"
};

bool exec(SQLiteDatabase& db, const string& sql) {
   unsigned long rowsAffected = 0;
   return db.executeUpdate(sql, rowsAffected);
}

bool createItems(SQLiteDatabase& db) {
   if (!exec(db, ITEMS_SCHEMA)) {
      return false;
   }
   for (const char* sql : ITEMS_ROWS) {
      if (!exec(db, sql)) {
         return false;
      }
   }
   return true;
}

// first column of the first row, or 'missing' if there is no such row
long long scalar(SQLiteDatabase& db, const string& sql, long long missing = -1) {
   unique_ptr<DBResultSet> rs(db.executeQuery(sql));
   if (rs && rs->next()) {
      return rs->longForColumnIndex(0);
   }
   return missing;
}

// the row count of 'table' as seen by a brand-new connection to the same file
long long countFromNewConnection(const string& path, const string& table) {
   SQLiteDatabase other(path);
   if (!other.open()) {
      return -2;
   }
   const long long count = scalar(other, "select count(*) from " + table);
   other.close();
   return count;
}

}


TestSQLiteFoundation::TestSQLiteFoundation() :
   poivre::TestSuite("TestSQLiteFoundation") {
}

void TestSQLiteFoundation::runTests() {
   test_scoped_temp_dir();
   test_open_close();
   test_failed_open();
   test_create_insert_update_delete();
   test_query_values();
   test_parameter_binding();
   test_zero_row_results();
   test_transactions();
   test_constraint_error_recovery();
   test_cursor_after_database_close();
   test_cursor_after_database_destruction();
   test_cursor_with_cache_disabled();
   test_cursor_close_and_destroy_repeated();
   test_cursor_invalid_after_reopen();
   test_temporary_text_arguments();
   test_temporary_blob_arguments();
   test_two_cursors_identical_sql();
   test_two_cursors_parameterized_sql();
   test_close_one_cursor_while_another_is_active();
   test_statement_reuse_after_cursor_destroyed();
   test_incorrect_argument_counts();
   test_arguments_then_none_then_arguments();
   test_cursor_exhaustion_is_terminal();
   test_cache_disabled_with_several_active_cursors();
   test_update_while_cursor_owns_same_sql();
   test_schema_change_query();
   test_schema_change_update();
   test_schema_change_repeated();
   test_schema_change_then_constraint_error();
   test_schema_change_with_independent_cursors();
   test_busy_timeout_configuration();
   test_result_set_status_transitions();
   test_close_with_unfinalized_statement();
   test_transaction_state_follows_sqlite();
   test_open_when_already_open();
   test_failed_open_recovers();
   test_statement_cache_is_bounded();
   test_statement_cache_limit_zero();
   test_statement_cache_limit_one();
   test_statement_cache_lru_order();
   test_statement_cache_eviction_with_active_cursors();
   test_statement_cache_lowering_limit_with_active_cursors();
   test_statement_cache_cleanup_on_close();
   test_text_with_nul_bytes();
   test_null_empty_and_nonempty_blobs();
   test_null_dates();
   test_integer_accessors();
   test_boolean_conversion_of_wide_integers();
   test_float_binding_is_exact_promotion();
   test_legacy_result_set_subclass_still_works();
   test_execute_update_refuses_row_returning_statements();
   test_bind_failure_fails_the_call();
   test_argument_count_mismatch_is_reported();
   test_errors_that_are_not_sqlite_errors();
   test_last_error_lifetime();
   test_open_failure_is_recorded();
   test_cursor_error_message();
   test_query_without_result_columns_runs_before_it_returns();
   test_query_with_result_columns_stays_lazy();
   test_query_without_result_columns_that_fails();
   test_query_without_result_columns_reentrancy();
   test_statement_boundary_refusals();
   test_statement_boundary_accepts();
   test_statement_boundary_and_the_statement_cache();
   test_statement_boundary_with_transactions_and_cursors();
   test_error_kind_names();
   test_open_with_a_threadsafe_library();
}

void TestSQLiteFoundation::requireLongEquals(long long expected,
                                             long long actual,
                                             const string& description) {
   require(expected == actual,
           description + " (expected " + to_string(expected) +
           ", got " + to_string(actual) + ")");
}

void TestSQLiteFoundation::requireDoubleEquals(double expected,
                                               double actual,
                                               const string& description) {
   require(expected == actual,
           description + " (expected " + to_string(expected) +
           ", got " + to_string(actual) + ")");
}

void TestSQLiteFoundation::test_scoped_temp_dir() {
   TEST_CASE("test_scoped_temp_dir");
   fs::path first_path;
   fs::path second_path;
   {
      chapeau_tests::ScopedTempDir first;
      chapeau_tests::ScopedTempDir second;
      first_path = first.path();
      second_path = second.path();

      require(first_path != second_path, "each directory gets its own name");
      require(fs::is_directory(first_path), "first directory exists");
      require(fs::is_directory(second_path), "second directory exists");
      require(first_path.parent_path() == fs::temp_directory_path(),
              "directories are created in the system temporary directory");

      ofstream(first.file("inside.txt")) << "x";
      require(fs::exists(first_path / "inside.txt"), "file created in directory");

      require(first.remove(), "remove() reports success");
      require(!fs::exists(first_path), "removed directory and its contents are gone");
      require(fs::is_directory(second_path), "removing one directory leaves the other");
   }
   require(!fs::exists(second_path), "destructor removes the directory");
}

void TestSQLiteFoundation::test_open_close() {
   TEST_CASE("test_open_close");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("open_close.sqlite3");

   {
      SQLiteDatabase db(path);
      require(db.sqliteHandle() == nullptr, "no connection before open()");
      require(db.open(), "open() creates a new database file");
      require(db.sqliteHandle() != nullptr, "connection exists after open()");
      require(db.goodConnection(), "connection is good after open()");
      require(exec(db, "create table t(a integer)"), "create table");
      require(exec(db, "insert into t(a) values (7)"), "insert a row");
      db.close();
      require(db.sqliteHandle() == nullptr, "no connection after close()");
   }
   require(fs::exists(path), "database file exists after close()");

   {
      SQLiteDatabase db(path);
      require(db.open(), "open() an existing database");
      requireLongEquals(7, scalar(db, "select a from t"),
                        "row written before close() is there after reopening");
      db.close();
   }
}

void TestSQLiteFoundation::test_failed_open() {
   TEST_CASE("test_failed_open");
   chapeau_tests::ScopedTempDir dir;

   // the directory does not exist, so the database file cannot be created
   SQLiteDatabase db(dir.file("no_such_directory/x.sqlite3"));
   db.setLogsErrors(false);
   require(!db.open(), "open() fails when the file cannot be created");
   require(!exec(db, "create table z(a integer)"),
           "executeUpdate fails cleanly on a database that did not open");
}

void TestSQLiteFoundation::test_create_insert_update_delete() {
   TEST_CASE("test_create_insert_update_delete");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("crud.sqlite3"));
   require(db.open(), "open");
   require(exec(db, "create table log(id integer primary key, msg text)"),
           "create table");

   unsigned long rowsAffected = 99;
   require(db.executeUpdate("insert into log(msg) values ('one')", rowsAffected),
           "first insert");
   requireLongEquals(1, rowsAffected, "insert affects one row");
   requireLongEquals(1, db.lastInsertRowId(),
                     "rowid of the first row in an empty table is 1");

   require(db.executeUpdate("insert into log(msg) values ('two')", rowsAffected),
           "second insert");
   requireLongEquals(2, db.lastInsertRowId(), "rowid of the second row is 2");

   require(db.executeUpdate("insert into log(id,msg) values (100,'three')", rowsAffected),
           "insert with an explicit id");
   requireLongEquals(100, db.lastInsertRowId(), "rowid is the explicit id");
   requireLongEquals(3, scalar(db, "select count(*) from log"), "three rows");

   require(db.executeUpdate("update log set msg = 'changed' where id <= 2", rowsAffected),
           "update two rows");
   requireLongEquals(2, rowsAffected, "update reports two affected rows");
   requireLongEquals(2, scalar(db, "select count(*) from log where msg = 'changed'"),
                     "two rows carry the new value");

   require(db.executeUpdate("delete from log where id = 100", rowsAffected),
           "delete one row");
   requireLongEquals(1, rowsAffected, "delete reports one affected row");
   requireLongEquals(2, scalar(db, "select count(*) from log"), "two rows remain");
   db.close();
}

void TestSQLiteFoundation::test_query_values() {
   TEST_CASE("test_query_values");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("query.sqlite3"));
   require(db.open(), "open");
   require(createItems(db), "create and fill the items table");

   struct Expected {
      long long id;
      const char* name;
      long long qty;
      double price;
   };
   const Expected expected[] = {
      {1, "apple", 3, 0.5},
      {2, "banana", 12, 1.25},
      {3, "cherry", 0, 2.75}
   };
   const int expectedRows = 3;

   // the same checks with the statement cache on and off
   for (int pass = 0; pass < 2; ++pass) {
      db.setShouldCacheStatements(pass == 0);

      unique_ptr<DBResultSet> rs(
         db.executeQuery("select id, name, qty, price from items order by id"));
      require(rs != nullptr, "executeQuery returns a result set");
      if (!rs) {
         continue;
      }
      requireLongEquals(4, rs->getColumnCount(), "column count");
      const SQLiteResultSet* sqliteRs = dynamic_cast<SQLiteResultSet*>(rs.get());
      require(sqliteRs != nullptr, "SQLite result set");
      if (sqliteRs) {
         requireLongEquals(0, sqliteRs->columnIndexForName("id"), "index of column id");
         requireLongEquals(3, sqliteRs->columnIndexForName("price"), "index of column price");
      }

      int row = 0;
      while (row < expectedRows && rs->next()) {
         const Expected& e = expected[row];
         requireLongEquals(e.id, rs->intForColumnIndex(0), "id by index");
         string name;
         require(rs->stringForColumnIndex(1, name), "name is not NULL");
         requireStringEquals(e.name, name, "name by index");
         requireLongEquals(e.qty, rs->longForColumnIndex(2), "qty by index");
         requireDoubleEquals(e.price, rs->doubleForColumnIndex(3), "price by index");
         ++row;
      }
      requireLongEquals(expectedRows, row, "all rows were returned");
      require(!rs->next(), "no row beyond the last one");
   }
   db.setShouldCacheStatements(true);

   {
      unique_ptr<DBResultSet> rs(
         db.executeQuery("select name, qty, price from items where id = 2"));
      require(rs && rs->next(), "row for id 2");
      if (rs) {
         string name;
         require(rs->stringForColumn("name", name), "name by column name");
         requireStringEquals("banana", name, "name by column name");
         requireLongEquals(12, rs->longForColumn("qty"), "qty by column name");
         requireLongEquals(12, rs->intForColumn("qty"), "qty as int by column name");
         requireDoubleEquals(1.25, rs->doubleForColumn("price"), "price by column name");
         require(rs->boolForColumn("qty"), "non-zero integer reads as true");
      }
   }
   {
      // ordering comes from the query, not from storage order
      unique_ptr<DBResultSet> rs(
         db.executeQuery("select name from items order by name desc"));
      require(rs != nullptr, "descending query");
      const char* const names[] = {"cherry", "banana", "apple"};
      int row = 0;
      while (rs && row < 3 && rs->next()) {
         string name;
         rs->stringForColumnIndex(0, name);
         requireStringEquals(names[row], name, "descending name order");
         ++row;
      }
      requireLongEquals(3, row, "three names");
   }
   {
      unique_ptr<DBResultSet> rs(
         db.executeQuery("select count(*), sum(qty), max(price) from items"));
      require(rs && rs->next(), "aggregate row");
      if (rs) {
         requireLongEquals(3, rs->longForColumnIndex(0), "count(*)");
         requireLongEquals(15, rs->longForColumnIndex(1), "sum(qty)");
         requireDoubleEquals(2.75, rs->doubleForColumnIndex(2), "max(price)");
      }
   }
   db.close();
}

void TestSQLiteFoundation::test_parameter_binding() {
   TEST_CASE("test_parameter_binding");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("binding.sqlite3"));
   require(db.open(), "open");
   require(createItems(db), "create and fill the items table");

   // one SQL text run repeatedly with different parameters, cache on and off
   for (int pass = 0; pass < 2; ++pass) {
      db.setShouldCacheStatements(pass == 0);
      const struct { long id; const char* name; } lookups[] = {
         {3, "cherry"}, {1, "apple"}, {2, "banana"}, {3, "cherry"}
      };
      for (const auto& lookup : lookups) {
         // (the arguments need not outlive the result set; see
         // test_temporary_text_arguments)
         DBStatementArgs args;
         args.add(new DBLong(lookup.id));
         unique_ptr<DBResultSet> rs(
            db.executeQuery("select name from items where id = ?", args));
         require(rs && rs->next(), "a row for a bound id");
         if (rs) {
            string name;
            rs->stringForColumnIndex(0, name);
            requireStringEquals(lookup.name, name, "row matches the bound id");
         }
      }
   }
   db.setShouldCacheStatements(true);

   {
      // two placeholders are bound in order: qty > 1 and price < 2.0
      DBStatementArgs args;
      args.add(new DBLong(1));
      args.add(new DBDouble(2.0));
      unique_ptr<DBResultSet> rs(db.executeQuery(
         "select name from items where qty > ? and price < ? order by id", args));
      require(rs != nullptr, "query with two parameters");
      const char* const names[] = {"apple", "banana"};
      int row = 0;
      while (rs && row < 2 && rs->next()) {
         string name;
         rs->stringForColumnIndex(0, name);
         requireStringEquals(names[row], name, "rows selected by two parameters");
         ++row;
      }
      requireLongEquals(2, row, "apple and banana match, cherry (price 2.75) does not");
   }

   // every supported value type, stored through bound parameters
   require(exec(db, "create table typed(id integer primary key, big integer, "
                    "small integer, s text, d real, flag integer, bl blob, "
                    "ts real, n text)"),
           "create typed table");
   const unsigned char blobBytes[] = {0x00, 0x01, 0xFE, 0xFF, 0x00, 0x7F};
   const string utf8 = "h\xC3\xA9llo w\xC3\xB6rld \xE2\x9C\x93";
   const long long big = 4294967296LL;   // 2^32
   {
      DBStatementArgs args;
      args.add(new DBLong(1));
      args.add(new DBLong(big));
      args.add(new DBInt(INT_MIN));
      args.add(new DBString(utf8));
      args.add(new DBDouble(-0.125));
      args.add(new DBBool(true));
      args.add(new DBData(blobBytes, sizeof(blobBytes)));
      args.add(new DBDate(1700000000.0));
      args.add(new DBNull("Null"));
      unsigned long rowsAffected = 0;
      require(db.executeUpdate("insert into typed(id,big,small,s,d,flag,bl,ts,n) "
                               "values (?,?,?,?,?,?,?,?,?)", args, rowsAffected),
              "insert one value of every type");
      requireLongEquals(1, rowsAffected, "one row inserted");
   }
   {
      // the same statement again with other values (true -> false)
      DBStatementArgs args;
      args.add(new DBLong(2));
      args.add(new DBLong(-1));
      args.add(new DBInt(0));
      args.add(new DBString(""));
      args.add(new DBDouble(0.0));
      args.add(new DBBool(false));
      args.add(new DBData(blobBytes, 1));
      args.add(new DBDate(0.0));
      args.add(new DBNull("Null"));
      unsigned long rowsAffected = 0;
      require(db.executeUpdate("insert into typed(id,big,small,s,d,flag,bl,ts,n) "
                               "values (?,?,?,?,?,?,?,?,?)", args, rowsAffected),
              "insert a second row with different values");
   }

   // what SQLite stored, checked without going through Chapeau's read API
   requireLongEquals(1, scalar(db,
      "select count(*) from typed where id = 1 and big = 4294967296 "
      "and small = -2147483648 and d = -0.125 and flag = 1 "
      "and typeof(bl) = 'blob' and length(bl) = 6 and typeof(n) = 'null'"),
      "SQLite holds the values that were bound");
   requireLongEquals(1, scalar(db,
      "select count(*) from typed where id = 2 and big = -1 and small = 0 "
      "and s = '' and d = 0.0 and flag = 0 and length(bl) = 1"),
      "SQLite holds the second set of bound values");

   {
      unique_ptr<DBResultSet> rs(db.executeQuery(
         "select big, small, s, d, flag, bl, ts from typed where id = 1"));
      require(rs && rs->next(), "typed row 1");
      if (rs) {
         requireLongEquals(big, rs->longForColumnIndex(0), "64-bit integer round trip");
         requireLongEquals(INT_MIN, rs->intForColumnIndex(1), "INT_MIN round trip");
         string s;
         require(rs->stringForColumnIndex(2, s), "string is not NULL");
         requireStringEquals(utf8, s, "UTF-8 string round trip");
         requireDoubleEquals(-0.125, rs->doubleForColumnIndex(3), "double round trip");
         require(rs->boolForColumnIndex(4), "true round trip");

         unique_ptr<DBData> data(rs->dataForColumnIndex(5));
         require(data != nullptr, "blob read back");
         if (data) {
            requireLongEquals(6, data->length(), "blob length, including embedded NULs");
            require(::memcmp(data->value(), blobBytes, sizeof(blobBytes)) == 0,
                    "blob bytes round trip");
         }
         unique_ptr<DBDate> date(rs->dateForColumnIndex(6));
         require(date != nullptr, "date read back");
         if (date) {
            requireDoubleEquals(1700000000.0, date->timeIntervalSince1970(),
                                "date round trip");
         }
      }
   }
   {
      unique_ptr<DBResultSet> rs(
         db.executeQuery("select flag from typed where id = 2"));
      require(rs && rs->next(), "typed row 2");
      if (rs) {
         require(!rs->boolForColumnIndex(0), "false round trip");
      }
   }

   {
      // text that looks like SQL is data, not SQL
      const string hostile = "O'Brien\"); drop table items; --";
      DBStatementArgs args;
      args.add(new DBLong(4));
      args.add(new DBString(hostile));
      unsigned long rowsAffected = 0;
      require(db.executeUpdate("insert into items(id,name) values (?,?)", args, rowsAffected),
              "insert text containing quotes and SQL");
      requireLongEquals(4, scalar(db, "select count(*) from items"),
                        "the items table still exists with four rows");

      DBStatementArgs lookup;
      lookup.add(new DBLong(4));
      unique_ptr<DBResultSet> rs(
         db.executeQuery("select name from items where id = ?", lookup));
      require(rs && rs->next(), "hostile row");
      if (rs) {
         string name;
         rs->stringForColumnIndex(0, name);
         requireStringEquals(hostile, name, "text is stored exactly as given");
      }
   }
   db.close();
}

void TestSQLiteFoundation::test_zero_row_results() {
   TEST_CASE("test_zero_row_results");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("zero.sqlite3"));
   require(db.open(), "open");
   require(exec(db, ITEMS_SCHEMA), "create empty items table");

   {
      unique_ptr<DBResultSet> rs(db.executeQuery("select name from items"));
      require(rs != nullptr, "a query over an empty table still gives a result set");
      require(rs && !rs->next(), "no rows in an empty table");
   }
   for (const char* sql : ITEMS_ROWS) {
      require(exec(db, sql), "fill the items table");
   }
   {
      DBStatementArgs none;
      none.add(new DBLong(999));
      unique_ptr<DBResultSet> rs(
         db.executeQuery("select name from items where id = ?", none));
      require(rs != nullptr, "a query that matches nothing gives a result set");
      require(rs && !rs->next(), "no row for id 999");
   }
   {
      // the same SQL text, now with a match, after an empty result
      DBStatementArgs some;
      some.add(new DBLong(2));
      unique_ptr<DBResultSet> rs(
         db.executeQuery("select name from items where id = ?", some));
      require(rs && rs->next(), "row for id 2 after an empty result");
      if (rs) {
         string name;
         rs->stringForColumnIndex(0, name);
         requireStringEquals("banana", name, "value after an empty result");
      }
   }
   requireLongEquals(0, scalar(db, "select count(*) from items where id > 1000"),
                     "count over no matching rows is a row containing 0");

   unsigned long rowsAffected = 99;
   require(db.executeUpdate("update items set qty = 1 where id = 12345", rowsAffected),
           "an update that matches nothing succeeds");
   requireLongEquals(0, rowsAffected, "update reports zero affected rows");
   rowsAffected = 99;
   require(db.executeUpdate("delete from items where id = 12345", rowsAffected),
           "a delete that matches nothing succeeds");
   requireLongEquals(0, rowsAffected, "delete reports zero affected rows");
   requireLongEquals(3, scalar(db, "select count(*) from items"), "rows unchanged");
   db.close();
}

void TestSQLiteFoundation::test_transactions() {
   TEST_CASE("test_transactions");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("txn.sqlite3");
   SQLiteDatabase db(path);
   require(db.open(), "open");
   require(exec(db, "create table acct(id integer primary key, v integer)"),
           "create table");

   require(!db.inTransaction(), "not in a transaction initially");
   require(db.beginTransaction(), "begin");
   require(db.inTransaction(), "in a transaction after begin");
   require(exec(db, "insert into acct(id,v) values (1,10)"), "insert in transaction");
   requireLongEquals(1, scalar(db, "select count(*) from acct"),
                     "the transaction sees its own insert");
   require(db.commit(), "commit");
   require(!db.inTransaction(), "not in a transaction after commit");
   requireLongEquals(1, countFromNewConnection(path, "acct"),
                     "a new connection sees the committed row");

   require(db.beginTransaction(), "begin again");
   require(exec(db, "insert into acct(id,v) values (2,20)"), "insert, to be rolled back");
   requireLongEquals(2, scalar(db, "select count(*) from acct"),
                     "the transaction sees the uncommitted row");
   require(db.rollback(), "rollback");
   require(!db.inTransaction(), "not in a transaction after rollback");
   requireLongEquals(1, scalar(db, "select count(*) from acct"),
                     "rolled-back row is gone on the same connection");
   requireLongEquals(1, countFromNewConnection(path, "acct"),
                     "rolled-back row never reached a new connection");

   require(db.beginDeferredTransaction(), "begin deferred");
   require(exec(db, "insert into acct(id,v) values (3,30)"), "insert in deferred transaction");
   require(db.commit(), "commit deferred transaction");
   requireLongEquals(2, countFromNewConnection(path, "acct"),
                     "committed deferred transaction is visible");
   requireLongEquals(40, scalar(db, "select sum(v) from acct"),
                     "the committed rows are the ones with v = 10 and 30");
   db.close();
}

void TestSQLiteFoundation::test_constraint_error_recovery() {
   TEST_CASE("test_constraint_error_recovery");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("constraint.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "create table u(id integer primary key, name text not null unique)"),
           "create table with constraints");

   const char* const insertSql = "insert into u(id,name) values (?,?)";
   unsigned long rowsAffected = 0;
   {
      DBStatementArgs args;
      args.add(new DBLong(1));
      args.add(new DBString("a"));
      require(db.executeUpdate(insertSql, args, rowsAffected), "first insert");
   }
   {
      DBStatementArgs duplicateKey;
      duplicateKey.add(new DBLong(1));
      duplicateKey.add(new DBString("b"));
      require(!db.executeUpdate(insertSql, duplicateKey, rowsAffected),
              "duplicate primary key is rejected");
      require(db.hadError(), "the failure is reported through hadError()");
      requireLongEquals(SQLITE_CONSTRAINT, db.lastErrorCode() & 0xff,
                        "the failure is a constraint error");
   }
   {
      // the very statement that just failed works again
      DBStatementArgs args;
      args.add(new DBLong(2));
      args.add(new DBString("b"));
      require(db.executeUpdate(insertSql, args, rowsAffected),
              "the same statement succeeds after a constraint error");
   }
   {
      DBStatementArgs nullName;
      nullName.add(new DBLong(3));
      nullName.add(new DBNull("Null"));
      require(!db.executeUpdate(insertSql, nullName, rowsAffected),
              "NULL in a NOT NULL column is rejected");
   }
   {
      DBStatementArgs duplicateName;
      duplicateName.add(new DBLong(3));
      duplicateName.add(new DBString("a"));
      require(!db.executeUpdate(insertSql, duplicateName, rowsAffected),
              "duplicate value in a UNIQUE column is rejected");
   }
   {
      DBStatementArgs args;
      args.add(new DBLong(3));
      args.add(new DBString("c"));
      require(db.executeUpdate(insertSql, args, rowsAffected),
              "valid insert after several errors");
   }
   requireLongEquals(3, scalar(db, "select count(*) from u"),
                     "only the three valid rows were stored");
   requireLongEquals(1 + 2 + 3, scalar(db, "select sum(id) from u"),
                     "the stored ids are 1, 2 and 3");

   // an error inside a transaction, followed by rollback
   require(db.beginTransaction(), "begin");
   require(exec(db, "insert into u(id,name) values (4,'d')"), "valid insert in transaction");
   require(!exec(db, "insert into u(id,name) values (4,'e')"),
           "duplicate key inside the transaction is rejected");
   require(db.rollback(), "rollback after an error");
   requireLongEquals(3, scalar(db, "select count(*) from u"),
                     "rollback removed the valid insert as well");
   requireLongEquals(0, scalar(db, "select count(*) from u where id = 4"),
                     "row 4 does not exist");

   // other kinds of failure leave the connection usable
   require(!exec(db, "this is not sql"), "a syntax error is reported");
   {
      unique_ptr<DBResultSet> rs(db.executeQuery("select * from no_such_table"));
      require(rs == nullptr, "a query over a missing table gives no result set");
   }
   requireLongEquals(3, scalar(db, "select count(*) from u"),
                     "connection still works after those failures");
   db.close();
}

// ---- result-set and argument lifetime (CH-01, CH-02) -----------------------

namespace {

SQLiteResultSet* openCursor(SQLiteDatabase& db, const string& sql) {
   return dynamic_cast<SQLiteResultSet*>(db.executeQuery(sql));
}

SQLiteResultSet* openCursor(SQLiteDatabase& db,
                            const string& sql,
                            const DBStatementArgs& args) {
   return dynamic_cast<SQLiteResultSet*>(db.executeQuery(sql, args));
}

const char* const ITEMS_BY_ID = "select id, name from items order by id";

}

// A result set with no data behind it (its database was closed or destroyed)
// answers like a NULL column everywhere -- 0, false, nullptr for every pointer
// the accessors return -- and never touches freed memory.
void TestSQLiteFoundation::requireCursorHasNoData(SQLiteResultSet& rs,
                                                  const string& when) {
   require(!rs.next(), when + ": next() is false");
   requireLongEquals(0, rs.intForColumnIndex(0), when + ": intForColumnIndex");
   requireLongEquals(0, rs.longForColumnIndex(0), when + ": longForColumnIndex");
   require(!rs.boolForColumnIndex(0), when + ": boolForColumnIndex");
   requireDoubleEquals(0.0, rs.doubleForColumnIndex(0), when + ": doubleForColumnIndex");
   requireLongEquals(0, rs.intForColumn("id"), when + ": intForColumn");
   requireLongEquals(0, rs.longForColumn("id"), when + ": longForColumn");
   requireDoubleEquals(0.0, rs.doubleForColumn("id"), when + ": doubleForColumn");

   unique_ptr<string> text(rs.stringForColumnIndex(0));
   require(text == nullptr, when + ": stringForColumnIndex gives no string");
   unique_ptr<string> namedText(rs.stringForColumn("name"));
   require(namedText == nullptr, when + ": stringForColumn gives no string");
   string value = "unchanged";
   require(!rs.stringForColumnIndex(0, value), when + ": stringForColumnIndex(string&)");
   requireStringEquals("", value, when + ": stringForColumnIndex(string&) clears its argument");
   require(!rs.stringForColumn("name", value), when + ": stringForColumn(string&)");

   // SQL NULL for every column: no data, no date (not an empty blob, not the epoch)
   unique_ptr<DBData> data(rs.dataForColumnIndex(0));
   require(data == nullptr, when + ": dataForColumnIndex is nullptr");
   unique_ptr<DBData> namedData(rs.dataForColumn("name"));
   require(namedData == nullptr, when + ": dataForColumn is nullptr");
   unique_ptr<DBDate> date(rs.dateForColumnIndex(0));
   require(date == nullptr, when + ": dateForColumnIndex is nullptr");
   unique_ptr<DBDate> namedDate(rs.dateForColumn("name"));
   require(namedDate == nullptr, when + ": dateForColumn is nullptr");
   requireLongEquals(0, rs.int64ForColumnIndex(0), when + ": int64ForColumnIndex");
   requireLongEquals(0, rs.int64ForColumn("id"), when + ": int64ForColumn");
   int checked = 99;
   require(!rs.intForColumnIndex(0, checked), when + ": checked intForColumnIndex is false");
   requireLongEquals(0, checked, when + ": and sets 0");
   checked = 99;
   require(!rs.intForColumn("id", checked), when + ": checked intForColumn is false");
   requireLongEquals(0, checked, when + ": and sets 0");
   requireLongEquals(0, rs.getColumnCount(), when + ": getColumnCount");
   require(rs.getTypeForColumnIndex(0) == ValueTypeNull, when + ": getTypeForColumnIndex");
   requireLongEquals(-1, rs.columnIndexForName("id"), when + ": columnIndexForName");
}

void TestSQLiteFoundation::test_cursor_after_database_close() {
   TEST_CASE("test_cursor_after_database_close");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("cursor_close.sqlite3"));
   require(db.open(), "open");
   require(createItems(db), "create and fill the items table");

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      unique_ptr<SQLiteResultSet> rs(openCursor(db, ITEMS_BY_ID));
      require(rs && rs->next(), when + ": first row before close()");
      if (!rs) {
         continue;
      }
      requireLongEquals(1, rs->intForColumnIndex(0), when + ": id before close()");

      db.close();   // a live cursor must neither block nor survive this
      require(db.sqliteHandle() == nullptr, when + ": the connection really closed");
      requireCursorHasNoData(*rs, when + ", after close()");

      rs.reset();
      require(db.open(), when + ": reopen");
   }
   db.close();
}

void TestSQLiteFoundation::test_cursor_after_database_destruction() {
   TEST_CASE("test_cursor_after_database_destruction");
   chapeau_tests::ScopedTempDir dir;

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      auto db = make_unique<SQLiteDatabase>(
         dir.file("cursor_destroy" + to_string(pass) + ".sqlite3"));
      require(db->open(), when + ": open");
      require(createItems(*db), when + ": create and fill the items table");
      db->setShouldCacheStatements(pass == 0);

      unique_ptr<SQLiteResultSet> stepped(openCursor(*db, ITEMS_BY_ID));
      unique_ptr<SQLiteResultSet> fresh(openCursor(*db, "select name from items order by id"));
      require(stepped && stepped->next(), when + ": a stepped cursor");
      require(fresh != nullptr, when + ": a cursor that was never stepped");

      db.reset();   // the database goes first

      if (stepped) {
         requireCursorHasNoData(*stepped, when + ", stepped cursor, after destruction");
      }
      if (fresh) {
         requireCursorHasNoData(*fresh, when + ", fresh cursor, after destruction");
      }
      fresh.reset();
      stepped.reset();
   }
}

void TestSQLiteFoundation::test_cursor_with_cache_disabled() {
   TEST_CASE("test_cursor_with_cache_disabled");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("cursor_cache.sqlite3"));
   require(db.open(), "open");
   require(createItems(db), "create and fill the items table");

   {
      // switching the cache off does not take a live cursor's statement away
      unique_ptr<SQLiteResultSet> rs(openCursor(db, ITEMS_BY_ID));
      require(rs && rs->next(), "first row");
      if (rs) {
         requireLongEquals(1, rs->intForColumnIndex(0), "first id");
         db.setShouldCacheStatements(false);
         require(rs->next(), "second row after the cache was switched off");
         requireLongEquals(2, rs->intForColumnIndex(0), "second id");
         require(rs->next(), "third row");
         requireLongEquals(3, rs->intForColumnIndex(0), "third id");
         require(!rs->next(), "no fourth row");
      }
   }

   // the database keeps working with the cache off, and with it back on
   requireLongEquals(3, scalar(db, "select count(*) from items"), "query with the cache off");
   db.setShouldCacheStatements(true);
   {
      unique_ptr<SQLiteResultSet> rs(openCursor(db, ITEMS_BY_ID));
      int rows = 0;
      while (rs && rows < 3 && rs->next()) {
         ++rows;
         requireLongEquals(rows, rs->intForColumnIndex(0), "id on a new cached cursor");
      }
      requireLongEquals(3, rows, "all rows on a new cached cursor");
   }

   {
      // cache switched off while a cursor is live, then the database closes
      unique_ptr<SQLiteResultSet> rs(openCursor(db, ITEMS_BY_ID));
      require(rs && rs->next(), "a live cursor");
      db.setShouldCacheStatements(false);
      db.close();
      if (rs) {
         requireCursorHasNoData(*rs, "cursor that outlived both the cache and the connection");
      }
   }
   require(db.open(), "reopen");
   db.setShouldCacheStatements(true);
   requireLongEquals(3, scalar(db, "select count(*) from items"), "usable after all of that");
   db.close();
}

void TestSQLiteFoundation::test_cursor_close_and_destroy_repeated() {
   TEST_CASE("test_cursor_close_and_destroy_repeated");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("cursor_repeat.sqlite3"));
   require(db.open(), "open");
   require(createItems(db), "create and fill the items table");

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      {
         unique_ptr<SQLiteResultSet> rs(openCursor(db, ITEMS_BY_ID));
         require(rs && rs->next(), when + ": a row");
         if (rs) {
            rs->close();
            rs->close();
            rs->close();
         }
      }   // destroyed after being closed three times

      {
         unique_ptr<SQLiteResultSet> rs(openCursor(db, ITEMS_BY_ID));
         int rows = 0;
         while (rs && rs->next()) {
            ++rows;
         }
         requireLongEquals(3, rows, when + ": a full pass");
         if (rs) {
            rs->close();   // already closed by reaching the end
            rs->close();
         }
      }

      {
         // two cursors, destroyed in both orders
         unique_ptr<SQLiteResultSet> first(openCursor(db, ITEMS_BY_ID));
         unique_ptr<SQLiteResultSet> second(openCursor(db, "select name from items"));
         require(first && second, when + ": two cursors");
         first.reset();
         second.reset();
         unique_ptr<SQLiteResultSet> third(openCursor(db, ITEMS_BY_ID));
         unique_ptr<SQLiteResultSet> fourth(openCursor(db, "select name from items"));
         require(third && fourth, when + ": two more cursors");
         fourth.reset();
         third.reset();
      }

      requireLongEquals(3, scalar(db, "select count(*) from items"),
                        when + ": the database is intact");
   }

   {
      // closing an invalid cursor again is also harmless
      db.setShouldCacheStatements(true);
      unique_ptr<SQLiteResultSet> rs(openCursor(db, ITEMS_BY_ID));
      require(rs && rs->next(), "a live cursor");
      db.close();
      if (rs) {
         rs->close();
         rs->close();
      }
      rs.reset();
   }
   require(db.open(), "reopen");
   db.close();
}

void TestSQLiteFoundation::test_cursor_invalid_after_reopen() {
   TEST_CASE("test_cursor_invalid_after_reopen");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("cursor_reopen.sqlite3"));
   require(db.open(), "open");
   require(createItems(db), "create and fill the items table");

   unique_ptr<SQLiteResultSet> old(openCursor(db, ITEMS_BY_ID));
   require(old && old->next(), "a row from the old cursor");

   db.close();
   require(db.open(), "reopen the same file");
   requireLongEquals(3, scalar(db, "select count(*) from items"), "the data is still there");

   // the old cursor is not revived by the database being open again
   if (old) {
      requireCursorHasNoData(*old, "old cursor after reopen");
   }

   unique_ptr<SQLiteResultSet> fresh(openCursor(db, ITEMS_BY_ID));
   require(fresh && fresh->next(), "a new cursor works");
   if (fresh) {
      requireLongEquals(1, fresh->intForColumnIndex(0), "the new cursor starts at the first row");
   }
   if (old) {
      require(!old->next(), "old cursor is still invalid while the new one is live");
   }
   if (fresh) {
      require(fresh->next(), "the new cursor is unaffected by the old one");
      requireLongEquals(2, fresh->intForColumnIndex(0), "the new cursor's second row");
   }
   fresh.reset();
   old.reset();
   db.close();
}

void TestSQLiteFoundation::test_temporary_text_arguments() {
   TEST_CASE("test_temporary_text_arguments");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("text_args.sqlite3"));
   require(db.open(), "open");

   const string first = string(5000, 'a') + "-first";
   const string second = string(7000, 'b') + "-second";

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      // the arguments are gone before the first next()
      auto args = make_unique<DBStatementArgs>();
      args->add(new DBString(first));
      unique_ptr<SQLiteResultSet> rs(openCursor(db, "select ?", *args));
      args.reset();
      require(rs && rs->next(), when + ": a row");
      if (rs) {
         string value;
         require(rs->stringForColumnIndex(0, value), when + ": the text is not NULL");
         requireStringEquals(first, value, when + ": text bound from destroyed arguments");
      }
      rs.reset();

      // the same SQL again, with different text, also destroyed before stepping
      auto again = make_unique<DBStatementArgs>();
      again->add(new DBString(second));
      rs.reset(openCursor(db, "select ?", *again));
      again.reset();
      require(rs && rs->next(), when + ": a row from the second execution");
      if (rs) {
         string value;
         rs->stringForColumnIndex(0, value);
         requireStringEquals(second, value, when + ": text from the second execution");
      }
      rs.reset();
   }
   db.setShouldCacheStatements(true);

   // an update whose statement stays cached after its arguments are gone
   require(exec(db, "create table words(id integer primary key, w text)"), "create table");
   for (int id = 1; id <= 2; ++id) {
      auto args = make_unique<DBStatementArgs>();
      args->add(new DBLong(id));
      args->add(new DBString(id == 1 ? first : second));
      unsigned long rowsAffected = 0;
      require(db.executeUpdate("insert into words(id,w) values (?,?)", *args, rowsAffected),
              "insert with temporary arguments");
   }
   requireLongEquals(static_cast<long long>(first.size()),
                     scalar(db, "select length(w) from words where id = 1"), "first stored length");
   requireLongEquals(static_cast<long long>(second.size()),
                     scalar(db, "select length(w) from words where id = 2"), "second stored length");

   // NULL and empty-string handling is unchanged
   {
      auto args = make_unique<DBStatementArgs>();
      args->add(nullptr);
      unique_ptr<SQLiteResultSet> rs(openCursor(db, "select ?", *args));
      args.reset();
      require(rs && rs->next(), "a row for a NULL argument");
      if (rs) {
         require(rs->getTypeForColumnIndex(0) == ValueTypeNull, "a null argument is bound as NULL");
         unique_ptr<string> text(rs->stringForColumnIndex(0));
         require(text == nullptr, "and reads back as no string");
      }
   }
   {
      auto args = make_unique<DBStatementArgs>();
      args->add(new DBNull("Null"));
      unique_ptr<SQLiteResultSet> rs(openCursor(db, "select ?", *args));
      args.reset();
      require(rs && rs->next(), "a row for a DBNull argument");
      if (rs) {
         require(rs->getTypeForColumnIndex(0) == ValueTypeNull, "a DBNull argument is bound as NULL");
      }
   }
   {
      auto args = make_unique<DBStatementArgs>();
      args->add(new DBString(""));
      unique_ptr<SQLiteResultSet> rs(openCursor(db, "select ?", *args));
      args.reset();
      require(rs && rs->next(), "a row for an empty string");
      if (rs) {
         require(rs->getTypeForColumnIndex(0) == ValueTypeText, "an empty string is bound as text");
         string value = "unchanged";
         require(rs->stringForColumnIndex(0, value), "the empty string is not NULL");
         requireStringEquals("", value, "and reads back empty");
      }
   }
   db.close();
}

void TestSQLiteFoundation::test_temporary_blob_arguments() {
   TEST_CASE("test_temporary_blob_arguments");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("blob_args.sqlite3"));
   require(db.open(), "open");

   // not a string: embedded NULs, every byte value, longer than any small buffer
   string firstBytes;
   string secondBytes;
   for (int i = 0; i < 4096; ++i) {
      firstBytes.push_back(static_cast<char>(i % 251));
      secondBytes.push_back(static_cast<char>((i * 7 + 3) % 256));
   }

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      const string* const blobs[] = {&firstBytes, &secondBytes};
      for (const string* blob : blobs) {
         auto args = make_unique<DBStatementArgs>();
         args->add(new DBData(blob->data(), blob->size()));
         unique_ptr<SQLiteResultSet> rs(openCursor(db, "select ?", *args));
         args.reset();   // the DBData is gone before the first next()
         require(rs && rs->next(), when + ": a row");
         if (rs) {
            unique_ptr<DBData> data(rs->dataForColumnIndex(0));
            require(data != nullptr, when + ": a blob");
            if (data) {
               requireLongEquals(static_cast<long long>(blob->size()), data->length(),
                                 when + ": blob length");
               require(::memcmp(data->value(), blob->data(), blob->size()) == 0,
                       when + ": blob bytes bound from destroyed arguments");
            }
         }
      }
   }
   db.setShouldCacheStatements(true);

   require(exec(db, "create table blobs(id integer primary key, b blob)"), "create table");
   for (int id = 1; id <= 2; ++id) {
      const string& bytes = (id == 1) ? firstBytes : secondBytes;
      auto args = make_unique<DBStatementArgs>();
      args->add(new DBLong(id));
      args->add(new DBData(bytes.data(), bytes.size()));
      unsigned long rowsAffected = 0;
      require(db.executeUpdate("insert into blobs(id,b) values (?,?)", *args, rowsAffected),
              "insert a blob from temporary arguments");
   }
   requireLongEquals(2, scalar(db, "select count(*) from blobs where typeof(b) = 'blob' and length(b) = 4096"),
                     "both blobs are stored as 4096-byte blobs");
   db.close();
}

// ---- prepared statement and cursor correctness (CH-03, CH-04, CH-05) -------

namespace {

// how many sqlite3 statements are open (prepared and not finalized) on the
// connection: cached ones, and the ones active result sets are running
int openStatementCount(SQLiteDatabase& db) {
   if (db.sqliteHandle() == nullptr) {
      return -1;
   }
   int count = 0;
   for (sqlite3_stmt* s = sqlite3_next_stmt(db.sqliteHandle(), nullptr);
        s != nullptr;
        s = sqlite3_next_stmt(db.sqliteHandle(), s)) {
      ++count;
   }
   return count;
}

// the items table, created with caching off so that the statements used to
// fill it are not left in the cache: the connection starts with none open
bool createItemsWithoutCachedStatements(SQLiteDatabase& db) {
   db.setShouldCacheStatements(false);
   const bool ok = createItems(db);
   db.setShouldCacheStatements(true);
   return ok;
}

string firstText(SQLiteResultSet& rs) {
   string value;
   rs.stringForColumnIndex(0, value);
   return value;
}

}

void TestSQLiteFoundation::test_two_cursors_identical_sql() {
   TEST_CASE("test_two_cursors_identical_sql");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("two_cursors.sqlite3"));
   require(db.open(), "open");
   require(createItemsWithoutCachedStatements(db), "create and fill the items table");

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);
      requireLongEquals(0, openStatementCount(db), when + ": no statement is open to begin with");

      {
         unique_ptr<SQLiteResultSet> r1(openCursor(db, ITEMS_BY_ID));
         unique_ptr<SQLiteResultSet> r2(openCursor(db, ITEMS_BY_ID));
         require(r1 && r2, when + ": two cursors over identical SQL");
         requireLongEquals(2, openStatementCount(db),
                           when + ": each active cursor has a statement of its own");
         if (r1 && r2) {
            // interleaved: each cursor has its own position
            require(r1->next(), when + ": r1 row 1");
            requireLongEquals(1, r1->intForColumnIndex(0), when + ": r1 id");
            require(r2->next(), when + ": r2 row 1");
            requireLongEquals(1, r2->intForColumnIndex(0), when + ": r2 starts at the first row");
            require(r1->next(), when + ": r1 row 2");
            requireLongEquals(2, r1->intForColumnIndex(0), when + ": r1 second id");
            require(r2->next(), when + ": r2 row 2");
            requireLongEquals(2, r2->intForColumnIndex(0), when + ": r2 second id");
            require(r1->next(), when + ": r1 row 3");
            requireLongEquals(3, r1->intForColumnIndex(0), when + ": r1 third id");
            require(!r1->next(), when + ": r1 is exhausted");

            // r1 ending does not disturb r2
            require(r2->next(), when + ": r2 row 3 after r1 ended");
            requireLongEquals(3, r2->intForColumnIndex(0), when + ": r2 third id");
            require(!r2->next(), when + ": r2 is exhausted");
         }
         // both ended: a cached statement stays for reuse, the other is gone
         requireLongEquals(pass == 0 ? 1 : 0, openStatementCount(db),
                           when + ": statements left once both ended");
      }
      requireLongEquals(pass == 0 ? 1 : 0, openStatementCount(db),
                        when + ": statements left once both are destroyed");
   }
   db.close();
}

void TestSQLiteFoundation::test_two_cursors_parameterized_sql() {
   TEST_CASE("test_two_cursors_parameterized_sql");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("two_param_cursors.sqlite3"));
   require(db.open(), "open");
   require(createItemsWithoutCachedStatements(db), "create and fill the items table");

   const char* const sql = "select name from items where id >= ? order by id";

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      // same SQL, different arguments, both arguments gone before stepping
      auto from1 = make_unique<DBStatementArgs>();
      from1->add(new DBLong(1));
      unique_ptr<SQLiteResultSet> r1(openCursor(db, sql, *from1));
      from1.reset();

      auto from3 = make_unique<DBStatementArgs>();
      from3->add(new DBLong(3));
      unique_ptr<SQLiteResultSet> r2(openCursor(db, sql, *from3));
      from3.reset();

      require(r1 && r2, when + ": two cursors over identical parameterized SQL");
      if (!r1 || !r2) {
         continue;
      }
      require(r1->next(), when + ": r1 row 1");
      requireStringEquals("apple", firstText(*r1), when + ": r1 first row");
      require(r2->next(), when + ": r2 row 1");
      requireStringEquals("cherry", firstText(*r2), when + ": r2 sees only id >= 3");
      require(r1->next(), when + ": r1 row 2");
      requireStringEquals("banana", firstText(*r1), when + ": r1 second row");
      require(!r2->next(), when + ": r2 has just the one row");
      require(r1->next(), when + ": r1 row 3");
      requireStringEquals("cherry", firstText(*r1), when + ": r1 third row");
      require(!r1->next(), when + ": r1 is exhausted");
   }
   db.close();
}

void TestSQLiteFoundation::test_close_one_cursor_while_another_is_active() {
   TEST_CASE("test_close_one_cursor_while_another_is_active");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("close_one.sqlite3"));
   require(db.open(), "open");
   require(createItemsWithoutCachedStatements(db), "create and fill the items table");

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      {
         // close the second cursor while the first is part way through
         unique_ptr<SQLiteResultSet> r1(openCursor(db, ITEMS_BY_ID));
         unique_ptr<SQLiteResultSet> r2(openCursor(db, ITEMS_BY_ID));
         require(r1 && r2 && r1->next() && r2->next() && r2->next(), when + ": both under way");
         if (r1 && r2) {
            requireLongEquals(1, r1->intForColumnIndex(0), when + ": r1 id before the close");
            r2->close();
            require(!r2->next(), when + ": the closed cursor is finished");
            require(r1->next(), when + ": r1 continues");
            requireLongEquals(2, r1->intForColumnIndex(0), when + ": r1 is where it was");
            require(r1->next(), when + ": r1 continues to the end");
            requireLongEquals(3, r1->intForColumnIndex(0), when + ": r1 third id");
            require(!r1->next(), when + ": r1 ends");
         }
      }

      {
         // and the other way round: close the first while the second runs on
         unique_ptr<SQLiteResultSet> r1(openCursor(db, ITEMS_BY_ID));
         unique_ptr<SQLiteResultSet> r2(openCursor(db, ITEMS_BY_ID));
         require(r1 && r2 && r1->next() && r2->next(), when + ": both under way again");
         if (r1 && r2) {
            r1->close();
            require(r2->next(), when + ": r2 continues after r1 closed");
            requireLongEquals(2, r2->intForColumnIndex(0), when + ": r2 second id");

            // a third cursor starts from the beginning, whatever r2 is doing
            unique_ptr<SQLiteResultSet> r3(openCursor(db, ITEMS_BY_ID));
            require(r3 && r3->next(), when + ": a third cursor");
            if (r3) {
               requireLongEquals(1, r3->intForColumnIndex(0), when + ": r3 starts at the first row");
            }
            require(r2->next(), when + ": r2 is not disturbed by r3");
            requireLongEquals(3, r2->intForColumnIndex(0), when + ": r2 third id");
         }
      }
   }
   db.close();
}

void TestSQLiteFoundation::test_statement_reuse_after_cursor_destroyed() {
   TEST_CASE("test_statement_reuse_after_cursor_destroyed");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("reuse.sqlite3"));
   require(db.open(), "open");
   require(createItemsWithoutCachedStatements(db), "create and fill the items table");
   db.setShouldCacheStatements(true);

   for (int round = 0; round < 3; ++round) {
      const string when = "round " + to_string(round);

      {
         // destroyed part way through
         unique_ptr<SQLiteResultSet> rs(openCursor(db, ITEMS_BY_ID));
         require(rs && rs->next(), when + ": a row");
         if (rs) {
            requireLongEquals(1, rs->intForColumnIndex(0), when + ": starts at the first row");
         }
      }
      requireLongEquals(1, openStatementCount(db),
                        when + ": the statement is kept, idle, in the cache");

      {
         // and again after running to the end
         unique_ptr<SQLiteResultSet> rs(openCursor(db, ITEMS_BY_ID));
         int rows = 0;
         while (rs && rows < 3 && rs->next()) {
            ++rows;
            requireLongEquals(rows, rs->intForColumnIndex(0), when + ": id in a full pass");
         }
         requireLongEquals(3, rows, when + ": all rows");
         require(rs && !rs->next(), when + ": ended");
      }
      requireLongEquals(1, openStatementCount(db),
                        when + ": still the one cached statement, reused");
   }
   db.close();
}

void TestSQLiteFoundation::test_incorrect_argument_counts() {
   TEST_CASE("test_incorrect_argument_counts");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("arg_counts.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(createItemsWithoutCachedStatements(db), "create and fill the items table");

   const char* const querySql = "select ?, ?";
   const char* const updateSql = "update items set qty = ? where id = ?";

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      for (int attempt = 0; attempt < 2; ++attempt) {
         const string label = when + ", attempt " + to_string(attempt);

         DBStatementArgs tooFew;
         tooFew.add(new DBLong(1));
         unique_ptr<SQLiteResultSet> rs(openCursor(db, querySql, tooFew));
         require(rs == nullptr, label + ": too few arguments are refused");

         DBStatementArgs tooMany;
         tooMany.add(new DBLong(1));
         tooMany.add(new DBLong(2));
         tooMany.add(new DBLong(3));
         rs.reset(openCursor(db, querySql, tooMany));
         require(rs == nullptr, label + ": too many arguments are refused");

         // the same SQL still works, with the right values
         DBStatementArgs right;
         right.add(new DBLong(5 + attempt));
         right.add(new DBLong(6 + attempt));
         rs.reset(openCursor(db, querySql, right));
         require(rs && rs->next(), label + ": the right number of arguments works");
         if (rs) {
            requireLongEquals(5 + attempt, rs->longForColumnIndex(0), label + ": first value");
            requireLongEquals(6 + attempt, rs->longForColumnIndex(1), label + ": second value");
         }
      }

      {
         // arguments for SQL that takes none, then the plain call
         DBStatementArgs extra;
         extra.add(new DBLong(1));
         unique_ptr<SQLiteResultSet> rs(
            openCursor(db, "select count(*) from items", extra));
         require(rs == nullptr, when + ": an argument for SQL without parameters is refused");
         rs.reset(openCursor(db, "select count(*) from items"));
         require(rs && rs->next(), when + ": the plain call works");
         if (rs) {
            requireLongEquals(3, rs->longForColumnIndex(0), when + ": count");
         }
      }

      // updates: a refused call executes nothing, and does not disturb the next
      unsigned long rowsAffected = 99;
      DBStatementArgs onlyQty;
      onlyQty.add(new DBLong(7));
      require(!db.executeUpdate(updateSql, onlyQty, rowsAffected),
              when + ": update with too few arguments is refused");
      requireLongEquals(0, rowsAffected, when + ": and reports no rows");

      DBStatementArgs three;
      three.add(new DBLong(7));
      three.add(new DBLong(2));
      three.add(new DBLong(2));
      rowsAffected = 99;
      require(!db.executeUpdate(updateSql, three, rowsAffected),
              when + ": update with too many arguments is refused");
      requireLongEquals(12, scalar(db, "select qty from items where id = 2"),
                        when + ": nothing was updated by the refused calls");

      DBStatementArgs good;
      good.add(new DBLong(99));
      good.add(new DBLong(2));
      require(db.executeUpdate(updateSql, good, rowsAffected), when + ": update with the right arguments");
      requireLongEquals(1, rowsAffected, when + ": one row updated");
      requireLongEquals(99, scalar(db, "select qty from items where id = 2"), when + ": new qty");

      DBStatementArgs fewAgain;
      fewAgain.add(new DBLong(7));
      require(!db.executeUpdate(updateSql, fewAgain, rowsAffected),
              when + ": refused again after a successful call");
      requireLongEquals(99, scalar(db, "select qty from items where id = 2"),
                        when + ": the refused call left the value alone");

      DBStatementArgs restore;
      restore.add(new DBLong(12));
      restore.add(new DBLong(2));
      require(db.executeUpdate(updateSql, restore, rowsAffected), when + ": restore the value");
      requireLongEquals(12, scalar(db, "select qty from items where id = 2"), when + ": restored");
   }
   db.close();
}

void TestSQLiteFoundation::test_arguments_then_none_then_arguments() {
   TEST_CASE("test_arguments_then_none_then_arguments");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("args_noargs.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(createItemsWithoutCachedStatements(db), "create and fill the items table");

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      // query: args -> no args -> args -> no args -> args
      const char* const sql = "select ?";
      const long long values[] = {7, 9, 11};
      for (int i = 0; i < 3; ++i) {
         {
            DBStatementArgs args;
            args.add(new DBLong(values[i]));
            unique_ptr<SQLiteResultSet> rs(openCursor(db, sql, args));
            require(rs && rs->next(), when + ": query with arguments, step " + to_string(i));
            if (rs) {
               requireLongEquals(values[i], rs->longForColumnIndex(0),
                                 when + ": value for step " + to_string(i));
            }
         }
         if (i < 2) {
            unique_ptr<SQLiteResultSet> bare(openCursor(db, sql));
            require(bare == nullptr, when + ": the no-argument call on SQL with a parameter is refused");
         }
      }
      requireLongEquals(pass == 0 ? 1 : 0, openStatementCount(db),
                        when + ": the cached statement is intact, an uncached one left nothing behind");

      // update: args -> no args -> args
      const char* const updateSql = "insert into items(id,name) values (?,?)";
      unsigned long rowsAffected = 99;
      {
         DBStatementArgs args;
         args.add(new DBLong(10 + pass * 10));
         args.add(new DBString("ten"));
         require(db.executeUpdate(updateSql, args, rowsAffected), when + ": insert with arguments");
         requireLongEquals(1, rowsAffected, when + ": one row inserted");
      }
      rowsAffected = 99;
      require(!db.executeUpdate(updateSql, rowsAffected),
              when + ": the no-argument update on SQL with parameters is refused");
      requireLongEquals(0, rowsAffected, when + ": and inserts nothing");
      {
         DBStatementArgs args;
         args.add(new DBLong(11 + pass * 10));
         args.add(new DBString("eleven"));
         require(db.executeUpdate(updateSql, args, rowsAffected),
                 when + ": insert with arguments after the refused call");
         requireLongEquals(1, rowsAffected, when + ": one row inserted again");
      }
      requireLongEquals(3 + 2 * (pass + 1), scalar(db, "select count(*) from items"),
                        when + ": exactly the two valid inserts of this pass were stored");
      requireLongEquals(1, scalar(db, "select count(*) from items where name = 'eleven' and id = " +
                                      to_string(11 + pass * 10)),
                        when + ": with the right values");
   }
   db.close();
}

void TestSQLiteFoundation::test_cursor_exhaustion_is_terminal() {
   TEST_CASE("test_cursor_exhaustion_is_terminal");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("exhaustion.sqlite3"));
   require(db.open(), "open");
   require(createItemsWithoutCachedStatements(db), "create and fill the items table");

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      {
         unique_ptr<SQLiteResultSet> rs(openCursor(db, ITEMS_BY_ID));
         int rowsSeen = 0;
         while (rs && rowsSeen < 3 && rs->next()) {
            ++rowsSeen;
         }
         requireLongEquals(3, rowsSeen, when + ": three rows");
         require(rs && !rs->next(), when + ": the end");

         // asking again never restarts the query
         for (int again = 0; again < 5; ++again) {
            if (rs && rs->next()) {
               ++rowsSeen;
            }
         }
         requireLongEquals(3, rowsSeen, when + ": no more rows however often next() is called");
         if (rs) {
            requireCursorHasNoData(*rs, when + ": after exhaustion");
            rs->close();
            rs->close();
         }
      }
      requireLongEquals(pass == 0 ? 1 : 0, openStatementCount(db),
                        when + ": the exhausted cursor holds no statement");

      {
         // a new cursor is a new execution: it does see the rows
         unique_ptr<SQLiteResultSet> rs(openCursor(db, ITEMS_BY_ID));
         int rows = 0;
         while (rs && rows < 3 && rs->next()) {
            ++rows;
         }
         requireLongEquals(3, rows, when + ": a new cursor sees every row");
      }

      {
         // exhaustion followed by the database closing, with another cursor active
         unique_ptr<SQLiteResultSet> done(openCursor(db, ITEMS_BY_ID));
         unique_ptr<SQLiteResultSet> running(openCursor(db, ITEMS_BY_ID));
         while (done && done->next()) {
         }
         require(running && running->next(), when + ": a cursor still running");
         db.close();
         require(db.sqliteHandle() == nullptr, when + ": the connection closed");
         if (done) {
            requireCursorHasNoData(*done, when + ": exhausted cursor after the database closed");
         }
         if (running) {
            requireCursorHasNoData(*running, when + ": running cursor after the database closed");
         }
      }
      require(db.open(), when + ": reopen");
   }
   db.close();
}

void TestSQLiteFoundation::test_cache_disabled_with_several_active_cursors() {
   TEST_CASE("test_cache_disabled_with_several_active_cursors");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("cache_many.sqlite3"));
   require(db.open(), "open");
   require(createItemsWithoutCachedStatements(db), "create and fill the items table");

   const char* const namesSql = "select name from items order by id";

   {
      // a: cached statement; b: same SQL, so a separate statement; c: other SQL
      unique_ptr<SQLiteResultSet> a(openCursor(db, ITEMS_BY_ID));
      unique_ptr<SQLiteResultSet> b(openCursor(db, ITEMS_BY_ID));
      unique_ptr<SQLiteResultSet> c(openCursor(db, namesSql));
      require(a && b && c, "three active cursors");
      if (a && b && c) {
         requireLongEquals(3, openStatementCount(db), "three statements are open");
         require(a->next() && b->next() && b->next() && c->next(), "all under way");
         requireLongEquals(1, a->intForColumnIndex(0), "a at row 1");
         requireLongEquals(2, b->intForColumnIndex(0), "b at row 2");
         requireStringEquals("apple", firstText(*c), "c at row 1");

         db.setShouldCacheStatements(false);   // the cache held a and c's statements
         requireLongEquals(3, openStatementCount(db), "no active statement was finalized");

         require(c->next(), "c row 2");
         requireStringEquals("banana", firstText(*c), "c at row 2");
         require(b->next(), "b row 3");
         requireLongEquals(3, b->intForColumnIndex(0), "b at row 3");
         require(a->next(), "a row 2");
         requireLongEquals(2, a->intForColumnIndex(0), "a at row 2");
         require(!b->next(), "b ends");
         requireLongEquals(2, openStatementCount(db), "b's statement is gone, the others live");
         require(a->next(), "a row 3");
         requireLongEquals(3, a->intForColumnIndex(0), "a at row 3");
         require(c->next(), "c row 3");
         requireStringEquals("cherry", firstText(*c), "c at row 3");
         require(!a->next() && !c->next(), "a and c end");
      }
   }
   requireLongEquals(0, openStatementCount(db),
                     "every statement was finalized once its last user ended");

   {
      // several active cursors when the database closes
      db.setShouldCacheStatements(true);
      unique_ptr<SQLiteResultSet> a(openCursor(db, ITEMS_BY_ID));
      unique_ptr<SQLiteResultSet> b(openCursor(db, ITEMS_BY_ID));
      unique_ptr<SQLiteResultSet> c(openCursor(db, namesSql));
      require(a && b && c && a->next() && b->next() && c->next(), "three cursors under way");
      db.setShouldCacheStatements(false);
      db.close();
      require(db.sqliteHandle() == nullptr, "the connection closed with cursors active");
      if (a && b && c) {
         requireCursorHasNoData(*a, "a after the database closed");
         requireCursorHasNoData(*b, "b after the database closed");
         requireCursorHasNoData(*c, "c after the database closed");
      }
   }
   require(db.open(), "reopen");
   requireLongEquals(0, openStatementCount(db), "a fresh connection has no statements");
   db.close();
}

void TestSQLiteFoundation::test_update_while_cursor_owns_same_sql() {
   TEST_CASE("test_update_while_cursor_owns_same_sql");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("update_slot.sqlite3"));
   require(db.open(), "open");
   db.setShouldCacheStatements(false);
   require(exec(db, "create table log(id integer primary key, msg text)"), "create table");
   db.setShouldCacheStatements(true);

   const char* const insertSql = "insert into log(msg) values ('x')";

   // a query-style cursor over a statement is not stepped until next() is
   // called, so it owns the cached statement for this SQL in the meantime
   unique_ptr<SQLiteResultSet> pending(openCursor(db, insertSql));
   require(pending != nullptr, "a cursor over an insert that has not run yet");
   requireLongEquals(1, openStatementCount(db), "its statement is the cached one");

   // an update of the same SQL runs on a statement of its own
   unsigned long rowsAffected = 0;
   require(db.executeUpdate(insertSql, rowsAffected), "executeUpdate of the same SQL");
   requireLongEquals(1, rowsAffected, "one row inserted");
   requireLongEquals(1, openStatementCount(db), "the update's statement is gone, the cached one remains");

   // the cursor's own statement still runs, once
   if (pending) {
      require(!pending->next(), "the insert runs when the cursor is stepped");
      require(!pending->next(), "and does not run again");
   }
   pending.reset();
   requireLongEquals(1, openStatementCount(db), "one idle cached statement is left");

   // with the cursor gone the cached statement is used again
   require(db.executeUpdate(insertSql, rowsAffected), "executeUpdate once more");
   requireLongEquals(1, openStatementCount(db), "still the one cached statement");

   // one row per execution: the update, the cursor, the update again
   requireLongEquals(3, scalar(db, "select count(*) from log"), "three rows stored");
   db.close();
}

// ---- schema changes under cached prepared statements (CH-06) ---------------

namespace {

// "select name from items where id = ?" -- the same SQL text every time, so
// with caching on it is one cached statement that outlives schema changes
string nameForId(SQLiteDatabase& db, long id) {
   DBStatementArgs args;
   args.add(new DBLong(id));
   unique_ptr<SQLiteResultSet> rs(
      openCursor(db, "select name from items where id = ?", args));
   if (rs && rs->next()) {
      return firstText(*rs);
   }
   return "<no row>";
}

// "update items set qty = ? where id = ?"; returns the rows it reports
// affecting, or -1 if the update failed
long long setQty(SQLiteDatabase& db, long qty, long id) {
   DBStatementArgs args;
   args.add(new DBLong(qty));
   args.add(new DBLong(id));
   unsigned long rowsAffected = 0;
   if (!db.executeUpdate("update items set qty = ? where id = ?", args, rowsAffected)) {
      return -1;
   }
   return static_cast<long long>(rowsAffected);
}

// the column count of "select * from items where id = ?"
int starColumnCount(SQLiteDatabase& db, long id) {
   DBStatementArgs args;
   args.add(new DBLong(id));
   unique_ptr<SQLiteResultSet> rs(
      openCursor(db, "select * from items where id = ?", args));
   return (rs && rs->next()) ? rs->getColumnCount() : -1;
}

}

void TestSQLiteFoundation::test_schema_change_query() {
   TEST_CASE("test_schema_change_query");
   chapeau_tests::ScopedTempDir dir;

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      SQLiteDatabase db(dir.file("schema_query" + to_string(pass) + ".sqlite3"));
      db.setLogsErrors(false);
      require(db.open(), when + ": open");
      require(createItems(db), when + ": create and fill the items table");
      db.setShouldCacheStatements(pass == 0);

      requireStringEquals("banana", nameForId(db, 2), when + ": before any schema change");
      requireLongEquals(4, starColumnCount(db, 2), when + ": select * sees four columns");

      // ALTER TABLE ADD COLUMN
      require(exec(db, "alter table items add column extra integer"), when + ": alter table");
      requireStringEquals("banana", nameForId(db, 2), when + ": after ALTER TABLE");
      requireStringEquals("cherry", nameForId(db, 3), when + ": another parameter after ALTER TABLE");
      requireStringEquals("apple", nameForId(db, 1), when + ": and another");
      requireLongEquals(5, starColumnCount(db, 2),
                        when + ": the same select * now sees the new column");

      // CREATE INDEX, then DROP INDEX
      require(exec(db, "create index ix_items_name on items(name)"), when + ": create index");
      requireStringEquals("banana", nameForId(db, 2), when + ": after CREATE INDEX");
      require(exec(db, "drop index ix_items_name"), when + ": drop index");
      requireStringEquals("cherry", nameForId(db, 3), when + ": after DROP INDEX");

      // a value stored through the new column is read through the cached statement
      require(exec(db, "update items set extra = 77 where id = 2"), when + ": set the new column");
      {
         DBStatementArgs args;
         args.add(new DBLong(2));
         unique_ptr<SQLiteResultSet> rs(
            openCursor(db, "select * from items where id = ?", args));
         require(rs && rs->next(), when + ": row 2 through select *");
         if (rs) {
            requireLongEquals(77, rs->longForColumn("extra"), when + ": the new column's value");
            requireLongEquals(12, rs->longForColumn("qty"), when + ": an old column's value");
         }
      }
      db.close();
   }
}

void TestSQLiteFoundation::test_schema_change_update() {
   TEST_CASE("test_schema_change_update");
   chapeau_tests::ScopedTempDir dir;

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      SQLiteDatabase db(dir.file("schema_update" + to_string(pass) + ".sqlite3"));
      db.setLogsErrors(false);
      require(db.open(), when + ": open");
      require(createItems(db), when + ": create and fill the items table");
      db.setShouldCacheStatements(pass == 0);

      requireLongEquals(1, setQty(db, 100, 1), when + ": update before any schema change");
      requireLongEquals(100, scalar(db, "select qty from items where id = 1"), when + ": qty 100");

      require(exec(db, "alter table items add column extra integer"), when + ": alter table");
      requireLongEquals(1, setQty(db, 101, 1), when + ": the same update after ALTER TABLE");
      requireLongEquals(101, scalar(db, "select qty from items where id = 1"), when + ": qty 101");
      requireLongEquals(1, setQty(db, 102, 1), when + ": and once more");
      requireLongEquals(102, scalar(db, "select qty from items where id = 1"), when + ": qty 102");

      require(exec(db, "create index ix_items_qty on items(qty)"), when + ": create index");
      requireLongEquals(1, setQty(db, 103, 3), when + ": the same update after CREATE INDEX");
      requireLongEquals(103, scalar(db, "select qty from items where id = 3"), when + ": qty of row 3");
      requireLongEquals(0, setQty(db, 5, 999), when + ": an update matching nothing reports 0 rows");

      // an insert statement across a schema change keeps working, with the new column NULL
      for (int i = 0; i < 2; ++i) {
         if (i == 1) {
            require(exec(db, "alter table items add column more text"), when + ": a second alter table");
         }
         DBStatementArgs args;
         args.add(new DBLong(20 + i));
         args.add(new DBString("added" + to_string(i)));
         unsigned long rowsAffected = 0;
         require(db.executeUpdate("insert into items(id,name) values (?,?)", args, rowsAffected),
                 when + ": the same insert, round " + to_string(i));
         requireLongEquals(1, rowsAffected, when + ": one row inserted");
      }
      requireLongEquals(5, scalar(db, "select count(*) from items"), when + ": five rows");
      requireLongEquals(2, scalar(db, "select count(*) from items where id >= 20 and extra is null"),
                        when + ": the added rows have the new column NULL");

      // the no-argument overloads: the same literal SQL across schema changes
      const string bump = "update items set qty = qty + 1 where id = 2";
      const string countSql = "select count(*) from items";
      require(exec(db, bump), when + ": literal update before a schema change");
      requireLongEquals(5, scalar(db, countSql), when + ": literal query before a schema change");
      require(exec(db, "alter table items add column more2 integer"), when + ": alter table again");
      require(exec(db, bump), when + ": the same literal update after ALTER TABLE");
      requireLongEquals(5, scalar(db, countSql), when + ": the same literal query after ALTER TABLE");
      require(exec(db, "create index ix_items_more2 on items(more2)"), when + ": create index");
      require(exec(db, bump), when + ": the same literal update after CREATE INDEX");
      requireLongEquals(12 + 3, scalar(db, "select qty from items where id = 2"),
                        when + ": the literal update ran three times");
      db.close();
   }
}

void TestSQLiteFoundation::test_schema_change_repeated() {
   TEST_CASE("test_schema_change_repeated");
   chapeau_tests::ScopedTempDir dir;

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      SQLiteDatabase db(dir.file("schema_repeat" + to_string(pass) + ".sqlite3"));
      db.setLogsErrors(false);
      require(db.open(), when + ": open");
      require(createItems(db), when + ": create and fill the items table");
      db.setShouldCacheStatements(pass == 0);

      for (int round = 1; round <= 6; ++round) {
         const string label = when + ", round " + to_string(round);
         require(exec(db, "alter table items add column c" + to_string(round) + " integer"),
                 label + ": add a column");
         require(exec(db, "create index ix_round_" + to_string(round) + " on items(name, qty)"),
                 label + ": create an index");
         if (round > 1) {
            require(exec(db, "drop index ix_round_" + to_string(round - 1)),
                    label + ": drop the previous index");
         }

         requireStringEquals("banana", nameForId(db, 2), label + ": cached query");
         requireStringEquals("cherry", nameForId(db, 3), label + ": cached query, other parameter");
         requireLongEquals(1, setQty(db, 1000 + round, 2), label + ": cached update");
         requireLongEquals(1000 + round, scalar(db, "select qty from items where id = 2"),
                           label + ": the update took effect");
         requireLongEquals(4 + round, starColumnCount(db, 1),
                           label + ": select * follows the columns");
      }
      db.close();
   }
}

void TestSQLiteFoundation::test_schema_change_then_constraint_error() {
   TEST_CASE("test_schema_change_then_constraint_error");
   chapeau_tests::ScopedTempDir dir;

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      SQLiteDatabase db(dir.file("schema_constraint" + to_string(pass) + ".sqlite3"));
      db.setLogsErrors(false);
      require(db.open(), when + ": open");
      require(createItems(db), when + ": create and fill the items table");
      db.setShouldCacheStatements(pass == 0);

      const char* const insertSql = "insert into items(id,name) values (?,?)";
      unsigned long rowsAffected = 0;
      const auto insert = [&](long id, const string& name) {
         DBStatementArgs args;
         args.add(new DBLong(id));
         args.add(new DBString(name));
         return db.executeUpdate(insertSql, args, rowsAffected);
      };

      require(insert(10, "ten"), when + ": insert before");
      require(!insert(10, "again"), when + ": duplicate key refused");
      requireLongEquals(SQLITE_CONSTRAINT, db.lastErrorCode() & 0xff,
                        when + ": and reported as a constraint error");

      require(exec(db, "alter table items add column extra integer"), when + ": alter table");

      // after the schema change the same statement still reports the real error ...
      require(!insert(10, "again"), when + ": duplicate key still refused after ALTER TABLE");
      requireLongEquals(SQLITE_CONSTRAINT, db.lastErrorCode() & 0xff,
                        when + ": as a constraint error, not as a schema error");
      // ... and still inserts when the data is valid
      require(insert(11, "eleven"), when + ": a valid insert after the refused one");
      requireLongEquals(1, rowsAffected, when + ": one row");

      require(exec(db, "create index ix_items_name on items(name)"), when + ": create index");
      require(!insert(11, "dup"), when + ": duplicate refused after CREATE INDEX");
      require(insert(12, "twelve"), when + ": valid insert recovers");

      requireLongEquals(3 + 3, scalar(db, "select count(*) from items"),
                        when + ": exactly the three valid inserts were stored");
      requireStringEquals("eleven", nameForId(db, 11), when + ": a stored row reads back");
      db.close();
   }
}

void TestSQLiteFoundation::test_schema_change_with_independent_cursors() {
   TEST_CASE("test_schema_change_with_independent_cursors");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("schema_cursors.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(createItems(db), "create and fill the items table");

   const char* const sql = "select name from items where id >= ? order by id";

   // warm the cached statement, change the schema, then run two cursors at once
   for (int round = 0; round < 3; ++round) {
      const string label = "round " + to_string(round);
      {
         DBStatementArgs args;
         args.add(new DBLong(1));
         unique_ptr<SQLiteResultSet> warm(openCursor(db, sql, args));
         require(warm && warm->next(), label + ": warm-up");
      }
      require(exec(db, "alter table items add column x" + to_string(round) + " integer"),
              label + ": alter table");

      DBStatementArgs from1;
      from1.add(new DBLong(1));
      DBStatementArgs from3;
      from3.add(new DBLong(3));
      unique_ptr<SQLiteResultSet> r1(openCursor(db, sql, from1));
      unique_ptr<SQLiteResultSet> r2(openCursor(db, sql, from3));
      require(r1 && r2, label + ": two cursors after the schema change");
      if (r1 && r2) {
         require(r1->next(), label + ": r1 row 1");
         requireStringEquals("apple", firstText(*r1), label + ": r1 first row");
         require(r2->next(), label + ": r2 row 1");
         requireStringEquals("cherry", firstText(*r2), label + ": r2 sees only id >= 3");
         require(r1->next(), label + ": r1 row 2");
         requireStringEquals("banana", firstText(*r1), label + ": r1 second row");
         require(!r2->next(), label + ": r2 ends");
         require(r1->next(), label + ": r1 row 3");
         requireStringEquals("cherry", firstText(*r1), label + ": r1 third row");
         require(!r1->next(), label + ": r1 ends");
      }
   }
   requireLongEquals(1, scalar(db, "select count(*) from pragma_table_info('items') where name = 'x2'"),
                     "the schema really changed three times");
   db.close();
}

// ---- busy timeout and result-set status (CH-07, CH-08) ---------------------

void TestSQLiteFoundation::test_busy_timeout_configuration() {
   TEST_CASE("test_busy_timeout_configuration");
   chapeau_tests::ScopedTempDir dir;

   // PRAGMA busy_timeout reports what SQLite itself has in force (milliseconds)
   const auto sqliteTimeout = [](SQLiteDatabase& d) {
      return scalar(d, "pragma busy_timeout", -1);
   };

   requireLongEquals(5000, SQLiteDatabase::DEFAULT_BUSY_TIMEOUT_MS, "the default is 5000 ms");

   SQLiteDatabase db(dir.file("busy_config.sqlite3"));
   requireLongEquals(5000, db.busyRetryTimeout(), "a new database reports the default");
   require(db.open(), "open");
   requireLongEquals(5000, db.busyRetryTimeout(), "still the default once open");
   requireLongEquals(5000, sqliteTimeout(db), "and SQLite has it: 5000 ms");

   db.setBusyRetryTimeout(250);
   requireLongEquals(250, db.busyRetryTimeout(), "a positive value reads back");
   requireLongEquals(250, sqliteTimeout(db), "and takes effect on the open connection");

   db.setBusyRetryTimeout(0);
   requireLongEquals(0, db.busyRetryTimeout(), "zero reads back");
   requireLongEquals(0, sqliteTimeout(db), "and means no waiting in SQLite");

   db.setBusyRetryTimeout(-5);
   requireLongEquals(0, db.busyRetryTimeout(), "a negative value is treated as zero");
   requireLongEquals(0, sqliteTimeout(db), "also in SQLite");

   db.setBusyRetryTimeout(1234);
   requireLongEquals(1234, sqliteTimeout(db), "and it can be raised again");

   // set while closed: kept, and applied when the connection is opened
   db.close();
   db.setBusyRetryTimeout(77);
   requireLongEquals(77, db.busyRetryTimeout(), "set on a closed database");
   require(db.open(), "reopen");
   requireLongEquals(77, sqliteTimeout(db), "applied by open()");
   db.close();
   require(db.open(), "reopen again");
   requireLongEquals(77, sqliteTimeout(db), "kept across close() and open()");
   db.close();

   // set before the first open()
   SQLiteDatabase other(dir.file("busy_config2.sqlite3"));
   other.setBusyRetryTimeout(321);
   require(other.open(), "open the second database");
   requireLongEquals(321, other.busyRetryTimeout(), "set before open() reads back");
   requireLongEquals(321, sqliteTimeout(other), "set before open() is applied by open()");

   // a second connection to the same file has its own timeout
   SQLiteDatabase third(dir.file("busy_config2.sqlite3"));
   require(third.open(), "a second connection");
   requireLongEquals(5000, sqliteTimeout(third), "which starts at the default");
   requireLongEquals(321, sqliteTimeout(other), "without changing the first");
   other.close();
   third.close();
}

void TestSQLiteFoundation::test_result_set_status_transitions() {
   TEST_CASE("test_result_set_status_transitions");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("status.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(createItems(db), "create and fill the items table");

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      {
         // Ready -> Row -> Row -> Row -> Done, through the base-class interface
         unique_ptr<DBResultSet> rs(db.executeQuery(ITEMS_BY_ID));
         require(rs != nullptr, when + ": a result set");
         if (rs) {
            require(rs->status() == DBResultSetStatus::Ready, when + ": new result set is Ready");
            requireLongEquals(0, rs->lastErrorCode(), when + ": with no error");
            for (int row = 1; row <= 3; ++row) {
               require(rs->next(), when + ": row " + to_string(row));
               require(rs->status() == DBResultSetStatus::Row, when + ": status Row");
               requireLongEquals(0, rs->lastErrorCode(), when + ": no error on a row");
            }
            require(!rs->next(), when + ": exhaustion");
            require(rs->status() == DBResultSetStatus::Done, when + ": status Done");
            requireLongEquals(0, rs->lastErrorCode(), when + ": exhaustion is not an error");
            for (int again = 0; again < 3; ++again) {
               require(!rs->next(), when + ": next() after exhaustion is false");
               require(rs->status() == DBResultSetStatus::Done, when + ": and stays Done");
            }
            rs->close();
            require(rs->status() == DBResultSetStatus::Done, when + ": close() after Done keeps Done");
         }
      }

      {
         // explicit close(): before any step, and part way through
         unique_ptr<DBResultSet> unused(db.executeQuery(ITEMS_BY_ID));
         require(unused != nullptr, when + ": a result set to close unused");
         if (unused) {
            unused->close();
            require(unused->status() == DBResultSetStatus::Done, when + ": closed while Ready: Done");
            require(!unused->next(), when + ": next() is false after close()");
         }
         unique_ptr<DBResultSet> half(db.executeQuery(ITEMS_BY_ID));
         require(half && half->next(), when + ": a row");
         if (half) {
            half->close();
            half->close();
            require(half->status() == DBResultSetStatus::Done, when + ": closed while Row: Done");
            requireLongEquals(0, half->lastErrorCode(), when + ": close() is not an error");
            require(!half->next(), when + ": next() is false after close()");
            require(half->status() == DBResultSetStatus::Done, when + ": and still Done");
         }
      }

      {
         // a failure: Error, with SQLite's own code, kept after the statement is released
         unique_ptr<SQLiteResultSet> rs(
            openCursor(db, "select abs(-9223372036854775807 - 1)"));
         require(rs != nullptr, when + ": a result set that will fail");
         if (rs) {
            require(rs->status() == DBResultSetStatus::Ready, when + ": Ready before the failing step");
            require(!rs->next(), when + ": the failing step returns false");
            require(rs->status() == DBResultSetStatus::Error, when + ": status Error, not Done");
            requireLongEquals(SQLITE_ERROR, rs->lastErrorCode(), when + ": SQLite's code");
            require(rs->statement() == nullptr, when + ": the statement has been released");
            for (int again = 0; again < 3; ++again) {
               require(!rs->next(), when + ": next() after an error is false");
               require(rs->status() == DBResultSetStatus::Error, when + ": and stays Error");
               requireLongEquals(SQLITE_ERROR, rs->lastErrorCode(), when + ": with the code kept");
            }
            rs->close();
            require(rs->status() == DBResultSetStatus::Error, when + ": close() does not hide the error");
            requireLongEquals(SQLITE_ERROR, rs->lastErrorCode(), when + ": nor its code");
            requireLongEquals(0, rs->getColumnCount(), when + ": accessors stay safe after an error");
            requireLongEquals(0, rs->longForColumnIndex(0), when + ": all of them");
         }
      }

      {
         // a specific code: a constraint failure surfaces as itself, not as SQLITE_ERROR
         unique_ptr<DBResultSet> rs(
            db.executeQuery("insert into items(id,name) values (1,'duplicate')"));
         require(rs != nullptr, when + ": a lazy insert");
         if (rs) {
            require(!rs->next(), when + ": the duplicate key stops it");
            require(rs->status() == DBResultSetStatus::Error, when + ": status Error");
            requireLongEquals(SQLITE_CONSTRAINT, rs->lastErrorCode() & 0xff,
                              when + ": a constraint error");
         }
         // a statement that succeeds is Done, not Error
         unique_ptr<DBResultSet> ok(
            db.executeQuery("insert into items(id,name) values (50,'fifty')"));
         require(ok && !ok->next(), when + ": a lazy insert that works returns false");
         if (ok) {
            require(ok->status() == DBResultSetStatus::Done, when + ": and is Done");
            requireLongEquals(0, ok->lastErrorCode(), when + ": with no error");
         }
         requireLongEquals(1, scalar(db, "select count(*) from items where id = 50"),
                           when + ": the insert ran");
         require(exec(db, "delete from items where id = 50"), when + ": remove it again");
      }
   }
   db.setShouldCacheStatements(true);

   {
      // invalidation: only an active result set becomes Invalidated; one that
      // had ended keeps how it ended
      unique_ptr<SQLiteResultSet> ready(openCursor(db, ITEMS_BY_ID));
      unique_ptr<SQLiteResultSet> onRow(openCursor(db, ITEMS_BY_ID));
      unique_ptr<SQLiteResultSet> done(openCursor(db, ITEMS_BY_ID));
      unique_ptr<SQLiteResultSet> failed(
         openCursor(db, "insert into items(id,name) values (1,'duplicate')"));
      require(ready && onRow && done && failed, "four result sets");
      if (ready && onRow && done && failed) {
         require(onRow->next(), "one is on a row");
         while (done->next()) {
         }
         require(!failed->next(), "one has failed");

         db.close();

         require(ready->status() == DBResultSetStatus::Invalidated, "Ready -> Invalidated");
         require(onRow->status() == DBResultSetStatus::Invalidated, "Row -> Invalidated");
         require(done->status() == DBResultSetStatus::Done, "Done stays Done");
         require(failed->status() == DBResultSetStatus::Error, "Error stays Error");
         requireLongEquals(SQLITE_CONSTRAINT, failed->lastErrorCode() & 0xff,
                           "and keeps its error code");
         requireLongEquals(0, ready->lastErrorCode(), "invalidation is not an error");
         require(!ready->next() && !onRow->next(), "next() on an invalidated result set is false");
         require(ready->status() == DBResultSetStatus::Invalidated, "and it stays Invalidated");
         ready->close();
         require(ready->status() == DBResultSetStatus::Invalidated, "close() does not turn it into Done");

         require(db.open(), "reopen");
         require(ready->status() == DBResultSetStatus::Invalidated &&
                 onRow->status() == DBResultSetStatus::Invalidated,
                 "reopening does not revive them");
      }
   }

   {
      // the database is destroyed first: the status says so, and destroying
      // the result sets afterwards is safe
      auto other = make_unique<SQLiteDatabase>(dir.file("status2.sqlite3"));
      require(other->open() && createItems(*other), "a second database");
      unique_ptr<SQLiteResultSet> active(openCursor(*other, ITEMS_BY_ID));
      unique_ptr<SQLiteResultSet> finished(openCursor(*other, ITEMS_BY_ID));
      require(active && finished && active->next(), "two result sets");
      if (active && finished) {
         while (finished->next()) {
         }
         other.reset();
         require(active->status() == DBResultSetStatus::Invalidated,
                 "destroyed under an active result set: Invalidated");
         require(finished->status() == DBResultSetStatus::Done,
                 "destroyed after exhaustion: still Done");
         require(!active->next() && !finished->next(), "no more rows from either");
      }
   }
   db.close();
}

void TestSQLiteFoundation::test_close_with_unfinalized_statement() {
   TEST_CASE("test_close_with_unfinalized_statement");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("close_unfinalized.sqlite3"));
   require(db.open(), "open");

   // a statement prepared behind Chapeau's back is one close() cannot finalize:
   // sqlite3_close() refuses while it exists. close() must neither wait for
   // that to change nor leak the connection.
   sqlite3_stmt* raw = nullptr;
   require(sqlite3_prepare_v2(db.sqliteHandle(), "select 1", -1, &raw, nullptr) == SQLITE_OK,
           "prepare a statement directly on the connection");
   db.close();
   require(db.sqliteHandle() == nullptr, "close() returns, and the database is closed");

   // finalizing the last statement lets SQLite free the connection
   // (the leak check of a sanitizer build would notice if it did not)
   require(sqlite3_finalize(raw) == SQLITE_OK, "finalize the statement afterwards");
}

// ---- transaction state, opening, and the bounded statement cache (CH-11, CH-12, CH-13) ----

namespace {

// the SQL text of every statement that is open on the connection, sorted:
// what SQLite itself holds, independently of what Chapeau thinks it holds
vector<string> openStatementSql(SQLiteDatabase& db) {
   vector<string> sql;
   if (db.sqliteHandle() == nullptr) {
      return sql;
   }
   for (sqlite3_stmt* s = sqlite3_next_stmt(db.sqliteHandle(), nullptr);
        s != nullptr;
        s = sqlite3_next_stmt(db.sqliteHandle(), s)) {
      const char* text = sqlite3_sql(s);
      sql.push_back(text != nullptr ? text : "");
   }
   sort(sql.begin(), sql.end());
   return sql;
}

// the open statements as one readable string, for failure messages
string describe(const vector<string>& sql) {
   string out = "{";
   for (size_t i = 0; i < sql.size(); ++i) {
      out += (i == 0 ? "" : ", ") + sql[i];
   }
   return out + "}";
}

// "select <n> * 2": a distinct statement for every n, with an answer known
// without asking Chapeau
long long runDistinctQuery(SQLiteDatabase& db, int n) {
   return scalar(db, "select " + to_string(n) + " * 2");
}

}

void TestSQLiteFoundation::test_transaction_state_follows_sqlite() {
   TEST_CASE("test_transaction_state_follows_sqlite");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("txn_state.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);

   require(!db.inTransaction(), "a database that was never opened is not in a transaction");
   require(db.open(), "open");
   require(!db.inTransaction(), "a new connection is not in a transaction");
   require(exec(db, "create table acct(id integer primary key, v integer)"), "create table");

   // the helpers
   require(db.beginTransaction(), "begin");
   require(db.inTransaction(), "in a transaction after begin");
   require(!db.beginTransaction(), "a second begin is refused");
   require(db.inTransaction(), "and the transaction is still open");
   require(db.commit(), "commit");
   require(!db.inTransaction(), "not in a transaction after commit");
   require(db.beginDeferredTransaction(), "begin deferred");
   require(db.inTransaction(), "in a transaction after begin deferred");
   require(db.rollback(), "rollback");
   require(!db.inTransaction(), "not in a transaction after rollback");

   // SQL run outside the helpers is reflected, in both directions
   require(exec(db, "BEGIN"), "BEGIN as plain SQL");
   require(db.inTransaction(), "a BEGIN run as plain SQL is a transaction");
   require(exec(db, "COMMIT"), "COMMIT as plain SQL");
   require(!db.inTransaction(), "a COMMIT run as plain SQL ends it");

   require(db.beginTransaction(), "begin with the helper");
   require(exec(db, "COMMIT"), "COMMIT as plain SQL after the helper's begin");
   require(!db.inTransaction(), "the helper's transaction, committed behind its back, is over");
   require(!db.commit(), "commit() with no transaction open fails");
   require(!db.inTransaction(), "and the state stays false");

   require(db.beginTransaction(), "begin with the helper again");
   require(exec(db, "ROLLBACK"), "ROLLBACK as plain SQL");
   require(!db.inTransaction(), "a ROLLBACK run as plain SQL ends it");

   // a statement can end a transaction without anyone saying COMMIT or ROLLBACK
   require(exec(db, "insert into acct(id,v) values (1,10)"), "a row");
   require(db.beginTransaction(), "begin");
   require(!exec(db, "insert or rollback into acct(id,v) values (1,99)"),
           "a conflict that resolves by rolling back");
   require(!db.inTransaction(), "which has ended the transaction");

   // close() rolls an open transaction back; a reopened connection starts clean
   require(db.beginTransaction(), "begin before closing");
   require(exec(db, "insert into acct(id,v) values (2,20)"), "uncommitted work");
   db.close();
   require(!db.inTransaction(), "a closed database is not in a transaction");
   require(db.open(), "reopen");
   require(!db.inTransaction(), "and the reopened connection is not either");
   requireLongEquals(1, scalar(db, "select count(*) from acct"), "the uncommitted row did not survive");
   db.close();
   require(!db.inTransaction(), "closed again");
}

void TestSQLiteFoundation::test_open_when_already_open() {
   TEST_CASE("test_open_when_already_open");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("open_twice.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(createItems(db), "create and fill the items table");
   sqlite3* const handle = db.sqliteHandle();
   require(handle != nullptr, "a connection");

   // plain double open
   require(!db.open(), "open() on an open database fails");
   require(db.sqliteHandle() == handle, "and the connection is the same one");
   requireLongEquals(3, scalar(db, "select count(*) from items"), "and works");
   requireLongEquals(0, db.lastErrorCode(), "without reporting an error of its own");

   // with an active result set
   {
      unique_ptr<SQLiteResultSet> rs(openCursor(db, ITEMS_BY_ID));
      require(rs && rs->next(), "an active result set, on its first row");
      require(!db.open(), "open() with an active result set fails");
      require(db.sqliteHandle() == handle, "the connection is unchanged");
      if (rs) {
         require(rs->status() == DBResultSetStatus::Row, "the result set was not invalidated");
         requireLongEquals(1, rs->intForColumnIndex(0), "it is where it was");
         require(rs->next(), "it continues");
         requireLongEquals(2, rs->intForColumnIndex(0), "to the second row");
         require(rs->next() && !rs->next(), "and to the end");
         require(rs->status() == DBResultSetStatus::Done, "which is a normal end");
      }
   }

   // with an open transaction
   {
      require(db.beginTransaction(), "begin");
      require(exec(db, "insert into items(id,name) values (7,'seven')"), "work in the transaction");
      require(!db.open(), "open() inside a transaction fails");
      require(db.sqliteHandle() == handle, "the connection is unchanged");
      require(db.inTransaction(), "the transaction is intact");
      requireLongEquals(1, scalar(db, "select count(*) from items where id = 7"),
                        "so is its uncommitted work");
      require(db.commit(), "it commits");
      require(!db.inTransaction(), "and ends");
      requireLongEquals(1, countFromNewConnection(path, "items where id = 7"),
                        "another connection sees the committed row");
   }

   // with prepared statements: the cache is untouched
   {
      for (int n = 0; n < 5; ++n) {
         runDistinctQuery(db, n);
      }
      const vector<string> before = openStatementSql(db);
      require(before.size() >= 5, "statements are cached");
      require(!db.open(), "open() with cached statements fails");
      const vector<string> after = openStatementSql(db);
      require(before == after,
              "the same statements are open afterwards: " + describe(before) + " / " + describe(after));
   }

   // the busy timeout configuration is kept
   db.setBusyRetryTimeout(321);
   require(!db.open(), "open() once more fails");
   requireLongEquals(321, db.busyRetryTimeout(), "the busy timeout reads back unchanged");
   requireLongEquals(321, scalar(db, "pragma busy_timeout"), "and so does SQLite's");

   // close() is still safe and may be repeated, and open() works after it
   db.close();
   db.close();
   require(db.sqliteHandle() == nullptr, "closed");
   require(db.open(), "open() on a closed database works");
   requireLongEquals(4, scalar(db, "select count(*) from items"), "with the committed data");
   require(!db.open(), "and fails again while open");
   db.close();
   db.close();
}

void TestSQLiteFoundation::test_failed_open_recovers() {
   TEST_CASE("test_failed_open_recovers");
   chapeau_tests::ScopedTempDir dir;
   const fs::path missing = fs::path(dir.file("not_there_yet"));
   SQLiteDatabase db((missing / "late.sqlite3").string());
   db.setLogsErrors(false);
   db.setBusyRetryTimeout(444);

   for (int attempt = 0; attempt < 3; ++attempt) {
      const string label = "attempt " + to_string(attempt);
      require(!db.open(), label + ": open() fails, the directory does not exist");
      require(db.sqliteHandle() == nullptr, label + ": no handle is left behind");
      requireLongEquals(SQLITE_CANTOPEN, db.lastErrorCode(), label + ": the reason is available");
      requireStringEquals("unable to open database file", db.lastErrorMessage(),
                          label + ": with SQLite's own message");
      require(db.hadError(), label + ": hadError()");
      require(!db.goodConnection(), label + ": no good connection");
      require(!exec(db, "create table z(a integer)"), label + ": nothing can be run on it");
      require(!db.inTransaction(), label + ": not in a transaction");
   }
   db.close();   // nothing to close, and that is fine
   db.close();

   // the object recovers once the file can be created
   require(fs::create_directory(missing), "create the directory");
   require(db.open(), "open() now works");
   require(db.sqliteHandle() != nullptr, "with a connection");
   requireLongEquals(SQLITE_OK, db.lastErrorCode(), "and no error is reported");
   require(!db.hadError(), "hadError() is false");
   requireLongEquals(444, scalar(db, "pragma busy_timeout"), "the configured busy timeout was applied");
   require(db.goodConnection(), "a good connection");
   require(exec(db, "create table z(a integer)"), "which works");
   require(exec(db, "insert into z(a) values (5)"), "and stores data");
   requireLongEquals(5, scalar(db, "select a from z"), "that reads back");
   db.close();
   require(!db.hadError(), "a closed database that opened fine reports no error");
}

void TestSQLiteFoundation::test_statement_cache_is_bounded() {
   TEST_CASE("test_statement_cache_is_bounded");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("cache_bound.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");

   requireLongEquals(128, SQLiteDatabase::DEFAULT_STATEMENT_CACHE_LIMIT, "the default limit is 128");
   requireLongEquals(128, static_cast<long long>(db.statementCacheLimit()), "a new database has it");
   require(db.shouldCacheStatements(), "and caches");

   // thousands of distinct statements, every answer checked
   for (int n = 0; n < 3000; ++n) {
      if (runDistinctQuery(db, n) != 2LL * n) {
         requireLongEquals(2LL * n, runDistinctQuery(db, n), "answer for statement " + to_string(n));
         break;
      }
   }
   requireLongEquals(128, static_cast<long long>(openStatementSql(db).size()),
                     "3000 distinct queries leave exactly 128 statements open");

   // updates and inserts go through the same cache
   require(exec(db, "create table log(id integer primary key, msg text)"), "create table");
   require(db.beginDeferredTransaction(), "one transaction for many inserts");
   for (int n = 0; n < 1500; ++n) {
      if (!exec(db, "insert into log(id,msg) values (" + to_string(n) + ",'m')")) {
         require(false, "insert " + to_string(n));
         break;
      }
   }
   require(db.commit(), "commit");
   requireLongEquals(128, static_cast<long long>(openStatementSql(db).size()),
                     "1500 distinct inserts do not grow it");
   requireLongEquals(1500, scalar(db, "select count(*) from log"), "and every insert ran");

   // the limit is independent of the on/off switch, and survives close()/open()
   db.setShouldCacheStatements(false);
   require(!db.shouldCacheStatements(), "switched off");
   requireLongEquals(128, static_cast<long long>(db.statementCacheLimit()), "the limit is kept");
   requireLongEquals(0, static_cast<long long>(openStatementSql(db).size()), "and the cache is empty");
   db.setShouldCacheStatements(true);
   require(db.shouldCacheStatements(), "switched on again");
   db.close();
   require(db.open(), "reopen");
   requireLongEquals(128, static_cast<long long>(db.statementCacheLimit()), "the limit survives close/open");
   db.close();
}

void TestSQLiteFoundation::test_statement_cache_limit_zero() {
   TEST_CASE("test_statement_cache_limit_zero");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("cache_zero.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");

   for (int n = 0; n < 10; ++n) {
      runDistinctQuery(db, n);
   }
   require(openStatementSql(db).size() == 10, "ten statements are cached");

   db.setStatementCacheLimit(0);
   requireLongEquals(0, static_cast<long long>(db.statementCacheLimit()), "the limit reads back as 0");
   require(!db.shouldCacheStatements(), "a limit of 0 means no caching");
   requireLongEquals(0, static_cast<long long>(openStatementSql(db).size()),
                     "and the cache was emptied at once");

   for (int n = 0; n < 50; ++n) {
      requireLongEquals(2LL * n, runDistinctQuery(db, n), "query " + to_string(n) + " works uncached");
   }
   requireLongEquals(0, static_cast<long long>(openStatementSql(db).size()), "nothing is left open");
   unsigned long rowsAffected = 0;
   require(db.executeUpdate("create table t(a integer)", rowsAffected), "an update works uncached");
   requireLongEquals(0, static_cast<long long>(openStatementSql(db).size()), "and leaves nothing open");

   // the switch cannot turn caching on while the limit is 0 ...
   db.setShouldCacheStatements(true);
   require(!db.shouldCacheStatements(), "switching on does not override a limit of 0");
   runDistinctQuery(db, 1);
   requireLongEquals(0, static_cast<long long>(openStatementSql(db).size()), "so nothing is cached");

   // ... and a positive limit does
   db.setStatementCacheLimit(5);
   require(db.shouldCacheStatements(), "a positive limit caches again");
   for (int n = 0; n < 20; ++n) {
      runDistinctQuery(db, n);
   }
   requireLongEquals(5, static_cast<long long>(openStatementSql(db).size()), "up to the new limit");

   // setShouldCacheStatements(false) is the same as 0 as far as the cache goes
   db.setShouldCacheStatements(false);
   requireLongEquals(0, static_cast<long long>(openStatementSql(db).size()), "emptied");
   runDistinctQuery(db, 3);
   requireLongEquals(0, static_cast<long long>(openStatementSql(db).size()), "and not refilled");
   db.close();
}

void TestSQLiteFoundation::test_statement_cache_limit_one() {
   TEST_CASE("test_statement_cache_limit_one");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("cache_one.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");
   db.setStatementCacheLimit(1);

   const auto cachedSql = [&db]() { return openStatementSql(db); };
   const auto onlyStatement = [&db]() {
      return sqlite3_next_stmt(db.sqliteHandle(), nullptr);
   };

   requireLongEquals(2, runDistinctQuery(db, 1), "query 1");
   require(cachedSql() == vector<string>{"select 1 * 2"}, "it is the one cached statement");
   sqlite3_stmt* const first = onlyStatement();

   requireLongEquals(2, runDistinctQuery(db, 1), "query 1 again");
   require(onlyStatement() == first, "the same statement was reused, not prepared again");

   requireLongEquals(4, runDistinctQuery(db, 2), "query 2");
   require(cachedSql() == vector<string>{"select 2 * 2"}, "it replaced query 1 as the one cached statement");

   requireLongEquals(2, runDistinctQuery(db, 1), "query 1 once more");
   require(cachedSql() == vector<string>{"select 1 * 2"}, "and replaced query 2 in turn");
   requireLongEquals(1, static_cast<long long>(cachedSql().size()), "never more than one");
   db.close();
}

void TestSQLiteFoundation::test_statement_cache_lru_order() {
   TEST_CASE("test_statement_cache_lru_order");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("cache_lru.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");
   db.setStatementCacheLimit(3);

   const auto sql = [](int n) { return "select " + to_string(n) + " * 2"; };
   const auto expectCached = [&](const vector<int>& ns, const string& label) {
      vector<string> expected;
      for (int n : ns) {
         expected.push_back(sql(n));
      }
      sort(expected.begin(), expected.end());
      const vector<string> actual = openStatementSql(db);
      require(actual == expected,
              label + ": expected " + describe(expected) + ", SQLite holds " + describe(actual));
   };

   runDistinctQuery(db, 1);
   runDistinctQuery(db, 2);
   runDistinctQuery(db, 3);
   expectCached({1, 2, 3}, "after 1, 2, 3");

   runDistinctQuery(db, 1);      // 1 becomes the most recently used; 2 the least
   runDistinctQuery(db, 4);      // evicts 2
   expectCached({1, 3, 4}, "after using 1 and adding 4 (2 was least recently used)");

   runDistinctQuery(db, 3);      // order, most to least recent: 3, 4, 1
   runDistinctQuery(db, 5);      // evicts 1
   expectCached({3, 4, 5}, "after using 3 and adding 5 (1 was least recently used)");

   runDistinctQuery(db, 2);      // 2 was evicted: prepared again; evicts 4
   expectCached({2, 3, 5}, "after 2 came back (4 was least recently used)");

   // an update statement takes part in the same order
   unsigned long rowsAffected = 0;
   require(db.executeUpdate("create table lru(a integer)", rowsAffected), "an update statement");
   vector<string> expectedAfterUpdate = {sql(2), sql(5), "create table lru(a integer)"};
   sort(expectedAfterUpdate.begin(), expectedAfterUpdate.end());
   require(openStatementSql(db) == expectedAfterUpdate,
           "the update is cached too, and evicted 3 (the least recently used): " +
           describe(openStatementSql(db)));
   db.close();
}

void TestSQLiteFoundation::test_statement_cache_eviction_with_active_cursors() {
   TEST_CASE("test_statement_cache_eviction_with_active_cursors");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("cache_active.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(createItemsWithoutCachedStatements(db), "create and fill the items table");
   db.setStatementCacheLimit(2);

   const string sqlA = "select id from items order by id";
   const string sqlB = "select name from items order by id";
   const string sqlC = "select qty from items order by id";

   {
      unique_ptr<SQLiteResultSet> a(openCursor(db, sqlA));
      require(a && a->next(), "a cursor on A, on its first row");
      requireLongEquals(1, a ? a->intForColumnIndex(0) : -1, "A's first id");
      require(openStatementSql(db) == vector<string>{sqlA}, "A's statement is cached, and in use");

      runDistinctQuery(db, 1);   // cached: B-like statement #1
      const string filler = "select 1 * 2";
      require(openStatementSql(db) == (vector<string>{filler, sqlA}), "A and a second statement are cached");

      // a third statement evicts the least recently used one: A, which is active
      scalar(db, sqlC);
      vector<string> expected = {filler, sqlA, sqlC};
      sort(expected.begin(), expected.end());
      require(openStatementSql(db) == expected,
              "A was evicted from the cache but is still open: " + describe(openStatementSql(db)));

      // the evicted statement keeps working for its result set
      if (a) {
         require(a->next(), "A's cursor goes on after its statement was evicted");
         requireLongEquals(2, a->intForColumnIndex(0), "with the right second id");
         require(a->status() == DBResultSetStatus::Row, "still a normal row");
         require(a->next() && !a->next(), "to the end");
         require(a->status() == DBResultSetStatus::Done, "which is a normal end");
      }
      // it ended: its statement, owned by the result set since the eviction, is finalized
      vector<string> afterEnd = {filler, sqlC};
      sort(afterEnd.begin(), afterEnd.end());
      require(openStatementSql(db) == afterEnd,
              "once A's cursor ended its statement is gone: " + describe(openStatementSql(db)));
   }

   // the same, but the cursor is simply destroyed part way
   {
      db.clearCachedStatements();
      unique_ptr<SQLiteResultSet> b(openCursor(db, sqlB));
      require(b && b->next(), "a cursor on B");
      scalar(db, sqlA);
      scalar(db, sqlC);          // evicts B
      require(openStatementSql(db).size() == 3, "B is evicted but open, besides A and C");
      b.reset();
      require(openStatementSql(db).size() == 2, "destroying the cursor finalizes it");
   }
   db.close();
}

void TestSQLiteFoundation::test_statement_cache_lowering_limit_with_active_cursors() {
   TEST_CASE("test_statement_cache_lowering_limit_with_active_cursors");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("cache_lower.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(createItemsWithoutCachedStatements(db), "create and fill the items table");
   db.setStatementCacheLimit(10);

   const auto sql = [](int n) { return "select id + " + to_string(n) + " from items order by id"; };
   unique_ptr<SQLiteResultSet> cursors[7];
   for (int n = 1; n <= 6; ++n) {
      // 1 and 3 stay active; the others run to the end at once
      unique_ptr<SQLiteResultSet> rs(openCursor(db, sql(n)));
      require(rs && rs->next(), "query " + to_string(n));
      if (n == 1 || n == 3) {
         cursors[n].swap(rs);
      } else {
         while (rs && rs->next()) {
         }
      }
   }
   requireLongEquals(6, static_cast<long long>(openStatementSql(db).size()), "six statements are cached");

   // lower the limit: most recently used first -- 6, 5 stay; 4, 3, 2, 1 go
   db.setStatementCacheLimit(2);
   requireLongEquals(2, static_cast<long long>(db.statementCacheLimit()), "the limit reads back");
   vector<string> expected = {sql(1), sql(3), sql(5), sql(6)};
   sort(expected.begin(), expected.end());
   require(openStatementSql(db) == expected,
           "2 and 4 (idle) were finalized, 1 and 3 (active) were not: " + describe(openStatementSql(db)));

   // the active ones are undisturbed and give the right rows (id + n for ids 1, 2, 3)
   for (int n : {1, 3}) {
      unique_ptr<SQLiteResultSet>& rs = cursors[n];
      require(rs != nullptr, "cursor " + to_string(n));
      if (!rs) {
         continue;
      }
      requireLongEquals(1 + n, rs->longForColumnIndex(0), "cursor " + to_string(n) + " first row");
      require(rs->next(), "cursor " + to_string(n) + " second row");
      requireLongEquals(2 + n, rs->longForColumnIndex(0), "cursor " + to_string(n) + " second value");
   }

   // switching the cache off entirely, with those two still active
   db.setStatementCacheLimit(0);
   expected = {sql(1), sql(3)};
   sort(expected.begin(), expected.end());
   require(openStatementSql(db) == expected,
           "limit 0 finalized the idle ones, the two active statements stay: " + describe(openStatementSql(db)));
   for (int n : {1, 3}) {
      unique_ptr<SQLiteResultSet>& rs = cursors[n];
      if (rs) {
         require(rs->next(), "cursor " + to_string(n) + " third row");
         requireLongEquals(3 + n, rs->longForColumnIndex(0), "cursor " + to_string(n) + " third value");
         require(!rs->next(), "cursor " + to_string(n) + " ends");
         require(rs->status() == DBResultSetStatus::Done, "normally");
      }
   }
   requireLongEquals(0, static_cast<long long>(openStatementSql(db).size()),
                     "when the last active cursor ended, nothing is open");
   db.close();
}

void TestSQLiteFoundation::test_statement_cache_cleanup_on_close() {
   TEST_CASE("test_statement_cache_cleanup_on_close");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("cache_close.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(createItemsWithoutCachedStatements(db), "create and fill the items table");

   for (int n = 0; n < 200; ++n) {
      runDistinctQuery(db, n);
   }
   unique_ptr<SQLiteResultSet> active(openCursor(db, "select id from items order by id"));
   unique_ptr<SQLiteResultSet> evicted(openCursor(db, "select name from items order by id"));
   require(active && evicted && active->next() && evicted->next(), "two active cursors");
   for (int n = 200; n < 400; ++n) {
      runDistinctQuery(db, n);   // evicts the older statements, the cursors' included
   }
   require(openStatementSql(db).size() >= 128, "a full cache, and the evicted statements still open");

   db.close();
   require(db.sqliteHandle() == nullptr, "the connection closed with a full cache and active cursors");
   if (active && evicted) {
      require(active->status() == DBResultSetStatus::Invalidated, "the cursors were invalidated");
      require(evicted->status() == DBResultSetStatus::Invalidated, "including the one whose statement was evicted");
   }

   // everything that was open was finalized: the connection can be reopened clean,
   // and a leak check of a sanitizer build would notice anything left over
   require(db.open(), "reopen");
   requireLongEquals(0, static_cast<long long>(openStatementSql(db).size()), "no statement carried over");
   requireLongEquals(2LL * 7, runDistinctQuery(db, 7), "and queries work again");
   db.close();
}

// ---- data fidelity: integers, text with NULs, blobs, dates (CH-14, CH-15, CH-16, CH-17, CH-19) ----

namespace {

// the first row of a query run on a connection that Chapeau knows nothing
// about, columns joined with '|' and NULL written as "NULL": what SQLite really
// holds, independent of what Chapeau's accessors make of it
string rawFirstRow(const string& path, const string& sql) {
   string out;
   sqlite3* handle = nullptr;
   if (sqlite3_open(path.c_str(), &handle) == SQLITE_OK) {
      sqlite3_stmt* stmt = nullptr;
      if (sqlite3_prepare_v2(handle, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK &&
          sqlite3_step(stmt) == SQLITE_ROW) {
         for (int i = 0; i < sqlite3_column_count(stmt); ++i) {
            const unsigned char* text = sqlite3_column_text(stmt, i);
            out += (i > 0 ? "|" : "");
            out += (text != nullptr) ? reinterpret_cast<const char*>(text) : "NULL";
         }
      } else {
         out = "ERROR " + string(sqlite3_errmsg(handle));
      }
      sqlite3_finalize(stmt);
   }
   sqlite3_close(handle);
   return out;
}

string hexOf(const string& bytes) {
   static const char digits[] = "0123456789ABCDEF";
   string out;
   for (unsigned char c : bytes) {
      out += digits[c >> 4];
      out += digits[c & 15];
   }
   return out;
}

// what an int accessor must return for a stored 64-bit value: the value itself
// if it fits, otherwise the nearest int
int expectedSaturated(long long value) {
   if (value > INT_MAX) {
      return INT_MAX;
   }
   if (value < INT_MIN) {
      return INT_MIN;
   }
   return static_cast<int>(value);
}

// a DBResultSet that knows only the original interface: it overrides the pure
// virtuals and nothing that was added later (status(), int64ForColumn(),
// the checked intForColumn()), as a subclass written before them would
class LegacyResultSet : public DBResultSet {
public:
   long storedLong;
   DBValueType storedType;
   LegacyResultSet() : storedLong(0), storedType(ValueTypeInteger) {}

   bool next() { return false; }
   void close() {}
   int intForColumn(const string&) const { return static_cast<int>(storedLong); }
   int intForColumnIndex(int) const { return static_cast<int>(storedLong); }
   long longForColumn(const string&) const { return storedLong; }
   long longForColumnIndex(int) const { return storedLong; }
   bool boolForColumn(const string&) const { return storedLong != 0; }
   bool boolForColumnIndex(int) const { return storedLong != 0; }
   double doubleForColumn(const string&) const { return 0; }
   double doubleForColumnIndex(int) const { return 0; }
   string* stringForColumn(const string&) const { return nullptr; }
   string* stringForColumnIndex(int) const { return nullptr; }
   bool stringForColumn(const string&, string&) const { return false; }
   bool stringForColumnIndex(int, string&) const { return false; }
   DBDate* dateForColumn(const string&) const { return nullptr; }
   DBDate* dateForColumnIndex(int) const { return nullptr; }
   DBData* dataForColumn(const string&) const { return nullptr; }
   DBData* dataForColumnIndex(int) const { return nullptr; }
   int getColumnCount() const { return 1; }
   DBValueType getTypeForColumnIndex(int) const { return storedType; }
};

}

void TestSQLiteFoundation::test_text_with_nul_bytes() {
   TEST_CASE("test_text_with_nul_bytes");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("text_nul.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "create table txt(id integer primary key, s text)"), "create table");

   string longValue(1000, 'x');
   longValue[500] = '\0';
   const vector<string> inputs = {
      string(),                      // empty string
      string("ab\0cd", 5),           // embedded NUL
      string("\0abc", 4),            // leading NUL
      string("abc\0", 4),            // trailing NUL
      string("\0", 1),               // only a NUL
      string("\0\0\0", 3),           // repeated NULs
      string("h\xC3\xA9llo w\xC3\xB6rld \xE2\x9C\x93"),   // ordinary UTF-8
      longValue                      // a long value with a NUL in the middle
   };

   for (size_t n = 0; n < inputs.size(); ++n) {
      const string& input = inputs[n];
      DBStatementArgs args;
      args.add(new DBLong(static_cast<long>(n + 1)));
      args.add(new DBString(input));
      unsigned long rowsAffected = 0;
      require(db.executeUpdate("insert into txt(id,s) values (?,?)", args, rowsAffected),
              "insert input " + to_string(n));

      // what SQLite holds, from another connection: all the bytes, as text
      requireStringEquals("text:" + to_string(input.size()) + ":" + hexOf(input),
         rawFirstRow(path, "select typeof(s)||':'||length(cast(s as blob))||':'||hex(s) from txt where id = " +
                           to_string(n + 1)),
         "input " + to_string(n) + " as stored");
   }

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      for (size_t n = 0; n < inputs.size(); ++n) {
         const string& input = inputs[n];
         const string label = when + ", input " + to_string(n) + " (" + to_string(input.size()) + " bytes)";
         DBStatementArgs args;
         args.add(new DBLong(static_cast<long>(n + 1)));
         unique_ptr<SQLiteResultSet> rs(
            openCursor(db, "select s as txt from txt where id = ?", args));
         require(rs && rs->next(), label + ": a row");
         if (!rs) {
            continue;
         }

         string byReference = "unchanged";
         require(rs->stringForColumnIndex(0, byReference), label + ": string& is not NULL");
         require(byReference == input, label + ": string& returns every byte (got " +
                                       to_string(byReference.size()) + ")");

         unique_ptr<string> byPointer(rs->stringForColumnIndex(0));
         require(byPointer != nullptr, label + ": string* is not NULL");
         require(byPointer && *byPointer == input, label + ": string* returns every byte");

         string byName = "unchanged";
         require(rs->stringForColumn("txt", byName), label + ": by name is not NULL");
         require(byName == input, label + ": by name returns every byte");

         unique_ptr<string> byNamePointer(rs->stringForColumn("txt"));
         require(byNamePointer && *byNamePointer == input, label + ": by name, string*, every byte");
      }
   }
   db.setShouldCacheStatements(true);

   // NULL is still NULL, and still distinguishable from the empty string
   require(exec(db, "insert into txt(id,s) values (100, NULL)"), "a NULL text");
   unique_ptr<SQLiteResultSet> rs(openCursor(db, "select s as txt from txt where id = 100"));
   require(rs && rs->next(), "the NULL row");
   if (rs) {
      string value = "unchanged";
      require(!rs->stringForColumnIndex(0, value), "NULL text: string& is false");
      requireStringEquals("", value, "and the string is cleared");
      unique_ptr<string> pointer(rs->stringForColumnIndex(0));
      require(pointer == nullptr, "NULL text: string* is nullptr");
      unique_ptr<string> byName(rs->stringForColumn("txt"));
      require(byName == nullptr, "NULL text by name: string* is nullptr");
   }
   rs.reset();
   db.close();
}

void TestSQLiteFoundation::test_null_empty_and_nonempty_blobs() {
   TEST_CASE("test_null_empty_and_nonempty_blobs");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("blobs3.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "create table blobs(id integer primary key, b blob)"), "create table");

   const string small("ab\0c", 4);
   string big;
   for (int i = 0; i < 70000; ++i) {
      big.push_back(static_cast<char>((i * 31 + 7) % 256));
   }

   // the DBData class itself: nothing is copied for a zero length or no buffer
   {
      DBData fromEmptyBuffer(small.data(), 0);
      requireLongEquals(0, fromEmptyBuffer.length(), "DBData(buffer, 0) is empty");
      require(fromEmptyBuffer.value() == nullptr, "and has no data pointer");
      DBData fromNoBuffer(nullptr, 5);
      requireLongEquals(0, fromNoBuffer.length(), "DBData(nullptr, 5) is empty, not an invalid copy");
      DBData sized(small.data(), small.size());
      requireLongEquals(4, sized.length(), "DBData(buffer, 4) has 4 bytes");
      require(::memcmp(sized.value(), small.data(), 4) == 0, "which are the bytes given");
      DBData copy(fromEmptyBuffer);
      DBData assigned;
      assigned = sized;
      requireLongEquals(0, copy.length(), "a copy of an empty DBData is empty");
      requireLongEquals(4, assigned.length(), "an assigned DBData has the bytes");
   }

   struct Case {
      long id;
      const char* label;
      DBAbstractDataType* value;      // null: a nullptr argument
      const char* sqlValue;           // used instead when not null
      string expectedStored;          // "<typeof>:<length or ->"
      const string* expectedBytes;    // set when a non-empty blob is expected back
   };
   const Case cases[] = {
      {1, "DBData with 4 bytes", new DBData(small.data(), small.size()), nullptr, "blob:4", &small},
      {2, "DBData with an empty buffer", new DBData(small.data(), 0), nullptr, "blob:0", nullptr},
      {3, "default DBData", new DBData(), nullptr, "blob:0", nullptr},
      {4, "DBData(nullptr, 0)", new DBData(nullptr, 0), nullptr, "blob:0", nullptr},
      {5, "DBNull", new DBNull("Data"), nullptr, "null:-", nullptr},
      {6, "a nullptr argument", nullptr, nullptr, "null:-", nullptr},
      {7, "SQL X''", nullptr, "X''", "blob:0", nullptr},
      {8, "SQL zeroblob(0)", nullptr, "zeroblob(0)", "blob:0", nullptr},
      {9, "SQL NULL", nullptr, "NULL", "null:-", nullptr},
      {10, "70000 bytes", new DBData(big.data(), big.size()), nullptr, "blob:70000", &big},
   };

   for (const Case& c : cases) {
      if (c.sqlValue != nullptr) {
         require(exec(db, "insert into blobs(id,b) values (" + to_string(c.id) + "," + c.sqlValue + ")"),
                 string("insert ") + c.label);
      } else {
         DBStatementArgs args;
         args.add(new DBLong(c.id));
         args.add(c.value);
         unsigned long rowsAffected = 0;
         require(db.executeUpdate("insert into blobs(id,b) values (?,?)", args, rowsAffected),
                 string("insert ") + c.label);
      }
      requireStringEquals(c.expectedStored,
         rawFirstRow(path, "select typeof(b)||':'||ifnull(length(b),'-') from blobs where id = " + to_string(c.id)),
         string(c.label) + ": what SQLite stored");
   }

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      for (const Case& c : cases) {
         const string label = when + ", " + c.label;
         const bool storedNull = (c.expectedStored.compare(0, 4, "null") == 0);
         DBStatementArgs args;
         args.add(new DBLong(c.id));
         unique_ptr<SQLiteResultSet> rs(
            openCursor(db, "select b as payload from blobs where id = ?", args));
         require(rs && rs->next(), label + ": a row");
         if (!rs) {
            continue;
         }

         require(rs->getTypeForColumnIndex(0) == (storedNull ? ValueTypeNull : ValueTypeBlob),
                 label + ": column type");
         unique_ptr<DBData> byIndex(rs->dataForColumnIndex(0));
         unique_ptr<DBData> byName(rs->dataForColumn("payload"));
         if (storedNull) {
            require(byIndex == nullptr, label + ": NULL reads as nullptr (index)");
            require(byName == nullptr, label + ": NULL reads as nullptr (name)");
         } else {
            require(byIndex != nullptr, label + ": a blob is never nullptr (index)");
            require(byName != nullptr, label + ": a blob is never nullptr (name)");
            const size_t expectedSize = (c.expectedBytes != nullptr) ? c.expectedBytes->size() : 0;
            if (byIndex) {
               requireLongEquals(static_cast<long long>(expectedSize), byIndex->length(),
                                 label + ": length (index)");
               if (c.expectedBytes != nullptr) {
                  require(::memcmp(byIndex->value(), c.expectedBytes->data(), expectedSize) == 0,
                          label + ": all the bytes (index)");
               }
            }
            if (byName) {
               requireLongEquals(static_cast<long long>(expectedSize), byName->length(),
                                 label + ": length (name)");
               if (c.expectedBytes != nullptr) {
                  require(::memcmp(byName->value(), c.expectedBytes->data(), expectedSize) == 0,
                          label + ": all the bytes (name)");
               }
            }
         }
      }
   }
   db.setShouldCacheStatements(true);

   // reading did not change what is stored: the NULLs are still NULL, the empty blobs still empty blobs
   requireStringEquals("5,6,9",
      rawFirstRow(path, "select group_concat(id) from blobs where b is null"), "the NULL rows are unchanged");
   requireStringEquals("2,3,4,7,8",
      rawFirstRow(path, "select group_concat(id) from blobs where typeof(b) = 'blob' and length(b) = 0"),
      "the empty blobs are unchanged");

   // through a parameter, with nothing stored at all
   for (int variant = 0; variant < 3; ++variant) {
      DBStatementArgs args;
      if (variant == 0) args.add(new DBData(nullptr, 0));
      if (variant == 1) args.add(new DBNull("Data"));
      if (variant == 2) args.add(nullptr);
      unique_ptr<SQLiteResultSet> rs(openCursor(db, "select ?", args));
      require(rs && rs->next(), "select ? " + to_string(variant));
      if (rs) {
         require(rs->getTypeForColumnIndex(0) == (variant == 0 ? ValueTypeBlob : ValueTypeNull),
                 "select ? " + to_string(variant) + ": an empty DBData is a blob, DBNull and nullptr are NULL");
         unique_ptr<DBData> data(rs->dataForColumnIndex(0));
         require((data == nullptr) == (variant != 0), "select ? " + to_string(variant) + ": nullptr only for NULL");
      }
   }
   db.close();
}

void TestSQLiteFoundation::test_null_dates() {
   TEST_CASE("test_null_dates");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("dates.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "create table dates(id integer primary key, d real)"), "create table");

   struct Case { long id; const char* label; DBAbstractDataType* value; const char* sqlValue; bool isNull; double seconds; };
   const Case cases[] = {
      {1, "the Unix epoch", new DBDate(0.0), nullptr, false, 0.0},
      {2, "a positive date", new DBDate(1700000000.5), nullptr, false, 1700000000.5},
      {3, "a date before the epoch", new DBDate(-86400.0), nullptr, false, -86400.0},
      {4, "a far date", new DBDate(253402300799.0), nullptr, false, 253402300799.0},
      {5, "DBNull", new DBNull("Date"), nullptr, true, 0.0},
      {6, "a nullptr argument", nullptr, nullptr, true, 0.0},
      {7, "SQL NULL", nullptr, "NULL", true, 0.0},
      {8, "SQL integer seconds", nullptr, "1700000000", false, 1700000000.0},
   };
   for (const Case& c : cases) {
      if (c.sqlValue != nullptr) {
         require(exec(db, "insert into dates(id,d) values (" + to_string(c.id) + "," + c.sqlValue + ")"),
                 string("insert ") + c.label);
      } else {
         DBStatementArgs args;
         args.add(new DBLong(c.id));
         args.add(c.value);
         unsigned long rowsAffected = 0;
         require(db.executeUpdate("insert into dates(id,d) values (?,?)", args, rowsAffected),
                 string("insert ") + c.label);
      }
      requireStringEquals(c.isNull ? "null" : "real",
         rawFirstRow(path, "select typeof(d) from dates where id = " + to_string(c.id)),
         string(c.label) + ": stored type");
   }

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);
      for (const Case& c : cases) {
         const string label = when + ", " + c.label;
         DBStatementArgs args;
         args.add(new DBLong(c.id));
         unique_ptr<SQLiteResultSet> rs(
            openCursor(db, "select d as moment from dates where id = ?", args));
         require(rs && rs->next(), label + ": a row");
         if (!rs) {
            continue;
         }
         unique_ptr<DBDate> byIndex(rs->dateForColumnIndex(0));
         unique_ptr<DBDate> byName(rs->dateForColumn("moment"));
         if (c.isNull) {
            require(byIndex == nullptr, label + ": SQL NULL is nullptr (index)");
            require(byName == nullptr, label + ": SQL NULL is nullptr (name)");
         } else {
            require(byIndex != nullptr, label + ": a date is not nullptr (index)");
            require(byName != nullptr, label + ": a date is not nullptr (name)");
            if (byIndex) {
               requireDoubleEquals(c.seconds, byIndex->timeIntervalSince1970(), label + ": seconds (index)");
            }
            if (byName) {
               requireDoubleEquals(c.seconds, byName->timeIntervalSince1970(), label + ": seconds (name)");
            }
         }
         unique_ptr<DBDate> unknown(rs->dateForColumn("no_such_column"));
         require(unknown == nullptr, label + ": an unknown column is nullptr as before");
      }
   }
   db.setShouldCacheStatements(true);
   db.close();
}

void TestSQLiteFoundation::test_integer_accessors() {
   TEST_CASE("test_integer_accessors");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("ints.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "create table ints(id integer primary key, i integer)"), "create table");

   struct Case { long id; const char* label; const char* sql; long long value; };
   const Case cases[] = {
      {1, "0", "0", 0},
      {2, "1", "1", 1},
      {3, "-1", "-1", -1},
      {4, "INT_MAX", "2147483647", INT_MAX},
      {5, "INT_MIN", "-2147483648", INT_MIN},
      {6, "INT_MAX + 1", "2147483648", 2147483648LL},
      {7, "INT_MIN - 1", "-2147483649", -2147483649LL},
      {8, "2^32", "4294967296", 4294967296LL},
      {9, "2^32 + 5", "4294967301", 4294967301LL},
      {10, "2^40", "1099511627776", 1099511627776LL},
      {11, "INT64_MAX", "9223372036854775807", INT64_MAX},
      {12, "INT64_MIN", "(-9223372036854775807 - 1)", INT64_MIN},
   };
   for (const Case& c : cases) {
      require(exec(db, "insert into ints(id,i) values (" + to_string(c.id) + "," + c.sql + ")"),
              string("insert ") + c.label);
      requireStringEquals("integer:" + to_string(c.value),
         rawFirstRow(path, "select typeof(i)||':'||i from ints where id = " + to_string(c.id)),
         string(c.label) + ": stored as an INTEGER");
   }

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);
      for (const Case& c : cases) {
         const string label = when + ", " + c.label;
         DBStatementArgs args;
         args.add(new DBLong(c.id));
         unique_ptr<SQLiteResultSet> rs(
            openCursor(db, "select i as num from ints where id = ?", args));
         require(rs && rs->next(), label + ": a row");
         if (!rs) {
            continue;
         }
         const int saturated = expectedSaturated(c.value);
         const bool fits = (saturated == c.value);

         requireLongEquals(saturated, rs->intForColumnIndex(0), label + ": int (index) saturates, never wraps");
         requireLongEquals(saturated, rs->intForColumn("num"), label + ": int (name) saturates, never wraps");
         requireLongEquals(c.value, rs->int64ForColumnIndex(0), label + ": int64 (index) is exact");
         requireLongEquals(c.value, rs->int64ForColumn("num"), label + ": int64 (name) is exact");
         require(rs->boolForColumnIndex(0) == (c.value != 0), label + ": bool (index)");
         require(rs->boolForColumn("num") == (c.value != 0), label + ": bool (name)");

         int checked = 99;
         require(rs->intForColumnIndex(0, checked) == fits, label + ": checked (index) succeeds only if it fits");
         requireLongEquals(fits ? c.value : 0, checked, label + ": checked (index) value, 0 on failure");
         checked = 99;
         require(rs->intForColumn("num", checked) == fits, label + ": checked (name) succeeds only if it fits");
         requireLongEquals(fits ? c.value : 0, checked, label + ": checked (name) value, 0 on failure");

         // a failed conversion is not a failure of the result set
         require(rs->status() == DBResultSetStatus::Row, label + ": the status is untouched");
      }
   }
   db.setShouldCacheStatements(true);

   // the other SQLite types: converted as before (saturating), refused by the checked accessor
   struct Other { const char* sql; const char* label; int expectedInt; long long expectedInt64; };
   const Other others[] = {
      {"select 42.0 as v", "REAL 42.0", 42, 42},
      {"select 3.5 as v", "REAL 3.5", 3, 3},
      {"select -3.5 as v", "REAL -3.5", -3, -3},
      {"select 3.0e10 as v", "REAL 3.0e10", INT_MAX, 30000000000LL},
      {"select -3.0e10 as v", "REAL -3.0e10", INT_MIN, -30000000000LL},
      {"select 1e300 as v", "REAL 1e300", INT_MAX, INT64_MAX},
      {"select '42' as v", "TEXT '42'", 42, 42},
      {"select '4294967297' as v", "TEXT '4294967297'", INT_MAX, 4294967297LL},
      {"select 'abc' as v", "TEXT 'abc'", 0, 0},
      {"select NULL as v", "NULL", 0, 0},
      {"select X'2A' as v", "BLOB X'2A'", 0, 0},
   };
   for (const Other& o : others) {
      unique_ptr<SQLiteResultSet> rs(openCursor(db, o.sql));
      require(rs && rs->next(), string(o.label) + ": a row");
      if (!rs) {
         continue;
      }
      requireLongEquals(o.expectedInt, rs->intForColumnIndex(0), string(o.label) + ": int");
      requireLongEquals(o.expectedInt64, rs->int64ForColumnIndex(0), string(o.label) + ": int64");
      int checked = 99;
      require(!rs->intForColumnIndex(0, checked), string(o.label) + ": the checked accessor refuses it");
      requireLongEquals(0, checked, string(o.label) + ": and sets the value to 0");
      checked = 99;
      require(!rs->intForColumn("v", checked), string(o.label) + ": by name too");
      requireLongEquals(0, checked, string(o.label) + ": with 0 as the value");
      require(rs->status() == DBResultSetStatus::Row, string(o.label) + ": the status is untouched");
   }

   {
      // unknown column or index
      unique_ptr<SQLiteResultSet> rs(openCursor(db, "select 7 as v"));
      require(rs && rs->next(), "a row");
      if (rs) {
         int checked = 99;
         require(!rs->intForColumn("nope", checked), "an unknown column is refused");
         requireLongEquals(0, checked, "with 0 as the value");
         checked = 99;
         require(!rs->intForColumnIndex(5, checked), "a column index that does not exist is refused");
         requireLongEquals(0, checked, "with 0 as the value");
         checked = 99;
         require(!rs->intForColumnIndex(-1, checked), "a negative column index is refused");
         requireLongEquals(0, checked, "with 0 as the value");
         requireLongEquals(0, rs->int64ForColumn("nope"), "int64 of an unknown column is 0");
         require(rs->status() == DBResultSetStatus::Row, "and none of it touched the status");
         int ok = 99;
         require(rs->intForColumn("v", ok) && ok == 7, "while a real INTEGER passes");
      }
   }
   db.close();
}

void TestSQLiteFoundation::test_boolean_conversion_of_wide_integers() {
   TEST_CASE("test_boolean_conversion_of_wide_integers");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("bools.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");

   struct Case { const char* sql; const char* label; bool expected; };
   const Case cases[] = {
      {"select 0 as v", "0", false},
      {"select 1 as v", "1", true},
      {"select -1 as v", "-1", true},
      {"select 4294967296 as v", "2^32 (low 32 bits are zero)", true},
      {"select 1099511627776 as v", "2^40 (low 32 bits are zero)", true},
      {"select (-9223372036854775807 - 1) as v", "INT64_MIN (low 32 bits are zero)", true},
      {"select 9223372036854775807 as v", "INT64_MAX", true},
      {"select 0.0 as v", "REAL 0.0", false},
      {"select 0.5 as v", "REAL 0.5 (truncates to 0, as before)", false},
      {"select 2.0 as v", "REAL 2.0", true},
      {"select '0' as v", "TEXT '0'", false},
      {"select '7' as v", "TEXT '7'", true},
      {"select 'abc' as v", "TEXT 'abc'", false},
      {"select NULL as v", "NULL", false},
   };
   for (const Case& c : cases) {
      unique_ptr<SQLiteResultSet> rs(openCursor(db, c.sql));
      require(rs && rs->next(), string(c.label) + ": a row");
      if (rs) {
         require(rs->boolForColumnIndex(0) == c.expected, string(c.label) + ": bool (index)");
         require(rs->boolForColumn("v") == c.expected, string(c.label) + ": bool (name)");
      }
   }
   db.close();
}

void TestSQLiteFoundation::test_float_binding_is_exact_promotion() {
   TEST_CASE("test_float_binding_is_exact_promotion");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("floats.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "create table f(id integer primary key, x real)"), "create table");

   const float tenth = 0.1f;
   struct Case { long id; const char* label; DBAbstractDataType* value; double expected; };
   const Case cases[] = {
      {1, "DBFloat(0.1f)", new DBFloat(0.1f), static_cast<double>(tenth)},
      {2, "DBDouble(0.1)", new DBDouble(0.1), 0.1},
      {3, "DBFloat(0.5f), exactly representable", new DBFloat(0.5f), 0.5},
      {4, "DBDouble((double) 0.1f)", new DBDouble(static_cast<double>(tenth)), static_cast<double>(tenth)},
      {5, "DBFloat(16777217.0f), which is 16777216 as a float", new DBFloat(16777217.0f), 16777216.0},
   };
   for (const Case& c : cases) {
      DBStatementArgs args;
      args.add(new DBLong(c.id));
      args.add(c.value);
      unsigned long rowsAffected = 0;
      require(db.executeUpdate("insert into f(id,x) values (?,?)", args, rowsAffected), string("insert ") + c.label);

      unique_ptr<SQLiteResultSet> rs(openCursor(db, "select x from f where id = " + to_string(c.id)));
      require(rs && rs->next(), string(c.label) + ": a row");
      if (rs) {
         requireDoubleEquals(c.expected, rs->doubleForColumnIndex(0), string(c.label) + ": the double that was stored");
         require(rs->getTypeForColumnIndex(0) == ValueTypeFloat, string(c.label) + ": a REAL");
      }
   }

   // a float survives the round trip unchanged
   {
      unique_ptr<SQLiteResultSet> rs(openCursor(db, "select x from f where id = 1"));
      require(rs && rs->next() && static_cast<float>(rs->doubleForColumnIndex(0)) == tenth,
              "the float read back is the float stored");
   }
   // and the documented consequence: it is not the double 0.1
   requireStringEquals("2", rawFirstRow(path, "select group_concat(id) from f where x = 0.1"),
                       "only the DBDouble(0.1) row equals the decimal literal 0.1");
   requireStringEquals("1,4", rawFirstRow(path, "select group_concat(id) from f where x = 0.10000000149011612"),
                       "while DBFloat(0.1f) equals the double it was promoted to");

   // binding the same float without Chapeau stores the same value
   {
      sqlite3* handle = nullptr;
      require(sqlite3_open(path.c_str(), &handle) == SQLITE_OK, "a raw connection");
      sqlite3_stmt* stmt = nullptr;
      sqlite3_prepare_v2(handle, "insert into f(id,x) values (100, ?)", -1, &stmt, nullptr);
      sqlite3_bind_double(stmt, 1, tenth);
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
      sqlite3_close(handle);
      requireStringEquals("1", rawFirstRow(path, "select (select x from f where id = 100) = (select x from f where id = 1)"),
                          "a plain sqlite3_bind_double of 0.1f stores exactly what DBFloat does");
   }
   db.close();
}

void TestSQLiteFoundation::test_legacy_result_set_subclass_still_works() {
   TEST_CASE("test_legacy_result_set_subclass_still_works");
   // LegacyResultSet (above) overrides only the original pure virtuals. That it
   // compiles at all is the source-compatibility check; this is what it does.
   LegacyResultSet legacy;
   DBResultSet& base = legacy;

   legacy.storedLong = 123456;
   legacy.storedType = ValueTypeInteger;
   requireLongEquals(123456, base.int64ForColumnIndex(0), "int64 defaults to the long accessor (index)");
   requireLongEquals(123456, base.int64ForColumn("any"), "int64 defaults to the long accessor (name)");
   int value = 99;
   require(base.intForColumnIndex(0, value), "the checked accessor works on top of getTypeForColumnIndex()");
   requireLongEquals(123456, value, "with the value");

   legacy.storedType = ValueTypeNull;
   value = 99;
   require(!base.intForColumnIndex(0, value), "NULL is refused");
   requireLongEquals(0, value, "with 0");

   legacy.storedType = ValueTypeFloat;
   value = 99;
   require(!base.intForColumnIndex(0, value), "a REAL is refused");

   legacy.storedType = ValueTypeInteger;
   legacy.storedLong = 0;
   value = 99;
   require(base.intForColumnIndex(0, value) && value == 0, "an INTEGER 0 is accepted");

   value = 99;
   require(!base.intForColumn("by name", value), "by name: a class that cannot look names up refuses");
   requireLongEquals(0, value, "with 0");

   require(base.status() == DBResultSetStatus::Ready, "status() defaults to Ready (H1a-4)");
   requireLongEquals(0, base.lastErrorCode(), "lastErrorCode() defaults to 0 (H1a-4)");
}

// ---- executeUpdate() and statements that return rows (CH-21) ----------------

namespace {

int g_sideEffectCalls = 0;

// a SQL function with a visible side effect: the proof that a SELECT is not
// harmless, and that a refused one was never run
void sideEffectFunction(sqlite3_context* context, int, sqlite3_value**) {
   ++g_sideEffectCalls;
   sqlite3_result_int(context, g_sideEffectCalls);
}

}

void TestSQLiteFoundation::test_execute_update_refuses_row_returning_statements() {
   TEST_CASE("test_execute_update_refuses_row_returning_statements");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("update_select.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(createItemsWithoutCachedStatements(db), "create and fill the items table");
   require(sqlite3_create_function(db.sqliteHandle(), "bump", 0, SQLITE_UTF8, nullptr,
                                   sideEffectFunction, nullptr, nullptr) == SQLITE_OK,
           "register a function with a side effect");

   struct Refused {
      const char* label;
      const char* sql;
      bool withArgument;   // run through the overload with arguments (one INTEGER 2)
   };
   const Refused refused[] = {
      {"SELECT", "select id from items", false},
      {"SELECT returning no rows", "select id from items where id > 1000", false},
      {"SELECT with a parameter", "select id from items where id = ?", true},
      {"SELECT through the argument overload without parameters", "select id from items", true},
      {"INSERT ... RETURNING", "insert into items(id,name) values (900,'x') returning id", false},
      {"INSERT ... RETURNING with a parameter", "insert into items(id,name) values (?,'x') returning id", true},
      {"PRAGMA that reads a value", "pragma user_version", false},
      {"PRAGMA that sets a value and returns it", "pragma journal_mode = memory", false},
      {"VALUES", "values (1)", false},
      {"EXPLAIN", "explain select 1", false},
      {"SELECT calling a function with a side effect", "select bump()", false},
   };

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);
      db.clearCachedStatements();
      g_sideEffectCalls = 0;

      for (const Refused& r : refused) {
         const string label = when + ", " + r.label;
         const vector<string> statementsBefore = openStatementSql(db);
         unsigned long rowsAffected = 99;

         bool result = true;
         if (r.withArgument) {
            DBStatementArgs args;
            args.add(new DBLong(2));
            result = db.executeUpdate(r.sql, args, rowsAffected);
         } else {
            result = db.executeUpdate(r.sql, rowsAffected);
         }

         require(!result, label + ": refused (returns false)");
         requireLongEquals(0, rowsAffected, label + ": no rows affected");
         requireLastError(db, DBErrorKind::RefusedStatement, 0, "returns rows", label + ": the refusal is recorded, as its own kind");
         require(!db.inTransaction(), label + ": no transaction was started");
         require(openStatementSql(db) == statementsBefore,
                 label + ": no statement was left behind or taken away: " + describe(openStatementSql(db)));
      }

      // refused means not run: none of the effects a row-returning statement can have happened
      requireLongEquals(3, scalar(db, "select count(*) from items"), when + ": nothing was inserted");
      requireLongEquals(0, g_sideEffectCalls, when + ": the function in a refused SELECT was never called");
      {
         unique_ptr<SQLiteResultSet> rs(openCursor(db, "pragma journal_mode"));
         require(rs && rs->next(), when + ": read the journal mode");
         if (rs) {
            requireStringEquals("delete", firstText(*rs), when + ": the refused PRAGMA did not change it");
         }
      }

      // the connection and its statements are fine afterwards
      unsigned long rowsAffected = 0;
      require(db.executeUpdate("update items set qty = qty + 1 where id = 1", rowsAffected),
              when + ": an ordinary update still works");
      requireLongEquals(1, rowsAffected, when + ": and reports its row");
      DBStatementArgs args;
      args.add(new DBLong(4));
      args.add(new DBString("added"));
      require(db.executeUpdate("insert into items(id,name) values (?,?)", args, rowsAffected),
              when + ": so does one with arguments");
      require(db.executeUpdate("delete from items where id = 4", rowsAffected),
              when + ": and the cleanup");
      requireLongEquals(3, scalar(db, "select count(*) from items"), when + ": rows as before");
      require(exec(db, "update items set qty = qty - 1 where id = 1"), when + ": undo the update");
   }
   db.setShouldCacheStatements(true);
   db.clearCachedStatements();

   {
      // a statement the cache owns, and then the same SQL given to executeUpdate: the cache is not hurt
      const char* const sql = "select id from items order by id";
      for (int round = 0; round < 3; ++round) {
         const string label = "round " + to_string(round);
         {
            unique_ptr<SQLiteResultSet> rs(openCursor(db, sql));
            int rows = 0;
            while (rs && rows < 3 && rs->next()) {
               ++rows;
            }
            requireLongEquals(3, rows, label + ": the cached query returns its rows");
         }
         unsigned long rowsAffected = 99;
         require(!db.executeUpdate(sql, rowsAffected), label + ": the same SQL is refused by executeUpdate");
         requireLongEquals(0, rowsAffected, label + ": with no rows affected");
         require(openStatementSql(db) == vector<string>{sql},
                 label + ": the cached statement is still the one open statement: " + describe(openStatementSql(db)));
      }
   }

   {
      // an active result set owns the cached statement; the refusal must not touch it
      const char* const sql = "select id from items order by id";
      unique_ptr<SQLiteResultSet> rs(openCursor(db, sql));
      require(rs && rs->next(), "an active result set");
      requireLongEquals(1, rs ? rs->intForColumnIndex(0) : -1, "on its first row");
      unsigned long rowsAffected = 99;
      require(!db.executeUpdate(sql, rowsAffected), "executeUpdate of the same SQL is refused");
      require(openStatementSql(db) == vector<string>{sql}, "no extra statement is left open");
      if (rs) {
         require(rs->next(), "the result set carries on");
         requireLongEquals(2, rs->intForColumnIndex(0), "with its second row");
         require(rs->next() && !rs->next(), "to its end");
         require(rs->status() == DBResultSetStatus::Done, "which is a normal end");
      }
   }

   {
      // inside a transaction
      require(db.beginTransaction(), "begin");
      unsigned long rowsAffected = 99;
      require(!db.executeUpdate("select 1", rowsAffected), "a SELECT inside a transaction is refused");
      require(db.inTransaction(), "the transaction is still open");
      require(exec(db, "insert into items(id,name) values (5,'five')"), "work inside it");
      require(db.commit(), "it commits");
      requireLongEquals(1, countFromNewConnection(dir.file("update_select.sqlite3"), "items where id = 5"),
                        "and the committed row is there");
   }
   db.close();
}

// ---- reliable error reporting (CH-20) ---------------------------------------

namespace {

// While this is alive the connection refuses to bind or return a text or blob
// longer than 10 bytes (SQLITE_TOOBIG): a bind that fails for the caller's data
// although the statement and the number of arguments are right.
class ShortValueLimit {
public:
   explicit ShortValueLimit(SQLiteDatabase& db) :
      m_handle(db.sqliteHandle()),
      m_previous(sqlite3_limit(db.sqliteHandle(), SQLITE_LIMIT_LENGTH, 10)) {
   }
   ~ShortValueLimit() {
      sqlite3_limit(m_handle, SQLITE_LIMIT_LENGTH, m_previous);
   }
private:
   sqlite3* m_handle;
   int m_previous;
};

// the SQL of the open statement whose text is 'sql', with its bound values
// filled in (SQLite's own view of what it holds), or "" if there is none
string expandedSqlOfStatement(SQLiteDatabase& db, const string& sql) {
   for (sqlite3_stmt* s = sqlite3_next_stmt(db.sqliteHandle(), nullptr);
        s != nullptr;
        s = sqlite3_next_stmt(db.sqliteHandle(), s)) {
      const char* text = sqlite3_sql(s);
      if (text != nullptr && sql == text) {
         char* expanded = sqlite3_expanded_sql(s);
         const string result = (expanded != nullptr) ? expanded : "";
         sqlite3_free(expanded);
         return result;
      }
   }
   return string();
}

const char* const TOO_LONG_FOR_LIMIT = "xxxxxxxxxxxxxxxxxxxx";   // 20 bytes

}

void TestSQLiteFoundation::requireLastError(Database& db,
                                            DBErrorKind kind,
                                            int code,
                                            const string& messagePart,
                                            const string& label) {
   require(db.lastErrorKind() == kind,
           label + ": kind is " + dbErrorKindName(kind) + " (got " +
           dbErrorKindName(db.lastErrorKind()) + ", message '" + db.lastErrorMessage() + "')");
   requireLongEquals(code, db.lastErrorCode(), label + ": code");

   const string message = db.lastErrorMessage();
   if (kind == DBErrorKind::None) {
      require(message.empty(), label + ": no message (got '" + message + "')");
   } else {
      require(!message.empty(), label + ": a message");
      require(message.find(messagePart) != string::npos,
              label + ": the message mentions '" + messagePart + "' (got '" + message + "')");
   }
}

void TestSQLiteFoundation::test_bind_failure_fails_the_call() {
   TEST_CASE("test_bind_failure_fails_the_call");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("bind_failure.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "create table n(id integer primary key, v text, w text)"), "create a table with nullable columns");
   require(exec(db, "create table nn(id integer primary key, v text not null)"), "create a table with a NOT NULL column");
   db.clearCachedStatements();

   const string tooLong = TOO_LONG_FOR_LIMIT;
   const string insertN = "insert into n(id,v,w) values (?,?,?)";
   const string insertNN = "insert into nn(id,v) values (?,?)";
   const string selectParam = "select ? is null, ?";

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);
      db.clearCachedStatements();
      require(exec(db, "delete from n"), when + ": start empty");
      require(exec(db, "delete from nn"), when + ": start empty (NOT NULL table)");
      db.clearCachedStatements();

      // the first run of a statement fails to bind: nothing is left behind, the
      // statement was only prepared for this call
      {
         unsigned long rows = 99;
         DBStatementArgs args;
         args.add(new DBLong(1));
         args.add(new DBString(tooLong));
         args.add(new DBString("fine"));
         bool ok = true;
         {
            ShortValueLimit limit(db);
            ok = db.executeUpdate(insertN, args, rows);
         }
         require(!ok, when + ": a bind failure fails executeUpdate (it used to store NULL and succeed)");
         requireLongEquals(0, rows, when + ": no rows affected");
         requireLastError(db, DBErrorKind::BindFailed, SQLITE_TOOBIG, "parameter 2", when + ": executeUpdate");
         requireLongEquals(0, countFromNewConnection(path, "n"),
                           when + ": nothing was stored -- not even a row with a NULL in the nullable column");
         require(openStatementSql(db).empty(),
                 when + ": no statement was left behind: " + describe(openStatementSql(db)));
      }

      // a NOT NULL column: the real reason is reported, not the constraint a
      // NULL in its place would have hit
      {
         unsigned long rows = 99;
         DBStatementArgs args;
         args.add(new DBLong(1));
         args.add(new DBString(tooLong));
         bool ok = true;
         {
            ShortValueLimit limit(db);
            ok = db.executeUpdate(insertNN, args, rows);
         }
         require(!ok, when + ": NOT NULL column: fails");
         requireLastError(db, DBErrorKind::BindFailed, SQLITE_TOOBIG, "too big", when + ": NOT NULL column");
         require(db.lastErrorCode() != SQLITE_CONSTRAINT,
                 when + ": and it is the bind error, not a constraint error");
         requireLongEquals(0, countFromNewConnection(path, "nn"), when + ": nothing was stored");
      }

      // a failing first parameter names parameter 1
      {
         unsigned long rows = 99;
         DBStatementArgs args;
         args.add(new DBString(tooLong));
         args.add(new DBString("a"));
         args.add(new DBString("b"));
         bool ok = true;
         {
            ShortValueLimit limit(db);
            ok = db.executeUpdate(insertN, args, rows);
         }
         require(!ok, when + ": the first argument cannot be bound");
         requireLastError(db, DBErrorKind::BindFailed, SQLITE_TOOBIG, "parameter 1", when + ": first argument");
      }

      // a blob that cannot be bound
      {
         unsigned long rows = 99;
         DBStatementArgs args;
         args.add(new DBLong(1));
         const string bytes = tooLong;
         args.add(new DBData(bytes.data(), bytes.size()));
         args.add(new DBNull("string"));
         bool ok = true;
         {
            ShortValueLimit limit(db);
            ok = db.executeUpdate(insertN, args, rows);
         }
         require(!ok, when + ": a blob that cannot be bound fails");
         requireLastError(db, DBErrorKind::BindFailed, SQLITE_TOOBIG, "parameter 2", when + ": blob");
      }

      // a query: no result set, and nothing run
      {
         DBStatementArgs args;
         args.add(new DBString(tooLong));
         args.add(new DBLong(7));
         unique_ptr<DBResultSet> rs;
         {
            ShortValueLimit limit(db);
            rs.reset(db.executeQuery(selectParam, args));
         }
         require(rs == nullptr, when + ": executeQuery returns no result set (it used to run with a NULL)");
         requireLastError(db, DBErrorKind::BindFailed, SQLITE_TOOBIG, "parameter 1", when + ": executeQuery");
         require(openStatementSql(db).empty(),
                 when + ": no statement was left behind by executeQuery: " + describe(openStatementSql(db)));
      }

      // the same statements run fine afterwards, with real values, and the
      // error is gone
      {
         unsigned long rows = 0;
         DBStatementArgs args;
         args.add(new DBLong(1));
         args.add(new DBString("good"));
         args.add(new DBString("also"));
         require(db.executeUpdate(insertN, args, rows), when + ": a valid insert works afterwards");
         requireLongEquals(1, rows, when + ": one row");
         requireLastError(db, DBErrorKind::None, 0, "", when + ": and the error is cleared");
         requireStringEquals("good|also", rawFirstRow(path, "select v, w from n where id = 1"),
                             when + ": the values that were stored");
         const vector<string> expected = (pass == 0) ? vector<string>{insertN} : vector<string>{};
         require(openStatementSql(db) == expected,
                 when + ": the statement is cached exactly when caching is on: " + describe(openStatementSql(db)));
      }

      // a failure on a statement that is reused (cached): the statement stays,
      // is idle, holds none of the values bound before the failure, and works
      {
         unsigned long rows = 99;
         DBStatementArgs args;
         args.add(new DBLong(2));
         args.add(new DBString(tooLong));
         args.add(new DBString("late"));
         bool ok = true;
         {
            ShortValueLimit limit(db);
            ok = db.executeUpdate(insertN, args, rows);
         }
         require(!ok, when + ": the same statement fails to bind");
         requireLastError(db, DBErrorKind::BindFailed, SQLITE_TOOBIG, "parameter 2", when + ": reused statement");
         requireLongEquals(1, countFromNewConnection(path, "n"), when + ": still only the good row");

         if (pass == 0) {
            require(openStatementSql(db) == vector<string>{insertN},
                    when + ": the cached statement is still the one open statement");
            requireStringEquals("insert into n(id,v,w) values (NULL,NULL,NULL)",
                                expandedSqlOfStatement(db, insertN),
                                when + ": and the values bound before the failure were cleared");
         }

         DBStatementArgs again;
         again.add(new DBLong(2));
         again.add(new DBString("second"));
         again.add(new DBString("row"));
         require(db.executeUpdate(insertN, again, rows), when + ": it still executes afterwards");
         requireStringEquals("second|row", rawFirstRow(path, "select v, w from n where id = 2"),
                             when + ": with the new values");
      }

      // the same for a query: a statement that is reused keeps none of the
      // values bound before the failure
      {
         {
            DBStatementArgs real;
            real.add(new DBString("a"));
            real.add(new DBLong(7));
            unique_ptr<DBResultSet> rs(db.executeQuery(selectParam, real));
            require(rs && rs->next(), when + ": the query works with real values");
            requireLongEquals(7, rs ? rs->intForColumnIndex(1) : -1, when + ": and returns them");
         }
         DBStatementArgs bad;
         bad.add(new DBString("b"));
         bad.add(new DBString(tooLong));
         unique_ptr<DBResultSet> none;
         {
            ShortValueLimit limit(db);
            none.reset(db.executeQuery(selectParam, bad));
         }
         require(none == nullptr, when + ": the reused query fails to bind");
         requireLastError(db, DBErrorKind::BindFailed, SQLITE_TOOBIG, "parameter 2", when + ": reused query");
         if (pass == 0) {
            requireStringEquals("select NULL is null, NULL", expandedSqlOfStatement(db, selectParam),
                                when + ": and it holds none of the values bound before the failure");
         } else {
            require(openStatementSql(db).empty(), when + ": (uncached) no statement is left behind");
         }
      }

      // an active result set is not disturbed by a failed bind, including a
      // failed query over the very SQL it is running
      {
         const string listSql = "select id from n order by id";
         unique_ptr<SQLiteResultSet> cursor(openCursor(db, listSql));
         require(cursor && cursor->next(), when + ": an active result set on its first row");
         requireLongEquals(1, cursor ? cursor->intForColumnIndex(0) : -1, when + ": row 1");

         unsigned long rows = 99;
         DBStatementArgs args;
         args.add(new DBLong(3));
         args.add(new DBString(tooLong));
         args.add(new DBString("x"));
         {
            ShortValueLimit limit(db);
            require(!db.executeUpdate(insertN, args, rows), when + ": a failed bind with a cursor open");
            DBStatementArgs q;
            q.add(new DBString(tooLong));
            q.add(new DBLong(0));
            unique_ptr<DBResultSet> none(db.executeQuery(selectParam, q));
            require(none == nullptr, when + ": and a failed query");
         }
         if (cursor) {
            require(cursor->next(), when + ": the cursor carries on");
            requireLongEquals(2, cursor->intForColumnIndex(0), when + ": with its second row");
            require(!cursor->next(), when + ": to its end");
            require(cursor->status() == DBResultSetStatus::Done, when + ": normally");
         }
      }
   }
   db.setShouldCacheStatements(true);
   db.close();
}

void TestSQLiteFoundation::test_argument_count_mismatch_is_reported() {
   TEST_CASE("test_argument_count_mismatch_is_reported");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("arg_counts.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "create table p(id integer primary key, v text)"), "create table");

   const string insertSql = "insert into p(id,v) values (?,?)";
   const string selectSql = "select id, v from p where id = ? and v = ?";

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);
      db.clearCachedStatements();
      require(exec(db, "delete from p"), when + ": start empty");
      db.clearCachedStatements();

      // run once with the right arguments so that, with caching on, the statements are cached
      // for the failing calls below (the first round tests a statement prepared for the call)
      for (int round = 0; round < 2; ++round) {
         const string label = when + ", " + (round == 0 ? "new statement" : "cached statement");
         const vector<string> before = openStatementSql(db);
         unsigned long rows = 99;

         {
            DBStatementArgs few;
            few.add(new DBLong(1));
            require(!db.executeUpdate(insertSql, few, rows), label + ": too few arguments fail");
            requireLongEquals(0, rows, label + ": nothing affected");
            requireLastError(db, DBErrorKind::BindCount, 0, "2 parameter(s) but 1 argument(s)", label + ": too few");
         }
         {
            DBStatementArgs many;
            many.add(new DBLong(1));
            many.add(new DBString("a"));
            many.add(new DBLong(9));
            rows = 99;
            require(!db.executeUpdate(insertSql, many, rows), label + ": too many arguments fail");
            requireLongEquals(0, rows, label + ": nothing affected");
            requireLastError(db, DBErrorKind::BindCount, 0, "2 parameter(s) but 3 argument(s)", label + ": too many");
            require(db.lastErrorCode() != SQLITE_RANGE,
                    label + ": SQLite was never asked to bind the surplus argument (it used to answer RANGE)");
         }
         {
            rows = 99;
            require(!db.executeUpdate(insertSql, rows), label + ": parameters but no arguments: executeUpdate(sql)");
            requireLongEquals(0, rows, label + ": nothing affected");
            requireLastError(db, DBErrorKind::BindCount, 0, "2 parameter(s) but 0 argument(s)", label + ": no arguments");
         }
         {
            unique_ptr<DBResultSet> none(db.executeQuery(selectSql));
            require(none == nullptr, label + ": parameters but no arguments: executeQuery(sql)");
            requireLastError(db, DBErrorKind::BindCount, 0, "2 parameter(s) but 0 argument(s)", label + ": query, no arguments");

            DBStatementArgs one;
            one.add(new DBLong(1));
            unique_ptr<DBResultSet> fewer(db.executeQuery(selectSql, one));
            require(fewer == nullptr, label + ": executeQuery with too few arguments");
            requireLastError(db, DBErrorKind::BindCount, 0, "2 parameter(s) but 1 argument(s)", label + ": query, too few");

            DBStatementArgs three;
            three.add(new DBLong(1));
            three.add(new DBString("a"));
            three.add(new DBLong(2));
            unique_ptr<DBResultSet> more(db.executeQuery(selectSql, three));
            require(more == nullptr, label + ": executeQuery with too many arguments");
            requireLastError(db, DBErrorKind::BindCount, 0, "2 parameter(s) but 3 argument(s)", label + ": query, too many");
         }
         {
            DBStatementArgs none;
            rows = 99;
            require(!db.executeUpdate("select 1", none, rows), label + ": (a refused statement is not a count error)");
            requireLastError(db, DBErrorKind::RefusedStatement, 0, "returns rows", label + ": refused");
            DBStatementArgs three;
            three.add(new DBLong(1));
            rows = 99;
            require(!db.executeUpdate("create table nope(a)", three, rows), label + ": arguments for a statement without parameters");
            requireLastError(db, DBErrorKind::BindCount, 0, "0 parameter(s) but 1 argument(s)", label + ": surplus for none");
         }

         require(openStatementSql(db) == before,
                 label + ": no statement was left behind or taken away: " + describe(openStatementSql(db)));
         requireLongEquals(round, countFromNewConnection(path, "p"),
                           label + ": nothing was stored by the failed calls (only the earlier round's row)");
         requireLongEquals(0, countFromNewConnection(path, "sqlite_master where name = 'nope'"),
                           label + ": and the statement with surplus arguments was not run");

         // the statement is fine afterwards
         DBStatementArgs good;
         good.add(new DBLong(10 + round));
         good.add(new DBString("ok"));
         rows = 0;
         require(db.executeUpdate(insertSql, good, rows), label + ": a correct call works afterwards");
         requireLongEquals(1, rows, label + ": one row");
         requireLastError(db, DBErrorKind::None, 0, "", label + ": and the error is cleared");
         DBStatementArgs q;
         q.add(new DBLong(10 + round));
         q.add(new DBString("ok"));
         unique_ptr<DBResultSet> rs(db.executeQuery(selectSql, q));
         require(rs && rs->next(), label + ": and so does a query");
      }
   }
   db.setShouldCacheStatements(true);
   db.close();
}

namespace {

// an SQL function that uses the database it runs in: while the outer call is
// running, that is a re-entrant use
struct Reentry {
   SQLiteDatabase* db;
   bool calledNested;
   bool nestedResult;
   DBErrorKind nestedKind;
   int nestedCode;
   DBErrorKind kindOnEntry;   // the database's error when the function started
};

Reentry g_reentry;

void reentrantFunction(sqlite3_context* context, int, sqlite3_value**) {
   unsigned long rows = 99;
   if (!g_reentry.calledNested) {
      // the first row only: the later ones see what the first one's nested call recorded
      g_reentry.kindOnEntry = g_reentry.db->lastErrorKind();
   }
   g_reentry.calledNested = true;
   g_reentry.nestedResult = g_reentry.db->executeUpdate("update u set a = a", rows);
   g_reentry.nestedKind = g_reentry.db->lastErrorKind();
   g_reentry.nestedCode = g_reentry.db->lastErrorCode();
   sqlite3_result_int(context, 1);
}


// an argument whose bind fails without SQLite being involved
class UnbindableArgument : public DBAbstractDataType {
public:
   UnbindableArgument() : DBAbstractDataType("unbindable") {}
   string valueAsString() const override { return "unbindable"; }
   bool bind(Database*, int, DBStatement*) const override { return false; }
};

}

void TestSQLiteFoundation::test_errors_that_are_not_sqlite_errors() {
   TEST_CASE("test_errors_that_are_not_sqlite_errors");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("not_sqlite.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);

   // a fresh database has no error
   requireLastError(db, DBErrorKind::None, 0, "", "a new database");
   require(!db.hadError(), "hadError() is false");

   // no connection, never opened: every operation says so, and none crashes
   {
      unsigned long rows = 99;
      DBStatementArgs args;
      args.add(new DBLong(1));
      const char* const noConnection = "no database connection";

      require(!db.executeUpdate("create table z(a)", rows), "executeUpdate(sql) without a connection");
      requireLongEquals(0, rows, "with no rows affected");
      requireLastError(db, DBErrorKind::NoConnection, 0, noConnection, "executeUpdate(sql)");

      rows = 99;
      require(!db.executeUpdate("insert into z values (?)", args, rows), "executeUpdate(sql, args) without a connection");
      requireLongEquals(0, rows, "with no rows affected");
      requireLastError(db, DBErrorKind::NoConnection, 0, noConnection, "executeUpdate(sql, args)");

      require(db.executeQuery("select 1") == nullptr, "executeQuery(sql) without a connection");
      requireLastError(db, DBErrorKind::NoConnection, 0, noConnection, "executeQuery(sql)");

      require(db.executeQuery("select ?", args) == nullptr, "executeQuery(sql, args) without a connection");
      requireLastError(db, DBErrorKind::NoConnection, 0, noConnection, "executeQuery(sql, args)");

      require(!db.goodConnection(), "goodConnection() without a connection");
      requireLastError(db, DBErrorKind::NoConnection, 0, noConnection, "goodConnection()");

      require(!db.beginTransaction(), "beginTransaction() without a connection");
      requireLastError(db, DBErrorKind::NoConnection, 0, noConnection, "beginTransaction()");
      require(!db.commit(), "commit() without a connection");
      requireLastError(db, DBErrorKind::NoConnection, 0, noConnection, "commit()");
      require(!db.rollback(), "rollback() without a connection");
      requireLastError(db, DBErrorKind::NoConnection, 0, noConnection, "rollback()");

      requireLongEquals(-1, db.lastInsertRowId(), "lastInsertRowId() without a connection");
      requireLastError(db, DBErrorKind::NoConnection, 0, noConnection, "lastInsertRowId()");
      require(db.hadError(), "hadError() is true");
   }

   // open() clears the error, and a second open() is its own failure
   require(db.open(), "open");
   requireLastError(db, DBErrorKind::None, 0, "", "after a successful open");
   sqlite3* const handle = db.sqliteHandle();
   require(exec(db, "create table u(a integer)"), "create a table");
   require(exec(db, "insert into u(a) values (1)"), "and a row");
   require(!db.open(), "open() on an open database");
   requireLastError(db, DBErrorKind::AlreadyOpen, 0, "already open", "second open()");
   require(db.sqliteHandle() == handle, "the connection is unchanged");
   require(db.hadError(), "hadError() is true even though no SQLite code is involved");
   require(exec(db, "insert into u(a) values (2)"), "and the connection works");
   requireLastError(db, DBErrorKind::None, 0, "", "a later success clears it");

   // a refused statement
   {
      unsigned long rows = 99;
      require(!db.executeUpdate("select a from u", rows), "a SELECT through executeUpdate");
      requireLastError(db, DBErrorKind::RefusedStatement, 0, "returns rows", "executeUpdate(select)");
      DBStatementArgs args;
      rows = 99;
      require(!db.executeUpdate("insert into u(a) values (50) returning a", args, rows), "INSERT ... RETURNING through executeUpdate");
      requireLastError(db, DBErrorKind::RefusedStatement, 0, "executeQuery", "executeUpdate(returning)");
      requireLongEquals(2, scalar(db, "select count(*) from u"), "and it was not run");
   }

   // re-entrant use from an SQL function
   {
      g_reentry = Reentry{&db, false, true, DBErrorKind::None, -1, DBErrorKind::RefusedStatement};
      require(sqlite3_create_function(db.sqliteHandle(), "reenter", 0, SQLITE_UTF8, nullptr,
                                      reentrantFunction, nullptr, nullptr) == SQLITE_OK,
              "register a function that uses the database");
      unsigned long rows = 0;
      require(db.executeUpdate("update u set a = a + reenter()", rows), "an update that calls it succeeds");
      require(g_reentry.calledNested, "the function ran");
      require(!g_reentry.nestedResult, "its use of the database was refused");
      require(g_reentry.nestedKind == DBErrorKind::InUse, "as InUse");
      requireLongEquals(0, g_reentry.nestedCode, "with no SQLite code");
      requireLastError(db, DBErrorKind::None, 0, "",
                       "the outer call succeeded: what the nested call recorded does not stay");

      // and when the outer call fails, its own error is the one that stays
      g_reentry.calledNested = false;
      require(exec(db, "create table k(id integer primary key)"), "create table k");
      require(exec(db, "insert into k(id) values (1)"), "with a row");
      require(!db.executeUpdate("insert into k(id) values (reenter())", rows), "an insert that calls it, and collides");
      require(g_reentry.calledNested && g_reentry.nestedKind == DBErrorKind::InUse, "the nested call was refused again");
      requireLastError(db, DBErrorKind::Sqlite, SQLITE_CONSTRAINT, "UNIQUE constraint failed", "the outer failure");
   }

   // an operation begins with no error: a function that runs inside it finds none
   {
      unsigned long rows = 0;
      require(!exec(db, "insert intoo u values (1)"), "a failing call leaves an error");
      g_reentry.kindOnEntry = DBErrorKind::RefusedStatement;
      g_reentry.calledNested = false;
      require(db.executeUpdate("update u set a = a + reenter()", rows), "executeUpdate(sql) with the function");
      require(g_reentry.kindOnEntry == DBErrorKind::None, "executeUpdate(sql) starts with no error");

      require(!exec(db, "insert intoo u values (1)"), "a failing call leaves an error again");
      g_reentry.kindOnEntry = DBErrorKind::RefusedStatement;
      g_reentry.calledNested = false;
      DBStatementArgs zero;
      zero.add(new DBLong(0));
      require(db.executeUpdate("update u set a = a + reenter() where a > ?", zero, rows), "executeUpdate(sql, args) with the function");
      require(g_reentry.kindOnEntry == DBErrorKind::None, "executeUpdate(sql, args) starts with no error");
   }

   // an argument that cannot be bound for a reason that is not SQLite's
   {
      unsigned long rows = 99;
      DBStatementArgs args;
      args.add(new UnbindableArgument());
      require(!db.executeUpdate("insert into u(a) values (?)", args, rows), "an argument whose bind fails");
      requireLongEquals(0, rows, "no rows affected");
      requireLastError(db, DBErrorKind::BindFailed, 0, "parameter 1: the argument could not be bound", "executeUpdate");
      unique_ptr<DBResultSet> none(db.executeQuery("select ?", args));
      require(none == nullptr, "and a query over it");
      requireLastError(db, DBErrorKind::BindFailed, 0, "parameter 1", "executeQuery");
   }

   // the bind functions: no statement, a parameter that does not exist, and a success that
   // leaves the error alone
   {
      require(!db.bindInt(1, 1, nullptr), "bindInt() without a statement");
      requireLastError(db, DBErrorKind::BindFailed, 0, "no statement", "bindInt(nullptr)");
      require(!db.bindString("x", 1, nullptr), "bindString() without a statement");
      requireLastError(db, DBErrorKind::BindFailed, 0, "no statement", "bindString(nullptr)");
      require(!db.bindNull(1, nullptr), "bindNull() without a statement");
      require(!db.bindBool(true, 1, nullptr), "bindBool() without a statement");
      require(!db.bindLong(1L, 1, nullptr), "bindLong() without a statement");
      require(!db.bindFloat(1.0f, 1, nullptr), "bindFloat() without a statement");
      require(!db.bindDouble(1.0, 1, nullptr), "bindDouble() without a statement");
      DBData blob("ab", 2);
      require(!db.bindData(&blob, 1, nullptr), "bindData() without a statement");
      requireLastError(db, DBErrorKind::BindFailed, 0, "no statement", "bindData(nullptr)");

      sqlite3_stmt* raw = nullptr;
      require(sqlite3_prepare_v2(db.sqliteHandle(), "select ?", -1, &raw, nullptr) == SQLITE_OK, "a statement of our own");
      SQLiteStatement own(raw, false);
      require(!db.bindInt(1, 5, &own), "bindInt() to a parameter that does not exist");
      requireLastError(db, DBErrorKind::BindFailed, SQLITE_RANGE, "out of range", "bindInt(5)");
      require(db.bindInt(1, 1, &own), "bindInt() to parameter 1");
      requireLastError(db, DBErrorKind::BindFailed, SQLITE_RANGE, "out of range", "a successful bind does not clear the error");
   }

   // close() is an operation: it clears the error, and afterwards there is no connection
   require(!exec(db, "create table u(a integer)"), "a failing call");
   require(db.hadError(), "which leaves an error");
   db.close();
   requireLastError(db, DBErrorKind::None, 0, "", "close() clears the error");
   require(!exec(db, "select 1"), "no connection after close()");
   requireLastError(db, DBErrorKind::NoConnection, 0, "no database connection", "after close()");
}

void TestSQLiteFoundation::test_last_error_lifetime() {
   TEST_CASE("test_last_error_lifetime");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("error_lifetime.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(createItems(db), "create and fill the items table");

   // a constraint failure
   unsigned long rows = 0;
   require(!db.executeUpdate("insert into items(id,name) values (1,'again')", rows), "a duplicate key");
   requireLastError(db, DBErrorKind::Sqlite, SQLITE_CONSTRAINT, "UNIQUE constraint failed: items.id", "constraint error");
   const string message = db.lastErrorMessage();

   // reading it does not clear it, however often, and neither do the other accessors
   for (int i = 0; i < 3; ++i) {
      require(db.lastErrorKind() == DBErrorKind::Sqlite && db.lastErrorCode() == SQLITE_CONSTRAINT &&
              db.lastErrorMessage() == message && db.hadError(), "reading the error leaves it (" + to_string(i) + ")");
   }
   (void) db.inTransaction();
   (void) db.databasePath();
   (void) db.busyRetryTimeout();
   (void) db.shouldCacheStatements();
   require(db.lastErrorKind() == DBErrorKind::Sqlite, "state queries leave it");

   // nothing the library does inside leaves it either: statements released,
   // bindings cleared, the cache emptied and resized
   {
      unique_ptr<SQLiteResultSet> held(openCursor(db, "select id from items"));
      require(held && held->next(), "a cursor");
      requireLastError(db, DBErrorKind::None, 0, "", "opening a cursor is an operation: it cleared the error");
      require(!db.executeUpdate("insert into items(id,name) values (1,'again')", rows), "fail again, with a cursor open");
      requireLastError(db, DBErrorKind::Sqlite, SQLITE_CONSTRAINT, "UNIQUE", "failure with a cursor open");
      held->close();
      requireLastError(db, DBErrorKind::Sqlite, SQLITE_CONSTRAINT, "UNIQUE", "closing the cursor (statement released)");
      held.reset();
      requireLastError(db, DBErrorKind::Sqlite, SQLITE_CONSTRAINT, "UNIQUE", "destroying the cursor");
   }
   db.clearCachedStatements();
   requireLastError(db, DBErrorKind::Sqlite, SQLITE_CONSTRAINT, "UNIQUE", "clearing the statement cache");
   db.setStatementCacheLimit(1);
   db.setStatementCacheLimit(Database::DEFAULT_STATEMENT_CACHE_LIMIT);
   db.setShouldCacheStatements(false);
   db.setShouldCacheStatements(true);
   requireLastError(db, DBErrorKind::Sqlite, SQLITE_CONSTRAINT, "UNIQUE", "resizing and switching the cache off and on");

   // the same through an update with arguments, cached and uncached: the
   // statement is reset and its bindings cleared (or deleted) after the failure
   for (int pass = 0; pass < 2; ++pass) {
      db.setShouldCacheStatements(pass == 0);
      db.clearCachedStatements();
      DBStatementArgs args;
      args.add(new DBLong(2));
      args.add(new DBString("dup"));
      require(!db.executeUpdate("insert into items(id,name) values (?,?)", args, rows), "duplicate key with arguments");
      requireLastError(db, DBErrorKind::Sqlite, SQLITE_CONSTRAINT, "UNIQUE constraint failed: items.id",
                       string(pass == 0 ? "cached" : "uncached") + " statement released after the failure");
   }
   db.setShouldCacheStatements(true);

   // a later success clears it ...
   require(exec(db, "update items set qty = qty + 1 where id = 1"), "a successful update");
   requireLastError(db, DBErrorKind::None, 0, "", "cleared by a successful update");
   require(!db.hadError(), "hadError() is false");
   require(exec(db, "update items set qty = qty - 1 where id = 1"), "undo");

   // ... and a later failure replaces it
   require(!exec(db, "insert intoo items values (1)"), "a syntax error");
   requireLastError(db, DBErrorKind::Sqlite, SQLITE_ERROR, "syntax error", "syntax error");
   DBStatementArgs wrong;
   wrong.add(new DBLong(1));
   require(!db.executeUpdate("insert into items(id,name) values (?,?)", wrong, rows), "wrong argument count");
   requireLastError(db, DBErrorKind::BindCount, 0, "2 parameter(s) but 1 argument(s)", "replaces the syntax error");
   require(!exec(db, "select 1"), "a refused statement");
   requireLastError(db, DBErrorKind::RefusedStatement, 0, "returns rows", "replaces the count error");

   // a result set that fails in next() records the failure on the database too
   {
      unique_ptr<DBResultSet> cursor(db.executeQuery("select abs(-9223372036854775807 - 1)"));
      require(cursor != nullptr, "a query given to executeQuery");
      requireLastError(db, DBErrorKind::None, 0, "", "creating the result set succeeded");
      require(cursor && !cursor->next(), "which fails when stepped");
      requireLastError(db, DBErrorKind::Sqlite, SQLITE_ERROR, "integer overflow", "the failed step");
      cursor.reset();
      requireLastError(db, DBErrorKind::Sqlite, SQLITE_ERROR, "integer overflow", "destroying the failed result set");
   }
   unique_ptr<DBResultSet> fine(db.executeQuery("select 1"));
   requireLastError(db, DBErrorKind::None, 0, "", "cleared by a successful query");
   fine.reset();
   require(!exec(db, "select 1"), "a refused statement again");
   require(db.hadError(), "which leaves an error");
   {
      DBStatementArgs byId;
      byId.add(new DBLong(1));
      unique_ptr<DBResultSet> withArguments(db.executeQuery("select name from items where id = ?", byId));
      require(withArguments != nullptr, "a query with arguments");
      requireLastError(db, DBErrorKind::None, 0, "", "cleared by a successful query with arguments");
   }

   // the transaction calls are built on executeUpdate: the failure of the call they make is theirs
   require(db.beginTransaction(), "begin");
   requireLastError(db, DBErrorKind::None, 0, "", "after begin");
   require(!db.beginTransaction(), "a nested begin");
   requireLastError(db, DBErrorKind::Sqlite, SQLITE_ERROR, "within a transaction", "nested beginTransaction()");
   require(!db.beginDeferredTransaction(), "a nested deferred begin");
   requireLastError(db, DBErrorKind::Sqlite, SQLITE_ERROR, "within a transaction", "nested beginDeferredTransaction()");
   require(db.inTransaction(), "the transaction is still open");
   requireLastError(db, DBErrorKind::Sqlite, SQLITE_ERROR, "within a transaction", "asking inTransaction() does not clear it");
   require(db.rollback(), "rollback");
   requireLastError(db, DBErrorKind::None, 0, "", "after rollback");
   require(!db.commit(), "commit without a transaction");
   requireLastError(db, DBErrorKind::Sqlite, SQLITE_ERROR, "no transaction is active", "commit()");
   require(!db.rollback(), "rollback without a transaction");
   requireLastError(db, DBErrorKind::Sqlite, SQLITE_ERROR, "no transaction is active", "rollback()");

   // close() clears it
   db.close();
   requireLastError(db, DBErrorKind::None, 0, "", "close()");
}

void TestSQLiteFoundation::test_open_failure_is_recorded() {
   TEST_CASE("test_open_failure_is_recorded");
   chapeau_tests::ScopedTempDir dir;
   const fs::path missing = fs::path(dir.file("not_there"));
   SQLiteDatabase db((missing / "x.sqlite3").string());
   db.setLogsErrors(false);

   for (int attempt = 0; attempt < 2; ++attempt) {
      require(!db.open(), "open() fails: the directory does not exist");
      requireLastError(db, DBErrorKind::Sqlite, SQLITE_CANTOPEN, "unable to open database file",
                       "attempt " + to_string(attempt));
   }
   require(fs::create_directory(missing), "create the directory");
   require(db.open(), "now it opens");
   requireLastError(db, DBErrorKind::None, 0, "", "and the error is cleared");
   db.close();
}

void TestSQLiteFoundation::test_cursor_error_message() {
   TEST_CASE("test_cursor_error_message");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("cursor_message.sqlite3");

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      unique_ptr<SQLiteDatabase> db(new SQLiteDatabase(path));
      db->setLogsErrors(false);
      require(db->open(), when + ": open");
      db->setShouldCacheStatements(pass == 0);
      require(exec(*db, "drop table if exists items"), when + ": start clean");
      require(createItems(*db), when + ": create and fill the items table");

      // a result set that works has no message
      {
         unique_ptr<SQLiteResultSet> ok(openCursor(*db, "select id from items"));
         require(ok && ok->lastErrorMessage().empty(), when + ": Ready: no message");
         while (ok && ok->next()) {
            require(ok->lastErrorMessage().empty(), when + ": Row: no message");
         }
         require(ok && ok->status() == DBResultSetStatus::Done && ok->lastErrorMessage().empty(),
                 when + ": Done: no message");
      }

      // a statement without result columns is run by executeQuery(): its failure is
      // already in the result set that comes back
      unique_ptr<SQLiteResultSet> failed(openCursor(*db, "insert into items(id,name) values (1,'again')"));
      require(failed != nullptr, when + ": a result set over a statement that fails");
      require(failed && !failed->next(), when + ": next() is false");
      require(failed && failed->status() == DBResultSetStatus::Error, when + ": status Error");
      requireLongEquals(SQLITE_CONSTRAINT, failed ? (failed->lastErrorCode() & 0xff) : -1, when + ": the code");
      const string expected = "UNIQUE constraint failed: items.id";
      requireStringEquals(expected, failed ? failed->lastErrorMessage() : string("(none)"), when + ": SQLite's message");

      // the statement was released when the step failed; the message is a copy
      require(openStatementSql(*db).empty() || pass == 0,
              when + ": (uncached) the statement is gone: " + describe(openStatementSql(*db)));
      requireStringEquals(expected, failed->lastErrorMessage(), when + ": after the statement was released");

      require(!failed->next(), when + ": next() on the failed result set");
      requireStringEquals(expected, failed->lastErrorMessage(), when + ": after exhaustion following the error");
      require(failed->status() == DBResultSetStatus::Error, when + ": still Error");

      failed->close();
      requireStringEquals(expected, failed->lastErrorMessage(), when + ": after close()");
      require(failed->status() == DBResultSetStatus::Error, when + ": close() does not turn it into Done");

      // a different failure elsewhere does not touch it, nor does success
      unsigned long rows = 0;
      require(!db->executeUpdate("insert into items(id,name) values (2,'again')", rows), "another failure");
      requireStringEquals(expected, failed->lastErrorMessage(), when + ": after an unrelated failure");
      require(exec(*db, "update items set qty = 1 where id = 3"), "a success");
      requireStringEquals(expected, failed->lastErrorMessage(), when + ": after an unrelated success");

      unique_ptr<SQLiteResultSet> overflow(openCursor(*db, "select abs(-9223372036854775807 - 1)"));
      require(overflow && overflow->status() == DBResultSetStatus::Ready && overflow->lastErrorMessage().empty(),
              when + ": a query that fails when stepped has no message before that");
      require(overflow && !overflow->next(), when + ": a second result set fails, when stepped, differently");
      requireLongEquals(SQLITE_ERROR, overflow ? overflow->lastErrorCode() : -1, when + ": its code");
      require(overflow && overflow->lastErrorMessage().find("integer overflow") != string::npos,
              when + ": its message is its own: " + (overflow ? overflow->lastErrorMessage() : string()));
      requireStringEquals(expected, failed->lastErrorMessage(), when + ": and the first keeps its own");

      // an active result set that is invalidated by close() has no error
      unique_ptr<SQLiteResultSet> active(openCursor(*db, "select id from items"));
      require(active && active->next(), when + ": an active result set");

      // the database closes, then is destroyed
      db->close();
      requireStringEquals(expected, failed->lastErrorMessage(), when + ": after the database was closed");
      require(overflow->lastErrorMessage().find("integer overflow") != string::npos, when + ": (second) after close");
      require(active->status() == DBResultSetStatus::Invalidated && active->lastErrorMessage().empty() &&
              active->lastErrorCode() == 0, when + ": an invalidated result set has no error");
      db.reset();
      requireStringEquals(expected, failed->lastErrorMessage(), when + ": after the database was destroyed");
      require(overflow->lastErrorMessage().find("integer overflow") != string::npos, when + ": (second) after destruction");
      require(failed->status() == DBResultSetStatus::Error, when + ": still Error");
   }
}

// ---- executeQuery() and statements without result columns (CH-18) ----------

namespace {

// a statement without result columns given to executeQuery(), what an
// independent connection sees before the call, and as soon as it returns
struct EagerCase {
   string label;
   string sql;
   function<void(DBStatementArgs&)> bindArguments;   // empty: the overload without arguments
   string probe;                                     // run on the independent connection
   string before;
   string after;
};

// the fixture of the cases: t(id, v) with three rows, and an empty table o
bool resetEagerFixture(SQLiteDatabase& db) {
   const bool wasCaching = db.shouldCacheStatements();
   db.setShouldCacheStatements(false);
   bool ok = exec(db, "drop table if exists t") && exec(db, "drop table if exists o") &&
             exec(db, "drop table if exists made") &&
             exec(db, "create table t(id integer primary key, v text not null)") &&
             exec(db, "insert into t(id,v) values (1,'a'),(2,'b'),(3,'c')") &&
             exec(db, "create table o(a integer)") && exec(db, "pragma user_version = 0");
   db.setShouldCacheStatements(wasCaching);
   db.clearCachedStatements();
   return ok;
}

}

void TestSQLiteFoundation::test_query_without_result_columns_runs_before_it_returns() {
   TEST_CASE("test_query_without_result_columns_runs_before_it_returns");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("eager.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");

   const string count = "select count(*) from t";
   const vector<EagerCase> cases = {
      {"INSERT", "insert into t(id,v) values (10,'x')", nullptr, count, "3", "4"},
      {"INSERT with arguments", "insert into t(id,v) values (?,?)",
       [](DBStatementArgs& a) { a.add(new DBLong(10)); a.add(new DBString("x")); }, count, "3", "4"},
      {"UPDATE", "update t set v = 'u' where id = 1", nullptr, "select v from t where id = 1", "a", "u"},
      {"UPDATE with arguments", "update t set v = ? where id = ?",
       [](DBStatementArgs& a) { a.add(new DBString("u")); a.add(new DBLong(1)); },
       "select v from t where id = 1", "a", "u"},
      {"DELETE", "delete from t where id > 1", nullptr, count, "3", "1"},
      {"DELETE with arguments", "delete from t where id > ?",
       [](DBStatementArgs& a) { a.add(new DBLong(1)); }, count, "3", "1"},
      {"CREATE TABLE", "create table made(a integer)", nullptr,
       "select count(*) from sqlite_master where name = 'made'", "0", "1"},
      {"CREATE INDEX", "create index ix_v on t(v)", nullptr,
       "select count(*) from sqlite_master where name = 'ix_v'", "0", "1"},
      {"DROP TABLE", "drop table o", nullptr,
       "select count(*) from sqlite_master where name = 'o'", "1", "0"},
      {"PRAGMA that sets a value and returns nothing", "pragma user_version = 7", nullptr,
       "pragma user_version", "0", "7"},
   };

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      for (const EagerCase& c : cases) {
         const string label = when + ", " + c.label;
         require(resetEagerFixture(db), label + ": fixture");
         requireStringEquals(c.before, rawFirstRow(path, c.probe), label + ": before the call");

         DBStatementArgs args;
         if (c.bindArguments) {
            c.bindArguments(args);
         }
         unique_ptr<DBResultSet> rs(c.bindArguments ? db.executeQuery(c.sql, args) : db.executeQuery(c.sql));

         // run before executeQuery() returned: visible to another connection with the
         // result set still alive, and never stepped
         require(rs != nullptr, label + ": a result set");
         requireStringEquals(c.after, rawFirstRow(path, c.probe), label + ": already done when executeQuery() returns");
         if (rs) {
            require(rs->status() == DBResultSetStatus::Done, label + ": status is Done");
            requireLongEquals(0, rs->lastErrorCode(), label + ": no error code");
            require(rs->lastErrorMessage().empty(), label + ": no error message");
         }
         requireLastError(db, DBErrorKind::None, 0, "", label + ": the database has no error");

         // the statement was let go of when it finished
         const vector<string> expectedStatements = (pass == 0) ? vector<string>{c.sql} : vector<string>{};
         require(openStatementSql(db) == expectedStatements,
                 label + ": the statement is idle in the cache, or finalized: " + describe(openStatementSql(db)));

         // stepping it is not another execution
         for (int i = 0; i < 3 && rs; ++i) {
            require(!rs->next(), label + ": next() is false");
         }
         if (rs) {
            require(rs->status() == DBResultSetStatus::Done, label + ": and the status stays Done");
         }
         requireStringEquals(c.after, rawFirstRow(path, c.probe), label + ": next() did not run it again");
         rs.reset();
         requireStringEquals(c.after, rawFirstRow(path, c.probe), label + ": nor did destroying the result set");
      }

      // the same statement again and again: executed once per call, nothing left over
      {
         require(resetEagerFixture(db), when + ": fixture for reuse");
         // the result sets are kept: a finished one holds nothing, so the next run
         // of the same SQL finds its statement idle and uses it, not a second one
         vector<unique_ptr<DBResultSet>> kept;
         for (int i = 0; i < 4; ++i) {
            DBStatementArgs args;
            args.add(new DBLong(30 + i));
            args.add(new DBString("again"));
            kept.emplace_back(db.executeQuery("insert into t(id,v) values (?,?)", args));
            require(kept.back() && kept.back()->status() == DBResultSetStatus::Done, when + ": reuse " + to_string(i) + ": done");
            requireLongEquals(4 + i, countFromNewConnection(path, "t"),
                              when + ": reuse " + to_string(i) + ": exactly one more row, at once");
         }
         const vector<string> expectedAfterReuse =
            (pass == 0) ? vector<string>{"insert into t(id,v) values (?,?)"} : vector<string>{};
         require(openStatementSql(db) == expectedAfterReuse,
                 when + ": four finished result sets hold no statement: " + describe(openStatementSql(db)));
         requireStringEquals("30,31,32,33", rawFirstRow(path, "select group_concat(id) from t where v = 'again'"),
                             when + ": the rows that were stored");
      }

      // a statement with no text to run
      for (const char* empty : {"", "   ", "-- nothing here"}) {
         unique_ptr<DBResultSet> rs(db.executeQuery(empty));
         require(rs != nullptr && rs->status() == DBResultSetStatus::Done && !rs->next(),
                 when + ": an empty statement (" + empty + ") is a finished result set");
         requireLastError(db, DBErrorKind::None, 0, "", when + ": and no error");
      }

      // alongside an active result set: both are fine, and the active one carries on
      {
         require(resetEagerFixture(db), when + ": fixture for the active cursor");
         unique_ptr<SQLiteResultSet> reading(openCursor(db, "select id from t order by id"));
         require(reading && reading->next(), when + ": an active result set");
         unique_ptr<DBResultSet> inserted(db.executeQuery("insert into o(a) values (1)"));
         require(inserted && inserted->status() == DBResultSetStatus::Done, when + ": an insert runs beside it");
         unique_ptr<DBResultSet> sameSql(db.executeQuery("select id from t order by id"));
         require(sameSql && sameSql->status() == DBResultSetStatus::Ready, when + ": the same SQL is a second, lazy cursor");
         requireLongEquals(1, countFromNewConnection(path, "o"), when + ": the insert is stored");
         if (reading) {
            requireLongEquals(1, reading->intForColumnIndex(0), when + ": the active cursor is on its first row");
            require(reading->next(), when + ": and carries on");
            requireLongEquals(2, reading->intForColumnIndex(0), when + ": to the second");
         }
      }
   }

   // transaction statements are run at once, and inTransaction() follows
   db.setShouldCacheStatements(true);
   require(resetEagerFixture(db), "fixture for transactions");
   {
      unique_ptr<DBResultSet> begin(db.executeQuery("BEGIN"));
      require(begin && begin->status() == DBResultSetStatus::Done, "BEGIN through executeQuery");
      require(db.inTransaction(), "the transaction is open as soon as executeQuery() returns");
      unique_ptr<DBResultSet> insert(db.executeQuery("insert into t(id,v) values (50,'in transaction')"));
      require(insert && insert->status() == DBResultSetStatus::Done, "an insert inside it");
      requireLongEquals(3, countFromNewConnection(path, "t"), "not visible to another connection before the commit");
      unique_ptr<DBResultSet> commit(db.executeQuery("COMMIT"));
      require(commit && commit->status() == DBResultSetStatus::Done, "COMMIT through executeQuery");
      require(!db.inTransaction(), "the transaction is closed as soon as executeQuery() returns");
      requireLongEquals(4, countFromNewConnection(path, "t"), "and the row is visible");
   }
   {
      unique_ptr<DBResultSet> begin(db.executeQuery("BEGIN"));
      unique_ptr<DBResultSet> insert(db.executeQuery("insert into t(id,v) values (51,'rolled back')"));
      require(db.inTransaction(), "a second transaction");
      unique_ptr<DBResultSet> nested(db.executeQuery("BEGIN"));
      require(nested && nested->status() == DBResultSetStatus::Error, "a BEGIN inside it fails, in the result set");
      requireLastError(db, DBErrorKind::Sqlite, SQLITE_ERROR, "within a transaction", "and in the database");
      require(db.inTransaction(), "the transaction is still open");
      unique_ptr<DBResultSet> rollback(db.executeQuery("ROLLBACK"));
      require(rollback && rollback->status() == DBResultSetStatus::Done, "ROLLBACK through executeQuery");
      require(!db.inTransaction(), "closed");
      requireLongEquals(4, countFromNewConnection(path, "t"), "the rolled back row was never stored");
   }
   // and mixed with the other calls
   require(db.beginTransaction(), "beginTransaction()");
   {
      unique_ptr<DBResultSet> insert(db.executeQuery("insert into t(id,v) values (52,'mixed')"));
      require(insert && insert->status() == DBResultSetStatus::Done, "an executeQuery insert inside it");
      unique_ptr<DBResultSet> commit(db.executeQuery("COMMIT"));
      require(!db.inTransaction() && commit && commit->status() == DBResultSetStatus::Done, "committed by executeQuery");
   }
   requireLongEquals(5, countFromNewConnection(path, "t"), "stored");
   db.close();
}

void TestSQLiteFoundation::test_query_with_result_columns_stays_lazy() {
   TEST_CASE("test_query_with_result_columns_stays_lazy");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("lazy.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(sqlite3_create_function(db.sqliteHandle(), "bump", 0, SQLITE_UTF8, nullptr,
                                   sideEffectFunction, nullptr, nullptr) == SQLITE_OK,
           "register a function with a side effect");
   require(resetEagerFixture(db), "fixture");

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);
      db.clearCachedStatements();

      // statements that can return rows are not run by executeQuery(), and not by destroying the result set
      const char* const lazy[] = {
         "select bump()", "select bump() from t", "values (bump())", "explain select bump()",
      };
      for (const char* sql : lazy) {
         g_sideEffectCalls = 0;
         unique_ptr<DBResultSet> rs(db.executeQuery(sql));
         require(rs != nullptr, when + ": a result set for " + sql);
         require(rs && rs->status() == DBResultSetStatus::Ready, when + ": " + sql + ": Ready, not run");
         requireLongEquals(0, g_sideEffectCalls, when + ": " + sql + ": the function was not called by executeQuery()");
         rs.reset();
         requireLongEquals(0, g_sideEffectCalls, when + ": " + sql + ": nor by destroying the result set unstepped");
      }
      {
         g_sideEffectCalls = 0;
         unique_ptr<DBResultSet> rs(db.executeQuery("select bump()"));
         require(rs && rs->next(), when + ": stepped, it runs");
         requireLongEquals(1, g_sideEffectCalls, when + ": once");
         require(!rs->next(), when + ": and ends");
         require(rs->status() == DBResultSetStatus::Done, when + ": Done");
      }
      {
         // a reading PRAGMA and a PRAGMA that sets a value and returns it
         unique_ptr<DBResultSet> reading(db.executeQuery("pragma user_version"));
         require(reading && reading->status() == DBResultSetStatus::Ready && reading->next(), when + ": pragma user_version is lazy");
         unique_ptr<DBResultSet> setting(db.executeQuery("pragma journal_mode = memory"));
         require(setting && setting->status() == DBResultSetStatus::Ready, when + ": pragma journal_mode = ... returns a row, so it is lazy");
         // journal_mode is a property of the connection, so it is asked of this one
         const auto journalMode = [&] {
            unique_ptr<SQLiteResultSet> mode(openCursor(db, "pragma journal_mode"));
            return (mode && mode->next()) ? firstText(*mode) : string("(none)");
         };
         requireStringEquals("delete", journalMode(), when + ": and has not been run");
         setting.reset();
         requireStringEquals("delete", journalMode(), when + ": not by destroying it either");
         unique_ptr<SQLiteResultSet> run(openCursor(db, "pragma journal_mode = memory"));
         require(run && run->next(), when + ": stepped, it runs");
         run.reset();
         requireStringEquals("memory", journalMode(), when + ": and changed the mode");
         unique_ptr<SQLiteResultSet> restore(openCursor(db, "pragma journal_mode = delete"));
         require(restore && restore->next(), when + ": restore the journal mode for the next pass");
         restore.reset();
         requireStringEquals("delete", journalMode(), when + ": restored");
      }

      // RETURNING: lazy, and one step does all of the changes
      {
         require(resetEagerFixture(db), when + ": fixture");
         db.setShouldCacheStatements(pass == 0);
         const string insertReturning = "insert into t(id,v) values (20,'r1'),(21,'r2'),(22,'r3') returning id";
         {
            unique_ptr<DBResultSet> rs(db.executeQuery(insertReturning));
            require(rs && rs->status() == DBResultSetStatus::Ready, when + ": INSERT ... RETURNING is lazy");
            requireLongEquals(3, countFromNewConnection(path, "t"), when + ": nothing inserted by executeQuery()");
         }
         requireLongEquals(3, countFromNewConnection(path, "t"), when + ": nor by destroying it unstepped");

         unique_ptr<SQLiteResultSet> rs(openCursor(db, insertReturning));
         require(rs && rs->next(), when + ": one step");
         requireLongEquals(20, rs ? rs->intForColumnIndex(0) : -1, when + ": returns the first id");
         requireLongEquals(6, scalar(db, "select count(*) from t"),
                           when + ": the one step inserted all three rows (seen on this connection)");
         requireLongEquals(3, countFromNewConnection(path, "t"),
                           when + ": but another connection sees none while the statement is unfinished");
         rs.reset();
         requireLongEquals(6, countFromNewConnection(path, "t"),
                           when + ": they are committed when the result set is destroyed");

         // consumed to the end: the same
         require(exec(db, "delete from t where id >= 20"), when + ": cleanup");
         rs.reset(openCursor(db, insertReturning));
         int rows = 0;
         while (rs && rs->next()) {
            ++rows;
         }
         requireLongEquals(3, rows, when + ": three rows returned");
         require(rs && rs->status() == DBResultSetStatus::Done, when + ": Done");
         requireLongEquals(6, countFromNewConnection(path, "t"), when + ": committed");

         // UPDATE and DELETE ... RETURNING are lazy too
         rs.reset(openCursor(db, "update t set v = 'changed' where id = 1 returning v"));
         require(rs && rs->status() == DBResultSetStatus::Ready, when + ": UPDATE ... RETURNING is lazy");
         requireStringEquals("a", rawFirstRow(path, "select v from t where id = 1"), when + ": not run");
         rs.reset();
         requireStringEquals("a", rawFirstRow(path, "select v from t where id = 1"), when + ": not run by destroying it");
         rs.reset(openCursor(db, "delete from t where id >= 20 returning id"));
         require(rs && rs->status() == DBResultSetStatus::Ready, when + ": DELETE ... RETURNING is lazy");
         requireLongEquals(6, countFromNewConnection(path, "t"), when + ": not run");
         rs.reset();
         requireLongEquals(6, countFromNewConnection(path, "t"), when + ": not run by destroying it");
      }
   }
   db.close();
}

void TestSQLiteFoundation::test_query_without_result_columns_that_fails() {
   TEST_CASE("test_query_without_result_columns_that_fails");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("eager_failures.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);
      require(resetEagerFixture(db), when + ": fixture");

      struct Failure {
         string label;
         string sql;
         function<void(DBStatementArgs&)> bindArguments;
         int code;
         string message;
      };
      const vector<Failure> failures = {
         {"duplicate key", "insert into t(id,v) values (1,'dup')", nullptr, SQLITE_CONSTRAINT,
          "UNIQUE constraint failed: t.id"},
         {"duplicate key with arguments", "insert into t(id,v) values (?,?)",
          [](DBStatementArgs& a) { a.add(new DBLong(2)); a.add(new DBString("dup")); }, SQLITE_CONSTRAINT,
          "UNIQUE constraint failed: t.id"},
         {"NOT NULL", "insert into t(id,v) values (9,null)", nullptr, SQLITE_CONSTRAINT,
          "NOT NULL constraint failed: t.v"},
         {"BEGIN inside a transaction", "BEGIN", nullptr, SQLITE_ERROR, "within a transaction"},
      };

      for (const Failure& f : failures) {
         const string label = when + ", " + f.label;
         if (f.label == "BEGIN inside a transaction") {
            // deferred: another connection can still read while it is open
            require(db.beginDeferredTransaction(), label + ": open a transaction first");
         }
         const long long rowsBefore = countFromNewConnection(path, "t");
         const vector<string> statementsBefore = openStatementSql(db);
         DBStatementArgs args;
         if (f.bindArguments) {
            f.bindArguments(args);
         }
         unique_ptr<DBResultSet> rs(f.bindArguments ? db.executeQuery(f.sql, args) : db.executeQuery(f.sql));

         // a result set, not nullptr: the statement was prepared and bound; it failed running
         require(rs != nullptr, label + ": a result set (not nullptr)");
         if (rs) {
            require(rs->status() == DBResultSetStatus::Error, label + ": status is Error at once");
            requireLongEquals(f.code, rs->lastErrorCode() & 0xff, label + ": the cursor's code");
            require(rs->lastErrorMessage().find(f.message) != string::npos,
                    label + ": the cursor's message: '" + rs->lastErrorMessage() + "'");
         }
         requireLastError(db, DBErrorKind::Sqlite, rs ? rs->lastErrorCode() : -1, f.message, label + ": the database records it");
         require(db.lastErrorMessage() == (rs ? rs->lastErrorMessage() : string("(none)")),
                 label + ": the same message in the database and the cursor");

         // it stays that way
         for (int i = 0; i < 3 && rs; ++i) {
            require(!rs->next(), label + ": next() is false");
         }
         if (rs) {
            require(rs->status() == DBResultSetStatus::Error, label + ": still Error");
            require(rs->lastErrorMessage().find(f.message) != string::npos, label + ": with its message");
         }
         requireLongEquals(rowsBefore, countFromNewConnection(path, "t"), label + ": nothing was stored");
         vector<string> expectedStatements = statementsBefore;
         if (pass == 0 && find(expectedStatements.begin(), expectedStatements.end(), f.sql) == expectedStatements.end()) {
            expectedStatements.push_back(f.sql);
            sort(expectedStatements.begin(), expectedStatements.end());
         }
         require(openStatementSql(db) == expectedStatements,
                 label + ": the statement was reset or finalized, not left open: " + describe(openStatementSql(db)));

         // a later success clears the database's error and leaves the cursor's alone
         unique_ptr<DBResultSet> fine(db.executeQuery("select 1"));
         requireLastError(db, DBErrorKind::None, 0, "", label + ": cleared by a later success");
         if (rs) {
            require(rs->status() == DBResultSetStatus::Error && rs->lastErrorMessage().find(f.message) != string::npos,
                    label + ": the cursor keeps its error");
         }
         if (f.label == "BEGIN inside a transaction") {
            require(db.rollback(), label + ": close the transaction");
         }
      }

      // the statement that failed works afterwards, with data that is valid
      {
         DBStatementArgs args;
         args.add(new DBLong(5));
         args.add(new DBString("fine"));
         unique_ptr<DBResultSet> rs(db.executeQuery("insert into t(id,v) values (?,?)", args));
         require(rs && rs->status() == DBResultSetStatus::Done, when + ": the same SQL works afterwards");
         requireStringEquals("fine", rawFirstRow(path, "select v from t where id = 5"), when + ": stored at once");
      }

      // a statement that cannot be prepared or bound is still nullptr
      {
         unique_ptr<DBResultSet> bad(db.executeQuery("insert intoo t values (1)"));
         require(bad == nullptr, when + ": a syntax error is nullptr");
         requireLastError(db, DBErrorKind::Sqlite, SQLITE_ERROR, "syntax error", when + ": syntax error");
         unique_ptr<DBResultSet> exists(db.executeQuery("create table t(x)"));
         require(exists == nullptr, when + ": a statement SQLite rejects when preparing it is nullptr");
         requireLastError(db, DBErrorKind::Sqlite, SQLITE_ERROR, "already exists", when + ": table exists");
         DBStatementArgs few;
         few.add(new DBLong(1));
         unique_ptr<DBResultSet> count(db.executeQuery("insert into t(id,v) values (?,?)", few));
         require(count == nullptr, when + ": a wrong number of arguments is nullptr");
         requireLastError(db, DBErrorKind::BindCount, 0, "2 parameter(s) but 1 argument(s)", when + ": count");
         DBStatementArgs tooLong;
         tooLong.add(new DBLong(6));
         tooLong.add(new DBString(TOO_LONG_FOR_LIMIT));
         unique_ptr<DBResultSet> bind;
         {
            ShortValueLimit limit(db);
            bind.reset(db.executeQuery("insert into t(id,v) values (?,?)", tooLong));
         }
         require(bind == nullptr, when + ": a bind failure is nullptr, and the row was not stored");
         requireLastError(db, DBErrorKind::BindFailed, SQLITE_TOOBIG, "parameter 2", when + ": bind");
         requireLongEquals(0, countFromNewConnection(path, "t where id = 6"), when + ": nothing stored");
      }
   }
   db.close();
}

void TestSQLiteFoundation::test_query_without_result_columns_reentrancy() {
   TEST_CASE("test_query_without_result_columns_reentrancy");
   chapeau_tests::ScopedTempDir dir;
   SQLiteDatabase db(dir.file("eager_reentry.sqlite3"));
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "create table u(a integer)") && exec(db, "insert into u(a) values (1)"), "table u");
   require(exec(db, "create table k(id integer primary key)") && exec(db, "insert into k(id) values (1)"), "table k");
   g_reentry = Reentry{&db, false, true, DBErrorKind::None, -1, DBErrorKind::RefusedStatement};
   require(sqlite3_create_function(db.sqliteHandle(), "reenter", 0, SQLITE_UTF8, nullptr,
                                   reentrantFunction, nullptr, nullptr) == SQLITE_OK,
           "register a function that uses the database");

   // the function runs while executeQuery() runs the statement: its use of the database is refused ...
   require(!exec(db, "insert intoo u values (1)"), "an earlier failure");
   unique_ptr<DBResultSet> ok(db.executeQuery("update u set a = a + reenter()"));
   require(ok && ok->status() == DBResultSetStatus::Done, "the update ran");
   require(g_reentry.calledNested && !g_reentry.nestedResult && g_reentry.nestedKind == DBErrorKind::InUse,
           "the function's own use of the database was refused");
   require(g_reentry.kindOnEntry == DBErrorKind::None, "and the statement started with no error");
   requireLastError(db, DBErrorKind::None, 0, "", "the statement succeeded: what the nested call recorded does not stay");

   // ... and when the statement fails, its own error is the one that stays
   g_reentry.calledNested = false;
   unique_ptr<DBResultSet> failed(db.executeQuery("insert into k(id) values (reenter())"));
   require(failed && failed->status() == DBResultSetStatus::Error, "the insert collides");
   require(g_reentry.calledNested && g_reentry.nestedKind == DBErrorKind::InUse, "the nested call was refused again");
   requireLastError(db, DBErrorKind::Sqlite, SQLITE_CONSTRAINT, "UNIQUE constraint failed", "its own error stays");
   db.close();
}

// ===================== H1a-11: one statement per call =====================
// executeQuery() and executeUpdate() take at most one statement. Whitespace,
// semicolons and comments may follow it; a second statement, trailing text
// that is not SQL, or a NUL byte anywhere refuses the whole call before
// anything is run. What an independent connection sees is the proof that
// nothing was.

namespace {

enum class Entry { Query, QueryArgs, Update, UpdateArgs };

const Entry ALL_ENTRIES[] = {Entry::Query, Entry::QueryArgs, Entry::Update, Entry::UpdateArgs};

const Entry ARGUMENT_ENTRIES[] = {Entry::QueryArgs, Entry::UpdateArgs};

string entryName(Entry entry) {
   switch (entry) {
      case Entry::Query: return "executeQuery(sql)";
      case Entry::QueryArgs: return "executeQuery(sql, args)";
      case Entry::Update: return "executeUpdate(sql)";
      default: return "executeUpdate(sql, args)";
   }
}

bool returnsCursor(Entry entry) {
   return Entry::Query == entry || Entry::QueryArgs == entry;
}

// what one call gave back
struct CallResult {
   bool accepted = false;             // a result set came back / executeUpdate() returned true
   DBResultSetStatus status = DBResultSetStatus::Ready;
   bool hasRow = false;               // the first next() of the result set
   long long value = -1;              // and its first column
   unsigned long rowsAffected = 77;   // set by executeUpdate(), 0 when it fails
};

// With oneParameter the argument overloads are given one argument (a long, 1).
CallResult callEntry(SQLiteDatabase& db, Entry entry, const string& sql, bool oneParameter) {
   CallResult result;
   DBStatementArgs args;
   if (oneParameter) {
      args.add(new DBLong(1));
   }

   if (returnsCursor(entry)) {
      unique_ptr<DBResultSet> rs(Entry::Query == entry ? db.executeQuery(sql)
                                                       : db.executeQuery(sql, args));
      if (rs) {
         result.accepted = true;
         result.status = rs->status();
         if (rs->next()) {
            result.hasRow = true;
            result.value = rs->longForColumnIndex(0);
         }
      }
   } else {
      result.accepted = (Entry::Update == entry) ? db.executeUpdate(sql, result.rowsAffected)
                                                 : db.executeUpdate(sql, args, result.rowsAffected);
   }
   return result;
}

// t(v), log(m) and nothing else; the three tables the cases write to, or create
bool resetBoundaryFixture(SQLiteDatabase& db) {
   const bool wasCaching = db.shouldCacheStatements();
   db.setShouldCacheStatements(false);
   const bool ok = exec(db, "drop trigger if exists trg") && exec(db, "drop table if exists a") &&
                   exec(db, "drop table if exists b") && exec(db, "drop table if exists t") &&
                   exec(db, "drop table if exists log") && exec(db, "create table t(v text)") &&
                   exec(db, "create table log(m text)");
   db.setShouldCacheStatements(wasCaching);
   db.clearCachedStatements();
   return ok;
}

// everything the cases can change, as an independent connection sees it:
// "<objects>|<t>|<log>"
string boundaryWorld(const string& path) {
   return rawFirstRow(path,
      "select ifnull((select group_concat(type || ':' || name, ',') from "
      "(select type, name from sqlite_master order by type, name)), '') || '|' || "
      "ifnull((select group_concat(v, ',') from (select v from t order by rowid)), '') || '|' || "
      "ifnull((select group_concat(m, ',') from (select m from log order by rowid)), '')");
}

const char* const FIXTURE_WORLD = "table:log,table:t||";

// the open statements themselves, not their text: a statement that was
// finalized, or prepared and not finalized, shows up in this
vector<sqlite3_stmt*> openStatementHandles(SQLiteDatabase& db) {
   vector<sqlite3_stmt*> handles;
   if (db.sqliteHandle() == nullptr) {
      return handles;
   }
   for (sqlite3_stmt* s = sqlite3_next_stmt(db.sqliteHandle(), nullptr);
        s != nullptr;
        s = sqlite3_next_stmt(db.sqliteHandle(), s)) {
      handles.push_back(s);
   }
   sort(handles.begin(), handles.end());
   return handles;
}

// SQL that must be refused as a whole, and what the message says about it
struct Refusal {
   string label;
   string sql;
   bool oneParameter;
   vector<string> messageParts;
};

const char* const ONLY_ONE = "only one statement is allowed per call; nothing was run";

}

void TestSQLiteFoundation::test_statement_boundary_refusals() {
   TEST_CASE("test_statement_boundary_refusals");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("boundary_refused.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "pragma synchronous = off"), "no fsync: the cases make many small changes");

   const string NUL(1, '\0');
   vector<Refusal> cases;

   // a second statement, or text after the first that is not SQL. 'rest' is where
   // the message must say that the trailing SQL starts: its first byte that is not blank.
   auto trailing = [&](const string& label, const string& sql, const string& rest,
                       bool oneParameter, const string& also = "") {
      const size_t offset = sql.find(rest, 1);
      require(offset != string::npos, label + ": the case holds '" + rest + "'");
      vector<string> parts = {ONLY_ONE, "the trailing SQL starts at offset " + to_string(offset)};
      if (!also.empty()) {
         parts.push_back(also);
      }
      cases.push_back({label, sql, oneParameter, parts});
   };
   // a NUL byte: the message says where the first one is
   auto withNul = [&](const string& label, const string& sql) {
      cases.push_back({label, sql, false,
                       {"the SQL contains a NUL byte at offset " + to_string(sql.find('\0')),
                        "nothing was run"}});
   };

   trailing("two CREATE TABLE", "create table a(x); create table b(y)", "create table b", false);
   trailing("two CREATE TABLE, semicolons and newlines", "create table a(x);\ncreate table b(y);\n", "create table b", false);
   trailing("two INSERT", "insert into t(v) values ('1'); insert into t(v) values ('2')", "insert into t(v) values ('2')", false);
   trailing("two INSERT, a parameter in the first", "insert into t(v) values (?); insert into t(v) values ('2')",
            "insert into t(v) values ('2')", true);
   trailing("two INSERT, a parameter in each", "insert into t(v) values (?); insert into t(v) values (?)",
            "insert into t(v) values (?)", true);
   trailing("two SELECT", "select 1; select 2", "select 2", false);
   trailing("two SELECT, a parameter in the first", "select ?; select 2", "select 2", true);
   trailing("three INSERT", "insert into t(v) values ('1'); insert into t(v) values ('2'); insert into t(v) values ('3')",
            "insert into t(v) values ('2')", false);
   trailing("INSERT then DELETE", "insert into t(v) values ('keep'); delete from t", "delete from t", false);
   trailing("a statement and a comment and a statement", "select 1; -- note\n select 2", "-- note", false);
   trailing("a statement with a literal that holds a semicolon, then another", "insert into t(v) values ('a;b'); delete from t",
            "delete from t", false);
   trailing("a trigger, then another statement",
            "create trigger trg after insert on t begin insert into log(m) values ('x'); end; delete from t", "delete from t", false);
   trailing("a leading semicolon, two statements", "; select 1; select 2", "select 2", false);
   // the trailing SQL starts at its first byte that is not blank, which may be a semicolon or a comment
   trailing("a second semicolon, then a statement", "select 1;; select 2", "; select 2", false);
   trailing("empty statements between two statements", "select 1; ; ;\n; select 2", "; ;\n; select 2", false);
   trailing("comments and semicolons between two statements", "insert into t(v) values ('1'); /* x */ ; -- y\n ; delete from t",
            "/* x */ ; -- y\n ; delete from t", false);
   trailing("a semicolon directly before the second statement", "delete from t;;delete from log", ";delete from log", false);
   trailing("a transaction in one call", "begin; insert into t(v) values ('1'); commit", "insert into t(v) values ('1')", false);
   trailing("BEGIN and a statement", "begin; select 1", "select 1", false);
   trailing("a statement, then something that is not SQL", "insert into t(v) values ('1'); this is not sql", "this is not sql", false,
            "the trailing SQL does not prepare");
   trailing("a statement, then half a statement", "insert into t(v) values ('1'); insert into", "insert into", false,
            "the trailing SQL does not prepare");
   trailing("a statement, then a statement that needs the first (a table)", "create table a(x); insert into a(x) values (1)",
            "insert into a(x) values (1)", false, "no such table: a");
   trailing("a statement, then a statement that needs the first (an index)", "create table a(x); create index ia on a(x)",
            "create index ia on a(x)", false, "no such table");
   trailing("a statement and an unterminated string", "select 1; select 'abc", "select 'abc", false);

   withNul("a NUL byte at the start", NUL + "select 1");
   withNul("a NUL byte in the middle", "select 1" + NUL + "; select 2");
   withNul("a NUL byte at the end", "select 1;" + NUL);
   withNul("a NUL byte after the statement", "insert into t(v) values ('1')" + NUL);
   withNul("only a NUL byte", NUL);
   withNul("a NUL byte in a string literal", "insert into t(v) values ('a" + NUL + "b')");
   withNul("a NUL byte in a trailing comment", "select 1; -- note" + NUL + "x");
   withNul("two NUL bytes", "select 1" + NUL + NUL);
   withNul("a NUL byte in a second statement", "select 1; select 2" + NUL);

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);
      require(resetBoundaryFixture(db), when + ": fixture");
      require(boundaryWorld(path) == FIXTURE_WORLD, when + ": the fixture is what the cases assume");
      // a valid statement that is run after each refusal; run once first so that
      // the cache (when there is one) holds it before the open statements are noted
      require(exec(db, "update log set m = m where 0"), when + ": a valid statement");
      const vector<sqlite3_stmt*> openBefore = openStatementHandles(db);

      for (const Refusal& c : cases) {
         for (Entry entry : ALL_ENTRIES) {
            for (int repeat = 0; repeat < 2; ++repeat) {
               const string label = when + ": " + entryName(entry) + ": " + c.label +
                                    (repeat == 0 ? "" : " (again)");
               const CallResult r = callEntry(db, entry, c.sql, c.oneParameter);

               require(!r.accepted, label + ": refused");
               if (!returnsCursor(entry)) {
                  requireLongEquals(0, static_cast<long long>(r.rowsAffected), label + ": rowsAffected is 0");
               }
               requireLastError(db, DBErrorKind::RefusedStatement, 0, c.messageParts[0], label);
               for (const string& part : c.messageParts) {
                  require(db.lastErrorMessage().find(part) != string::npos,
                          label + ": the message says '" + part + "' (got '" + db.lastErrorMessage() + "')");
               }

               requireStringEquals(FIXTURE_WORLD, boundaryWorld(path),
                                   label + ": nothing was run, seen from an independent connection");
               require(openStatementHandles(db) == openBefore,
                       label + ": no statement was left behind, cached, or finalized: " + describe(openStatementSql(db)));
               require(!db.inTransaction(), label + ": no transaction was left open");

               require(exec(db, "update log set m = m where 0"), label + ": a valid statement works right after");
               requireLastError(db, DBErrorKind::None, 0, "", label + ": and the refusal is gone");
            }
         }
      }
   }

   db.close();
}

namespace {

// SQL that must run, with what is true afterwards
struct Acceptance {
   string label;
   string sql;
   bool oneParameter;
   bool returnsRows;           // a SELECT: executeUpdate() refuses it as a statement that returns rows
   string worldAfter;          // boundaryWorld() afterwards
   long long rowsAffected;     // of executeUpdate(), or -1: not checked
   string worldAfterInsert;    // if not empty: after "insert into t(v) values ('z')" too (the trigger fires)
};

}

void TestSQLiteFoundation::test_statement_boundary_accepts() {
   TEST_CASE("test_statement_boundary_accepts");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("boundary_accepted.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "pragma synchronous = off"), "no fsync: the cases make many small changes");

   const string FIXTURE = FIXTURE_WORLD;
   vector<Acceptance> cases;

   // one SELECT, with what may follow it
   const vector<string> harmlessTails = {
      ";", ";;;", " ;  ; ;", "   \t\n", " -- note", "; -- note\n", " /* c */", "; /* a */ ; -- b",
      " /* unterminated", " -- no newline at end", "\n;\n", " /* one */ /* two */ ;; -- three"
   };
   for (const string& tail : harmlessTails) {
      cases.push_back({"SELECT then [" + tail + "]", "select 1" + tail, false, true, FIXTURE, -1, ""});
      cases.push_back({"SELECT with a parameter then [" + tail + "]", "select ?" + tail, true, true, FIXTURE, -1, ""});
   }
   // blank or empty before the statement
   for (const string lead : {"  \n ", "/* lead */ ", "-- lead\n", "; ", ";;  ; "}) {
      cases.push_back({"[" + lead + "] then SELECT", lead + "select 1", false, true, FIXTURE, -1, ""});
   }
   // semicolons and statement text that are not statement boundaries
   cases.push_back({"a semicolon in a string literal", "select length('a;b;c') - 4", false, true, FIXTURE, -1, ""});
   cases.push_back({"a semicolon in a quoted name", "select 1 as \"a;b\"", false, true, FIXTURE, -1, ""});
   cases.push_back({"a statement in a block comment", "select 1 /* ; select 2 ; */", false, true, FIXTURE, -1, ""});
   cases.push_back({"a statement in a line comment", "select 1 -- ; select 2", false, true, FIXTURE, -1, ""});
   cases.push_back({"an escaped quote and a semicolon", "select length('it''s;') - 4", false, true, FIXTURE, -1, ""});

   // statements that return no rows
   const string ONE_ROW = "table:log,table:t|x|";
   for (const string& tail : harmlessTails) {
      cases.push_back({"INSERT then [" + tail + "]", "insert into t(v) values ('x')" + tail, false, false, ONE_ROW, 1, ""});
      cases.push_back({"INSERT with a parameter then [" + tail + "]", "insert into t(v) values (?)" + tail, true, false,
                       "table:log,table:t|1|", 1, ""});
      cases.push_back({"CREATE TABLE then [" + tail + "]", "create table a(x)" + tail, false, false,
                       "table:a,table:log,table:t||", -1, ""});
   }
   cases.push_back({"a semicolon in the value inserted", "insert into t(v) values ('a;b')", false, false,
                    "table:log,table:t|a;b|", 1, ""});
   cases.push_back({"a semicolon in the value inserted, and a tail", "insert into t(v) values ('a;b');  ;", false, false,
                    "table:log,table:t|a;b|", 1, ""});
   cases.push_back({"two statements' worth of text in a string", "insert into t(v) values ('x; delete from t');", false, false,
                    "table:log,table:t|x; delete from t|", 1, ""});
   const string TRIGGER = "create trigger trg after insert on t begin "
                          "insert into log(m) values ('fired'); update log set m = m where 0; end";
   cases.push_back({"a trigger with two statements in its body", TRIGGER, false, false,
                    "table:log,table:t,trigger:trg||", -1, "table:log,table:t,trigger:trg|z|fired"});
   cases.push_back({"a trigger, then a semicolon", TRIGGER + ";", false, false,
                    "table:log,table:t,trigger:trg||", -1, "table:log,table:t,trigger:trg|z|fired"});
   cases.push_back({"a trigger, then a comment", TRIGGER + "; -- done", false, false,
                    "table:log,table:t,trigger:trg||", -1, "table:log,table:t,trigger:trg|z|fired"});

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);

      for (const Acceptance& c : cases) {
         for (Entry entry : (c.oneParameter ? vector<Entry>(begin(ARGUMENT_ENTRIES), end(ARGUMENT_ENTRIES))
                                            : vector<Entry>(begin(ALL_ENTRIES), end(ALL_ENTRIES)))) {
            const string label = when + ": " + entryName(entry) + ": " + c.label;
            require(resetBoundaryFixture(db), label + ": fixture");
            const vector<sqlite3_stmt*> openBefore = openStatementHandles(db);

            const CallResult r = callEntry(db, entry, c.sql, c.oneParameter);

            if (c.returnsRows && !returnsCursor(entry)) {
               // a SELECT is refused by executeUpdate() for what it is, not for its tail
               require(!r.accepted, label + ": executeUpdate() refuses a statement that returns rows");
               requireLastError(db, DBErrorKind::RefusedStatement, 0, "returns rows", label);
               require(db.lastErrorMessage().find("only one statement") == string::npos,
                       label + ": and not as a second statement (got '" + db.lastErrorMessage() + "')");
               requireLongEquals(0, static_cast<long long>(r.rowsAffected), label + ": rowsAffected is 0");
            } else if (returnsCursor(entry)) {
               require(r.accepted, label + ": a result set (" + db.lastErrorMessage() + ")");
               requireLastError(db, DBErrorKind::None, 0, "", label);
               if (c.returnsRows) {
                  require(r.hasRow && r.value == 1, label + ": one row, holding 1");
               } else {
                  require(r.status == DBResultSetStatus::Done && !r.hasRow, label + ": finished at once, no rows");
               }
            } else {
               require(r.accepted, label + ": accepted (" + db.lastErrorMessage() + ")");
               requireLastError(db, DBErrorKind::None, 0, "", label);
               if (c.rowsAffected >= 0) {
                  requireLongEquals(c.rowsAffected, static_cast<long long>(r.rowsAffected), label + ": rowsAffected");
               }
            }

            const bool expectedToRun = !(c.returnsRows && !returnsCursor(entry));
            requireStringEquals(expectedToRun ? c.worldAfter : FIXTURE_WORLD, boundaryWorld(path),
                                label + ": what an independent connection sees");

            if (expectedToRun && !c.worldAfterInsert.empty()) {
               require(exec(db, "insert into t(v) values ('z')"), label + ": an insert");
               requireStringEquals(c.worldAfterInsert, boundaryWorld(path), label + ": and the trigger fires");
            }

            if (pass != 0) {
               require(openStatementHandles(db) == openBefore,
                       label + ": nothing is left open when the cache is off: " + describe(openStatementSql(db)));
            }
         }
      }
   }

   // no statement at all: empty, blank, only comments, only semicolons
   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);
      require(resetBoundaryFixture(db), when + ": fixture");

      for (const string& sql : vector<string>{"", "   ", "\n\t", "-- nothing here", "/* nothing here */", ";", ";;", "; ;  ;",
                                               " ; -- x\n ;", "/* a */ ; /* b */ -- c"}) {
         for (Entry entry : ALL_ENTRIES) {
            const string label = when + ": " + entryName(entry) + ": [" + sql + "]";
            const vector<sqlite3_stmt*> openBefore = openStatementHandles(db);
            const CallResult r = callEntry(db, entry, sql, false);

            if (returnsCursor(entry)) {
               // the contract that was there before: a result set with nothing in it
               require(r.accepted && r.status == DBResultSetStatus::Done && !r.hasRow, label + ": a finished result set");
               requireLastError(db, DBErrorKind::None, 0, "", label);
            } else {
               // executeUpdate() has never had a contract for this; it is not a refused statement
               require(db.lastErrorKind() != DBErrorKind::RefusedStatement, label + ": it is not refused");
            }
            requireStringEquals(FIXTURE_WORLD, boundaryWorld(path), label + ": nothing changed");
            require(openStatementHandles(db) == openBefore, label + ": no statement was opened");
         }
      }
   }

   db.close();
}

void TestSQLiteFoundation::test_statement_boundary_and_the_statement_cache() {
   TEST_CASE("test_statement_boundary_and_the_statement_cache");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("boundary_cache.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "pragma synchronous = off"), "no fsync: the cases make many small changes");
   db.setShouldCacheStatements(true);
   require(resetBoundaryFixture(db), "fixture");
   const string NUL(1, '\0');

   const vector<string> refused = {
      "select 1; select 2",
      "select 1;" + NUL,
      "insert into log(m) values ('c'); delete from log",
      "select 1; this is not sql",
      "select 1" + NUL + "; select 2",
   };

   // two statements are cached ...
   requireLongEquals(1, scalar(db, "select 1"), "a query");
   require(exec(db, "insert into log(m) values ('c')"), "an update");
   const vector<sqlite3_stmt*> cached = openStatementHandles(db);
   requireLongEquals(2, static_cast<long long>(cached.size()), "two statements are cached");

   // ... refusals, the first statement of which is the SQL of a cached one, add and remove nothing ...
   for (const string& sql : refused) {
      for (Entry entry : ALL_ENTRIES) {
         const CallResult r = callEntry(db, entry, sql, false);
         require(!r.accepted, entryName(entry) + ": refused: " + sql);
         require(openStatementHandles(db) == cached, entryName(entry) + ": the cache is as it was: " + sql);
      }
   }

   // ... and the cached statements are still valid, and are the very ones that run
   requireLongEquals(1, scalar(db, "select 1"), "the cached query still runs");
   require(exec(db, "insert into log(m) values ('c')"), "the cached update still runs");
   require(openStatementHandles(db) == cached, "by the same two statements");
   requireStringEquals("table:log,table:t||c,c", boundaryWorld(path), "the update ran each time it was asked to, and only then");

   // the cache is bounded as before: a refusal is neither an entry, nor a use of one
   db.clearCachedStatements();
   db.setStatementCacheLimit(2);
   requireLongEquals(11, scalar(db, "select 11"), "A");
   requireLongEquals(12, scalar(db, "select 12"), "B");
   for (Entry entry : ALL_ENTRIES) {
      require(!callEntry(db, entry, "select 11; select 13", false).accepted, entryName(entry) + ": refused");
      require(!callEntry(db, entry, "select 14" + NUL, false).accepted, entryName(entry) + ": refused, with a NUL byte");
   }
   requireLongEquals(14, scalar(db, "select 14"), "C");
   require(openStatementSql(db) == (vector<string>{"select 12", "select 14"}),
           "A, the least recently used, went to make room for C: " + describe(openStatementSql(db)));
   db.setStatementCacheLimit(128);

   // SQL with a harmless tail is cached as any SQL, and reused
   db.clearCachedStatements();
   requireLongEquals(1, scalar(db, "select 1;  -- note"), "a query with a tail");
   const vector<sqlite3_stmt*> withTail = openStatementHandles(db);
   requireLongEquals(1, static_cast<long long>(withTail.size()), "cached");
   requireLongEquals(1, scalar(db, "select 1;  -- note"), "again");
   requireLongEquals(1, scalar(db, "select 1;  -- note"), "and again");
   require(openStatementHandles(db) == withTail, "by the same statement each time");
   require(exec(db, "insert into log(m) values ('r');"), "an update with a tail");
   const vector<sqlite3_stmt*> updateWithTail = openStatementHandles(db);
   requireLongEquals(2, static_cast<long long>(updateWithTail.size()), "cached");
   require(exec(db, "insert into log(m) values ('r');") && exec(db, "insert into log(m) values ('r');"), "twice more");
   require(openStatementHandles(db) == updateWithTail, "by the same statement each time");
   requireStringEquals("table:log,table:t||c,c,r,r,r", boundaryWorld(path), "each run stored one row");

   // with no cache nothing is kept, refused or not
   db.setShouldCacheStatements(false);
   db.clearCachedStatements();
   require(openStatementHandles(db).empty(), "nothing is open");
   for (const string& sql : refused) {
      for (Entry entry : ALL_ENTRIES) {
         require(!callEntry(db, entry, sql, false).accepted, entryName(entry) + ": refused, uncached: " + sql);
         require(openStatementHandles(db).empty(), entryName(entry) + ": and nothing is open");
      }
   }

   db.close();
}

void TestSQLiteFoundation::test_statement_boundary_with_transactions_and_cursors() {
   TEST_CASE("test_statement_boundary_with_transactions_and_cursors");
   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("boundary_context.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);
   require(db.open(), "open");
   require(exec(db, "pragma synchronous = off"), "no fsync: the cases make many small changes");

   for (int pass = 0; pass < 2; ++pass) {
      const string when = (pass == 0) ? "cached statements" : "uncached statements";
      db.setShouldCacheStatements(pass == 0);
      require(resetBoundaryFixture(db), when + ": fixture");

      // a refusal in the middle of a transaction leaves the transaction as it was
      require(db.beginDeferredTransaction(), when + ": begin");
      require(exec(db, "insert into t(v) values ('in transaction')"), when + ": an insert");
      for (Entry entry : ALL_ENTRIES) {
         const CallResult r = callEntry(db, entry, "insert into t(v) values ('lost'); insert into t(v) values ('lost too')", false);
         require(!r.accepted, when + ": " + entryName(entry) + ": refused");
         require(db.inTransaction(), when + ": " + entryName(entry) + ": the transaction is still open");
      }
      require(exec(db, "insert into t(v) values ('second')"), when + ": and goes on");
      require(db.commit(), when + ": commit");
      requireStringEquals("table:log,table:t|in transaction,second|", boundaryWorld(path),
                          when + ": only what was asked for singly is stored");

      // a refusal made as the first call is no transaction at all
      require(!exec(db, "begin; insert into t(v) values ('never')"), when + ": BEGIN and a statement is refused");
      require(!db.inTransaction(), when + ": no transaction was started");
      require(!exec(db, "rollback; commit"), when + ": ROLLBACK and COMMIT in one call are refused");

      // a live result set is not disturbed by a refusal
      require(exec(db, "insert into t(v) values ('third')"), when + ": a third row");
      unique_ptr<SQLiteResultSet> reading(openCursor(db, "select v from t order by rowid"));
      require(reading && reading->next(), when + ": a result set, on its first row");
      for (Entry entry : ALL_ENTRIES) {
         const CallResult r = callEntry(db, entry, "select v from t order by rowid; select 2", false);
         require(!r.accepted, when + ": " + entryName(entry) + ": the same SQL and another statement is refused");
      }
      string value;
      require(reading->stringForColumnIndex(0, value) && value == "in transaction", when + ": the result set is where it was");
      require(reading->next() && reading->stringForColumnIndex(0, value) && value == "second", when + ": and goes on");
      require(reading->next() && reading->stringForColumnIndex(0, value) && value == "third", when + ": to the end");
      require(!reading->next(), when + ": which is the end");
      reading.reset();
   }

   db.close();
}

void TestSQLiteFoundation::test_error_kind_names() {
   TEST_CASE("test_error_kind_names");
   struct Named {
      DBErrorKind kind;
      const char* name;
   };
   const Named ALL[] = {
      {DBErrorKind::None, "None"},
      {DBErrorKind::Sqlite, "Sqlite"},
      {DBErrorKind::BindCount, "BindCount"},
      {DBErrorKind::BindFailed, "BindFailed"},
      {DBErrorKind::NoConnection, "NoConnection"},
      {DBErrorKind::AlreadyOpen, "AlreadyOpen"},
      {DBErrorKind::InUse, "InUse"},
      {DBErrorKind::RefusedStatement, "RefusedStatement"},
      {DBErrorKind::UnsupportedLibrary, "UnsupportedLibrary"},
   };

   for (const Named& n : ALL) {
      requireStringEquals(n.name, dbErrorKindName(n.kind), string("the name of kind ") + n.name);
   }

   // the existing kinds keep their values, and the new one comes after them
   requireLongEquals(0, static_cast<long long>(DBErrorKind::None), "None is 0");
   requireLongEquals(7, static_cast<long long>(DBErrorKind::RefusedStatement), "RefusedStatement is still 7");
   requireLongEquals(8, static_cast<long long>(DBErrorKind::UnsupportedLibrary), "UnsupportedLibrary is 8");

   // a value that is no kind has a name too, and it is not one of the real ones
   requireStringEquals("Unknown", dbErrorKindName(static_cast<DBErrorKind>(99)), "a value that is no kind");
}

void TestSQLiteFoundation::test_open_with_a_threadsafe_library() {
   TEST_CASE("test_open_with_a_threadsafe_library");
   // The refusal of a SQLite without threading support is tested against a real
   // SQLITE_THREADSAFE=0 library, in its own program (tests/regression, option
   // CHAPEAU_THREADSAFE0_TEST). What can be tested here is that the library
   // this suite runs against is not refused, and that open() is otherwise as it was.
   require(::sqlite3_threadsafe() != 0, "this suite runs against a SQLite with threading support");

   chapeau_tests::ScopedTempDir dir;
   const string path = dir.file("guard.sqlite3");
   SQLiteDatabase db(path);
   db.setLogsErrors(false);

   require(db.open(), "open() is not refused");
   requireLastError(db, DBErrorKind::None, 0, "", "and records no error");
   sqlite3* const handle = db.sqliteHandle();
   require(handle != nullptr, "there is a connection");
   require(db.goodConnection(), "which works");
   require(exec(db, "create table g(a integer)"), "create a table");

   // an open database keeps its own answer, and its connection
   require(!db.open(), "open() on an open database");
   requireLastError(db, DBErrorKind::AlreadyOpen, 0, "already open", "second open()");
   require(db.sqliteHandle() == handle, "the connection is unchanged");
   require(exec(db, "insert into g(a) values (1)"), "and still works");

   // close and open again
   db.close();
   require(db.sqliteHandle() == nullptr, "closed");
   require(db.open(), "opens again");
   requireLastError(db, DBErrorKind::None, 0, "", "with no error");
   unique_ptr<SQLiteResultSet> rs(openCursor(db, "select a from g"));
   require(rs && rs->next() && rs->longForColumnIndex(0) == 1, "and the data is there");
   rs.reset();
   db.close();

   // a failed open still records its own kind (not UnsupportedLibrary), and a later one works
   SQLiteDatabase missing((fs::path(dir.file("not_there")) / "x.sqlite3").string());
   missing.setLogsErrors(false);
   require(!missing.open(), "open() in a directory that does not exist");
   requireLastError(missing, DBErrorKind::Sqlite, SQLITE_CANTOPEN, "unable to open database file", "the failure is SQLite's");
   require(missing.lastErrorKind() != DBErrorKind::UnsupportedLibrary, "and not the library guard");
   require(missing.sqliteHandle() == nullptr, "no connection is left");
   require(fs::create_directory(fs::path(dir.file("not_there"))), "create the directory");
   require(missing.open(), "now it opens");
   requireLastError(missing, DBErrorKind::None, 0, "", "and the error is cleared");
   missing.close();
}
