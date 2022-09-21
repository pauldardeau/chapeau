#include "test_sqlite_statement.h"
#include "SQLiteDatabase.h"

TestSQLiteStatement::TestSQLiteStatement() :
   TestSuite("TestSQLiteStatement") {
}

void TestSQLiteStatement::runTests() {
   test_constructor();
   test_destructor();
   test_close();
   test_reset();
   test_statement();
   test_query();
   test_setQuery();
   test_useCount();
   test_incrementUseCount();
   test_description();
}

void TestSQLiteStatement::test_constructor() {
   TEST_CASE("test_constructor");
}

void TestSQLiteStatement::test_destructor() {
   TEST_CASE("test_destructor");
}

void TestSQLiteStatement::test_close() {
   TEST_CASE("test_close");
}

void TestSQLiteStatement::test_reset() {
   TEST_CASE("test_reset");
}

void TestSQLiteStatement::test_statement() {
   TEST_CASE("test_statement");
}

void TestSQLiteStatement::test_query() {
   TEST_CASE("test_query");
}

void TestSQLiteStatement::test_setQuery() {
   TEST_CASE("test_setQuery");
}

void TestSQLiteStatement::test_useCount() {
   TEST_CASE("test_useCount");
}

void TestSQLiteStatement::test_incrementUseCount() {
   TEST_CASE("test_incrementUseCount");
}

void TestSQLiteStatement::test_description() {
   TEST_CASE("test_description");
}

