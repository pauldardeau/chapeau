#include "test_sqlite_foundation.h"

#include <climits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "scoped_temp_dir.h"
#include "SQLiteDatabase.h"
#include "SQLiteResultSet.h"
#include "DBStatementArgs.h"
#include "DBBool.h"
#include "DBData.h"
#include "DBDate.h"
#include "DBDouble.h"
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
// answers like a NULL column everywhere, and never touches freed memory.
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

   unique_ptr<DBData> data(rs.dataForColumnIndex(0));
   require(data != nullptr && data->length() == 0, when + ": dataForColumnIndex is empty");
   unique_ptr<DBDate> date(rs.dateForColumnIndex(0));
   require(date != nullptr, when + ": dateForColumnIndex");
   if (date) {
      requireDoubleEquals(0.0, date->timeIntervalSince1970(), when + ": date is 0.0");
   }
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
