#include "test_sqlite_database.h"
#include "fs_test_case.h"
#include "SQLiteDatabase.h"
#include "OSUtils.h"

using namespace std;
using namespace poivre;
using namespace chapeau;


TestSQLiteDatabase::TestSQLiteDatabase() :
   TestSuite("TestSQLiteDatabase") {
}

void TestSQLiteDatabase::runTests() {
   test_constructor();
   test_destructor();
   test_sqliteLibVersion();
   test_databasePath();
   test_sqliteHandle();
   test_open();
   test_close();
   test_goodConnection();
   /*
   test_complainAboutInUse();
   test_lastErrorMessage();
   test_hadError();
   test_lastErrorCode();
   test_lastInsertRowId();
   test_bindNull();
   test_bindData();
   test_bindDate();
   test_bindBool();
   test_bindInt();
   test_bindLong();
   test_bindFloat();
   test_bindDouble();
   test_bindString();
   test_executeQuery();
   test_executeQuery_withArgs();
   test_executeUpdate();
   test_executeUpdate_withArgs();
   test_rollback();
   test_commit();
   test_beginDeferredTransaction();
   test_beginTransaction();
   test_logsErrors();
   test_setLogsErrors();
   test_crashOnErrors();
   test_setCrashOnErrors();
   test_inUse();
   test_setInUse();
   test_inTransaction();
   test_setInTransaction();
   test_traceExecution();
   test_setTraceExecution();
   test_checkedOut();
   test_setCheckedOut();
   test_busyRetryTimeout();
   test_setBusyRetryTimeout();
   */
}

void TestSQLiteDatabase::test_constructor() {
   TEST_CASE("test_constructor");
   string test_dir = "/tmp/test_chapeau_sqlitedb_test_ctor";
   FSTestCase test_case(*this, test_dir);
   string file_path = chaudiere::OSUtils::pathJoin(test_dir, "test.sqlite3");
   SQLiteDatabase db(file_path);
}

void TestSQLiteDatabase::test_destructor() {
   TEST_CASE("test_destructor");
}

void TestSQLiteDatabase::test_sqliteLibVersion() {
   TEST_CASE("test_sqliteLibVersion");
   string sqlite_lib_version = SQLiteDatabase::sqliteLibVersion();
   require(sqlite_lib_version.length() > 0, "sqliteLibVersion should return non-empty string");
}

void TestSQLiteDatabase::test_databasePath() {
   TEST_CASE("test_databasePath");
   string test_dir = "/tmp/test_chapeau_sqlitedb_test_databasePath";
   FSTestCase test_case(*this, test_dir);
   string file_path = chaudiere::OSUtils::pathJoin(test_dir, "test.sqlite3");
   SQLiteDatabase db(file_path);
   string db_path = db.databasePath();
   requireStringEquals(file_path, db_path, "databasePath should match what's given to ctor");
}

void TestSQLiteDatabase::test_sqliteHandle() {
   TEST_CASE("test_sqliteHandle");
   string test_dir = "/tmp/test_chapeau_sqlitedb_test_sqliteHandle";
   FSTestCase test_case(*this, test_dir);
   string file_path = chaudiere::OSUtils::pathJoin(test_dir, "test.sqlite3");
   SQLiteDatabase db(file_path);
   sqlite3* db_handle = db.sqliteHandle();
   require(db_handle == nullptr, "sqliteHandle must return nullptr when DB closed");
   require(db.open(), "open should succeed on valid path");
   db_handle = db.sqliteHandle();
   require(db_handle != nullptr, "sqliteHandle must return non-nullptr when DB open");
   db.close();
   db_handle = db.sqliteHandle();
   require(db_handle == nullptr, "sqliteHandle must return nullptr when DB closed");
}

void TestSQLiteDatabase::test_open() {
   TEST_CASE("test_open");
   string test_dir = "/tmp/test_chapeau_sqlitedb_test_open";
   FSTestCase test_case(*this, test_dir);
   string file_path = chaudiere::OSUtils::pathJoin(test_dir, "test.sqlite3");
   SQLiteDatabase db(file_path);
   require(db.open(), "open should succeed on valid path");
}

void TestSQLiteDatabase::test_close() {
   TEST_CASE("test_close");
   string test_dir = "/tmp/test_chapeau_sqlitedb_test_close";
   FSTestCase test_case(*this, test_dir);
   string file_path = chaudiere::OSUtils::pathJoin(test_dir, "test.sqlite3");
   SQLiteDatabase db(file_path);
   db.close();
   require(db.open(), "open should succeed on valid path");
   db.close();
   requireFalse(db.goodConnection(), "goodConnection should return false after close");
}

void TestSQLiteDatabase::test_goodConnection() {
   TEST_CASE("test_goodConnection");
   string test_dir = "/tmp/test_chapeau_sqlitedb_test_goodConnection";
   FSTestCase test_case(*this, test_dir);
   string file_path = chaudiere::OSUtils::pathJoin(test_dir, "test.sqlite3");
   SQLiteDatabase db(file_path);
   require(db.open(), "open should succeed on valid path");
   require(db.goodConnection(), "goodConnection should succeed when DB is open");
   db.close();
   requireFalse(db.goodConnection(), "goodConnection should return false when DB is closed");
}

void TestSQLiteDatabase::test_complainAboutInUse() {
   TEST_CASE("test_complainAboutInUse");
   //TODO: implement test_complainAboutInUse
}

void TestSQLiteDatabase::test_lastErrorMessage() {
   TEST_CASE("test_lastErrorMessage");
   //TODO: implement test_lastErrorMessage
}

void TestSQLiteDatabase::test_hadError() {
   TEST_CASE("test_hadError");
   //TODO: implement test_hadError
}

void TestSQLiteDatabase::test_lastErrorCode() {
   TEST_CASE("test_lastErrorCode");
   //TODO: implement test_lastErrorCode
}

void TestSQLiteDatabase::test_lastInsertRowId() {
   TEST_CASE("test_lastInsertRowId");
   //TODO: implement test_lastInsertRowId
}

void TestSQLiteDatabase::test_bindNull() {
   TEST_CASE("test_bindNull");
   //TODO: implement test_bindNull
}

void TestSQLiteDatabase::test_bindData() {
   TEST_CASE("test_bindData");
   //TODO: implement test_bindData
}

void TestSQLiteDatabase::test_bindDate() {
   TEST_CASE("test_bindDate");
   //TODO: implement test_bindDate
}

void TestSQLiteDatabase::test_bindBool() {
   TEST_CASE("test_bindBool");
   //TODO: implement test_bindBool
}

void TestSQLiteDatabase::test_bindInt() {
   TEST_CASE("test_bindInt");
   //TODO: implement test_bindInt
}

void TestSQLiteDatabase::test_bindLong() {
   TEST_CASE("test_bindLong");
   //TODO: implement test_bindLong
}

void TestSQLiteDatabase::test_bindFloat() {
   TEST_CASE("test_bindFloat");
   //TODO: implement test_bindFloat
}

void TestSQLiteDatabase::test_bindDouble() {
   TEST_CASE("test_bindDouble");
   //TODO: implement test_bindDouble
}

void TestSQLiteDatabase::test_bindString() {
   TEST_CASE("test_bindString");
   //TODO: implement test_bindString
}

void TestSQLiteDatabase::test_executeQuery() {
   TEST_CASE("test_executeQuery");
   //TODO: implement test_executeQuery
}

void TestSQLiteDatabase::test_executeQuery_withArgs() {
   TEST_CASE("test_executeQuery_withArgs");
   //TODO: implement test_executeQuery_withArgs
}

void TestSQLiteDatabase::test_executeUpdate() {
   TEST_CASE("test_executeUpdate");
   //TODO: implement test_executeUpdate
}

void TestSQLiteDatabase::test_executeUpdate_withArgs() {
   TEST_CASE("test_executeUpdate_withArgs");
   //TODO: implement test_executeUpdate_withArgs
}

void TestSQLiteDatabase::test_rollback() {
   TEST_CASE("test_rollback");
   //TODO: implement test_rollback
}

void TestSQLiteDatabase::test_commit() {
   TEST_CASE("test_commit");
   //TODO: implement test_commit
}

void TestSQLiteDatabase::test_beginDeferredTransaction() {
   TEST_CASE("test_beginDeferredTransaction");
   //TODO: implement test_beginDeferredTransaction
}

void TestSQLiteDatabase::test_beginTransaction() {
   TEST_CASE("test_beginTransaction");
   //TODO: implement test_beginTransaction
}

void TestSQLiteDatabase::test_logsErrors() {
   TEST_CASE("test_logsErrors");
   //TODO: implement test_logsErrors
}

void TestSQLiteDatabase::test_setLogsErrors() {
   TEST_CASE("test_setLogsErrors");
   //TODO: implement test_setLogsErrors
}

void TestSQLiteDatabase::test_crashOnErrors() {
   TEST_CASE("test_crashOnErrors");
   //TODO: implement test_crashOnErrors
}

void TestSQLiteDatabase::test_setCrashOnErrors() {
   TEST_CASE("test_setCrashOnErrors");
   //TODO: implement test_setCrashOnErrors
}

void TestSQLiteDatabase::test_inUse() {
   TEST_CASE("test_inUse");
   //TODO: implement test_inUse
}

void TestSQLiteDatabase::test_setInUse() {
   TEST_CASE("test_setInUse");
   //TODO: implement test_setInUse
}

void TestSQLiteDatabase::test_inTransaction() {
   TEST_CASE("test_inTransaction");
   //TODO: implement test_inTransaction
}

void TestSQLiteDatabase::test_setInTransaction() {
   TEST_CASE("test_setInTransaction");
   //TODO: implement test_setInTransaction
}

void TestSQLiteDatabase::test_traceExecution() {
   TEST_CASE("test_traceExecution");
   //TODO: implement test_traceExecution
}

void TestSQLiteDatabase::test_setTraceExecution() {
   TEST_CASE("test_setTraceExecution");
   //TODO: implement test_setTraceExecution
}

void TestSQLiteDatabase::test_checkedOut() {
   TEST_CASE("test_checkedOut");
   //TODO: implement test_checkedOut
}

void TestSQLiteDatabase::test_setCheckedOut() {
   TEST_CASE("test_setCheckedOut");
   //TODO: implement test_setCheckedOut
}

void TestSQLiteDatabase::test_busyRetryTimeout() {
   TEST_CASE("test_busyRetryTimeout");
   string test_dir = "/tmp/test_chapeau_sqlitedb_test_busyRetryTimeout";
   FSTestCase test_case(*this, test_dir);
   string file_path = chaudiere::OSUtils::pathJoin(test_dir, "test.sqlite3");
   SQLiteDatabase db(file_path);
   require(db.open(), "open should succeed on valid path");
   int timeout = db.busyRetryTimeout();
   require(timeout == 0, "timeout should be 0");
   timeout = 10;
   db.setBusyRetryTimeout(timeout);
   int get_timeout = db.busyRetryTimeout();
   require(timeout == get_timeout, "busy retry timeout should match what was set");
}

void TestSQLiteDatabase::test_setBusyRetryTimeout() {
   TEST_CASE("test_setBusyRetryTimeout");
   string test_dir = "/tmp/test_chapeau_sqlitedb_test_setBusyRetryTimeout";
   FSTestCase test_case(*this, test_dir);
   string file_path = chaudiere::OSUtils::pathJoin(test_dir, "test.sqlite3");
   SQLiteDatabase db(file_path);
   require(db.open(), "open should succeed on valid path");
   int timeout = 15;
   db.setBusyRetryTimeout(timeout);
   int get_timeout = db.busyRetryTimeout();
   require(timeout == get_timeout, "busy retry timeout should match what was set");
}

