#include "test_database.h"
#include "Database.h"


TestDatabase::TestDatabase() :
   TestSuite("TestDatabase") {
}

void TestDatabase::runTests() {
   test_bind_null();
   test_bind_bool();
   test_bind_int();
   test_bind_long();
   test_bind_float();
   test_bind_double();
   test_bind_string();
   test_bind_data();
   test_bind_date();

   test_shouldCacheStatements();
   test_setShouldCacheStatements();
   test_cachedStatementForQuery();
   test_setCachedStatement();
   test_clearCachedStatements();

   test_resultset_query();
   test_resultset_setQuery();
}

void TestDatabase::test_bind_null() {
   TEST_CASE("test_bind_null");
}

void TestDatabase::test_bind_bool() {
   TEST_CASE("test_bind_bool");
}

void TestDatabase::test_bind_int() {
   TEST_CASE("test_bind_int");
}

void TestDatabase::test_bind_long() {
   TEST_CASE("test_bind_long");
}

void TestDatabase::test_bind_float() {
   TEST_CASE("test_bind_float");
}

void TestDatabase::test_bind_double() {
   TEST_CASE("test_bind_double");
}

void TestDatabase::test_bind_string() {
   TEST_CASE("test_bind_string");
}

void TestDatabase::test_bind_data() {
   TEST_CASE("test_bind_data");
}

void TestDatabase::test_bind_date() {
   TEST_CASE("test_bind_date");
}

void TestDatabase::test_shouldCacheStatements() {
   TEST_CASE("test_shouldCacheStatements");
}

void TestDatabase::test_setShouldCacheStatements() {
   TEST_CASE("test_setShouldCacheStatements");
}

void TestDatabase::test_cachedStatementForQuery() {
   TEST_CASE("test_cachedStatementForQuery");
}

void TestDatabase::test_setCachedStatement() {
   TEST_CASE("test_setCachedStatement");
}

void TestDatabase::test_clearCachedStatements() {
   TEST_CASE("test_clearCachedStatements");
}

void TestDatabase::test_resultset_query() {
   TEST_CASE("test_resultset_query");
}

void TestDatabase::test_resultset_setQuery() {
   TEST_CASE("test_resultset_setQuery");
}

