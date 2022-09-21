#include "test_sqlite_database.h"
#include "SQLiteDatabase.h"


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
}

void TestSQLiteDatabase::test_constructor() {
   TEST_CASE("test_constructor");
}

void TestSQLiteDatabase::test_destructor() {
   TEST_CASE("test_destructor");
}

void TestSQLiteDatabase::test_sqliteLibVersion() {
   TEST_CASE("test_sqliteLibVersion");
}

void TestSQLiteDatabase::test_databasePath() {
   TEST_CASE("test_databasePath");
}

void TestSQLiteDatabase::test_sqliteHandle() {
   TEST_CASE("test_sqliteHandle");
}

void TestSQLiteDatabase::test_open() {
   TEST_CASE("test_open");
}

void TestSQLiteDatabase::test_close() {
   TEST_CASE("test_close");
}

void TestSQLiteDatabase::test_goodConnection() {
   TEST_CASE("test_goodConnection");
}

void TestSQLiteDatabase::test_complainAboutInUse() {
   TEST_CASE("test_complainAboutInUse");
}

void TestSQLiteDatabase::test_lastErrorMessage() {
   TEST_CASE("test_lastErrorMessage");
}

void TestSQLiteDatabase::test_hadError() {
   TEST_CASE("test_hadError");
}

void TestSQLiteDatabase::test_lastErrorCode() {
   TEST_CASE("test_lastErrorCode");
}

void TestSQLiteDatabase::test_lastInsertRowId() {
   TEST_CASE("test_lastInsertRowId");
}

void TestSQLiteDatabase::test_bindNull() {
   TEST_CASE("test_bindNull");
}

void TestSQLiteDatabase::test_bindData() {
   TEST_CASE("test_bindData");
}

void TestSQLiteDatabase::test_bindDate() {
   TEST_CASE("test_bindDate");
}

void TestSQLiteDatabase::test_bindBool() {
   TEST_CASE("test_bindBool");
}

void TestSQLiteDatabase::test_bindInt() {
   TEST_CASE("test_bindInt");
}

void TestSQLiteDatabase::test_bindLong() {
   TEST_CASE("test_bindLong");
}

void TestSQLiteDatabase::test_bindFloat() {
   TEST_CASE("test_bindFloat");
}

void TestSQLiteDatabase::test_bindDouble() {
   TEST_CASE("test_bindDouble");
}

void TestSQLiteDatabase::test_bindString() {
   TEST_CASE("test_bindString");
}

void TestSQLiteDatabase::test_executeQuery() {
   TEST_CASE("test_executeQuery");
}

void TestSQLiteDatabase::test_executeQuery_withArgs() {
   TEST_CASE("test_executeQuery_withArgs");
}

void TestSQLiteDatabase::test_executeUpdate() {
   TEST_CASE("test_executeUpdate");
}

void TestSQLiteDatabase::test_executeUpdate_withArgs() {
   TEST_CASE("test_executeUpdate_withArgs");
}

void TestSQLiteDatabase::test_rollback() {
   TEST_CASE("test_rollback");
}

void TestSQLiteDatabase::test_commit() {
   TEST_CASE("test_commit");
}

void TestSQLiteDatabase::test_beginDeferredTransaction() {
   TEST_CASE("test_beginDeferredTransaction");
}

void TestSQLiteDatabase::test_beginTransaction() {
   TEST_CASE("test_beginTransaction");
}

void TestSQLiteDatabase::test_logsErrors() {
   TEST_CASE("test_logsErrors");
}

void TestSQLiteDatabase::test_setLogsErrors() {
   TEST_CASE("test_setLogsErrors");
}

void TestSQLiteDatabase::test_crashOnErrors() {
   TEST_CASE("test_crashOnErrors");
}

void TestSQLiteDatabase::test_setCrashOnErrors() {
   TEST_CASE("test_setCrashOnErrors");
}

void TestSQLiteDatabase::test_inUse() {
   TEST_CASE("test_inUse");
}

void TestSQLiteDatabase::test_setInUse() {
   TEST_CASE("test_setInUse");
}

void TestSQLiteDatabase::test_inTransaction() {
   TEST_CASE("test_inTransaction");
}

void TestSQLiteDatabase::test_setInTransaction() {
   TEST_CASE("test_setInTransaction");
}

void TestSQLiteDatabase::test_traceExecution() {
   TEST_CASE("test_traceExecution");
}

void TestSQLiteDatabase::test_setTraceExecution() {
   TEST_CASE("test_setTraceExecution");
}

void TestSQLiteDatabase::test_checkedOut() {
   TEST_CASE("test_checkedOut");
}

void TestSQLiteDatabase::test_setCheckedOut() {
   TEST_CASE("test_setCheckedOut");
}

void TestSQLiteDatabase::test_busyRetryTimeout() {
   TEST_CASE("test_busyRetryTimeout");
}

void TestSQLiteDatabase::test_setBusyRetryTimeout() {
   TEST_CASE("test_setBusyRetryTimeout");
}

