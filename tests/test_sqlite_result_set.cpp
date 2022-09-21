#include "test_sqlite_result_set.h"
#include "SQLiteResultSet.h"

TestSQLiteResultSet::TestSQLiteResultSet() :
   TestSuite("TestSQLiteResultSet") {
}

void TestSQLiteResultSet::runTests() {
   test_constructor();
   test_destructor();
   test_close();
   test_columnIndexForName();
   test_statement();
   test_setStatement();
   test_setParentDB();
   test_next();
   test_intForColumn();
   test_intForColumnIndex();
   test_longForColumn();
   test_longForColumnIndex();
   test_boolForColumn();
   test_boolForColumnIndex();
   test_doubleForColumn();
   test_doubleForColumnIndex();
   test_stringForColumn();
   test_stringForColumnIndex();
   test_dateForColumn();
   test_dateForColumnIndex();
   test_dataForColumn();
   test_dataForColumnIndex();
   test_setupColumnNames();
}

void TestSQLiteResultSet::test_constructor() {
   TEST_CASE("test_constructor");
}

void TestSQLiteResultSet::test_destructor() {
   TEST_CASE("test_destructor");
}
 
void TestSQLiteResultSet::test_close() {
   TEST_CASE("test_close");
}
 
void TestSQLiteResultSet::test_columnIndexForName() {
   TEST_CASE("test_columnIndexForName");
}
 
void TestSQLiteResultSet::test_statement() {
   TEST_CASE("test_statement");
}
 
void TestSQLiteResultSet::test_setStatement() {
   TEST_CASE("test_setStatement");
}
 
void TestSQLiteResultSet::test_setParentDB() {
   TEST_CASE("test_setParentDB");
}
 
void TestSQLiteResultSet::test_next() {
   TEST_CASE("test_next");
}
 
void TestSQLiteResultSet::test_intForColumn() {
   TEST_CASE("test_intForColumn");
}
 
void TestSQLiteResultSet::test_intForColumnIndex() {
   TEST_CASE("test_intForColumnIndex");
}
 
void TestSQLiteResultSet::test_longForColumn() {
   TEST_CASE("test_longForColumn");
}
 
void TestSQLiteResultSet::test_longForColumnIndex() {
   TEST_CASE("test_longForColumnIndex");
}
 
void TestSQLiteResultSet::test_boolForColumn() {
   TEST_CASE("test_boolForColumn");
}
 
void TestSQLiteResultSet::test_boolForColumnIndex() {
   TEST_CASE("test_boolForColumnIndex");
}
 
void TestSQLiteResultSet::test_doubleForColumn() {
   TEST_CASE("test_doubleForColumn");
}
 
void TestSQLiteResultSet::test_doubleForColumnIndex() {
   TEST_CASE("test_doubleForColumnIndex");
}
 
void TestSQLiteResultSet::test_stringForColumn() {
   TEST_CASE("test_stringForColumn");
}
 
void TestSQLiteResultSet::test_stringForColumnIndex() {
   TEST_CASE("test_stringForColumnIndex");
}
 
void TestSQLiteResultSet::test_dateForColumn() {
   TEST_CASE("test_dateForColumn");
}
 
void TestSQLiteResultSet::test_dateForColumnIndex() {
   TEST_CASE("test_dateForColumnIndex");
}
 
void TestSQLiteResultSet::test_dataForColumn() {
   TEST_CASE("test_dataForColumn");
}
 
void TestSQLiteResultSet::test_dataForColumnIndex() {
   TEST_CASE("test_dataForColumnIndex");
}
 
void TestSQLiteResultSet::test_setupColumnNames() {
   TEST_CASE("test_setupColumnNames");
}
 
