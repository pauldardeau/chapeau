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
         // the arguments must outlive the result set that was built from them
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
